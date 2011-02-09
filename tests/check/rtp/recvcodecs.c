/* Farsight 2 unit tests for FsRtpConference
 *
 * Copyright (C) 2007 Collabora, Nokia
 * @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/rtp/gstrtpbuffer.h>

#include <gst/farsight/fs-conference-iface.h>
#include <gst/farsight/fs-element-added-notifier.h>

#include "check-threadsafe.h"

#define SEND_BUFFER_COUNT 100
#define BUFFER_COUNT 20

GMutex *count_mutex;
GCond *count_cond;
guint buffer_count = 0;


static void
handoff_handler (GstElement *fakesink, GstBuffer *buffer, GstPad *pad,
    gpointer user_data)
{
  g_mutex_lock (count_mutex);
  buffer_count ++;

  GST_LOG ("buffer %d", buffer_count);

  if (buffer_count == BUFFER_COUNT)
    g_cond_broadcast (count_cond);
  ts_fail_unless (buffer_count <= SEND_BUFFER_COUNT);
  g_mutex_unlock (count_mutex);
}

static void
src_pad_added_cb (FsStream *self,
    GstPad   *pad,
    FsCodec  *codec,
    GstElement *pipeline)
{
  GstElement *sink;
  GstPad *sinkpad;

  sink = gst_element_factory_make ("fakesink", NULL);
  g_object_set (sink, "sync", TRUE,
      "signal-handoffs", TRUE,
      NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (handoff_handler), NULL);
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  gst_element_set_state (sink, GST_STATE_PLAYING);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (pad, sinkpad)));
  gst_object_unref (sinkpad);

  GST_DEBUG ("Pad added");
}

static guint decoder_count = 0;

static void
element_added (FsElementAddedNotifier *notif, GstBin *bin, GstElement *element,
    gpointer user_data)
{
  GstElementFactory *fact = gst_element_get_factory (element);

  if (!fact)
    return;

  if (strcmp (gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (fact)),
              "theoradec"))
    return;

  ts_fail_unless (decoder_count == 0);
  decoder_count++;
}

static void
caps_changed (GstPad *pad, GParamSpec *spec, FsStream *stream)
{
  GstCaps *caps;
  GstStructure *s;
  FsCodec *codec;
  GList *codecs;
  const gchar *config;
  GError *error = NULL;

  g_object_get (pad, "caps", &caps, NULL);

  if (!caps)
    return;

  s = gst_caps_get_structure (caps, 0);

  codec = fs_codec_new (96, "THEORA", FS_MEDIA_TYPE_VIDEO, 90000);

  config = gst_structure_get_string (s, "configuration");
  if (config)
    fs_codec_add_optional_parameter (codec, "configuration", config);

  codecs = g_list_prepend (NULL, codec);
  fail_unless (fs_stream_set_remote_codecs (stream, codecs, &error),
      "Unable to set remote codec: %s",
      error ? error->message : "UNKNOWN");
  fs_codec_list_destroy (codecs);
}

static gboolean
drop_theora_config (GstPad *pad, GstBuffer *buffer, gpointer user_data)
{
  guint8 *payload = gst_rtp_buffer_get_payload (buffer);
  guint32 header;
  guchar TDT;

  header = GST_READ_UINT32_BE (payload);
  /*
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Ident                     | F |TDT|# pkts.|
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * F: Fragment type (0=none, 1=start, 2=cont, 3=end)
   * TDT: Theora data type (0=theora, 1=config, 2=comment, 3=reserved)
   * pkts: number of packets.
   */
  TDT = (header & 0x30) >> 4;

  if (TDT == 1)
    return FALSE;
  else
    return TRUE;
}

GST_START_TEST (test_rtprecv_inband_config_data)
{
  FsParticipant *participant = NULL;
  FsStream *stream = NULL;
  GstElement *src;
  GstElement *pipeline;
  GstElement *sink;
  GstBus *bus;
  GstMessage *msg;
  guint port = 0;
  GError *error = NULL;
  GList *codecs = NULL;
  GstElement *fspipeline;
  GstElement *conference;
  FsSession *session;
  GList *item;
  GstPad *pad;
  GstElement *pay;
  FsElementAddedNotifier *notif;

  count_mutex = g_mutex_new ();
  count_cond = g_cond_new ();
  buffer_count = 0;
  decoder_count = 0;

  fspipeline = gst_pipeline_new (NULL);

  notif = fs_element_added_notifier_new ();
  fs_element_added_notifier_add (notif, GST_BIN (fspipeline));
  g_signal_connect (notif, "element-added", G_CALLBACK (element_added), NULL);


  conference = gst_element_factory_make ("fsrtpconference", NULL);

  fail_unless (gst_bin_add (GST_BIN (fspipeline), conference));

  session = fs_conference_new_session (FS_CONFERENCE (conference),
      FS_MEDIA_TYPE_VIDEO, &error);
  if (error)
    fail ("Error while creating new session (%d): %s",
        error->code, error->message);
  fail_if (session == NULL, "Could not make session, but no GError!");
  g_object_set (session, "no-rtcp-timeout", 0, NULL);

  g_object_get (session, "codecs-without-config", &codecs, NULL);
  for (item = codecs; item; item = item->next)
  {
    FsCodec *codec = item->data;

    if (!g_ascii_strcasecmp ("THEORA", codec->encoding_name))
      break;
  }
  fs_codec_list_destroy (codecs);

  if (!item)
  {
    GST_INFO ("Skipping %s because THEORA is not detected", G_STRFUNC);
    g_object_unref (session);
    gst_object_unref (fspipeline);
    return;
  }

  participant = fs_conference_new_participant (
      FS_CONFERENCE (conference), &error);
  if (error)
    ts_fail ("Error while creating new participant (%d): %s",
        error->code, error->message);
  ts_fail_if (participant == NULL,
      "Could not make participant, but no GError!");

  stream = fs_session_new_stream (session, participant, FS_DIRECTION_RECV,
      &error);
  if (error)
    ts_fail ("Error while creating new stream (%d): %s",
        error->code, error->message);
  ts_fail_if (stream == NULL, "Could not make stream, but no GError!");

  fail_unless (fs_stream_set_transmitter (stream, "rawudp", NULL, 0, &error));
  fail_unless (error == NULL);

  g_signal_connect (stream, "src-pad-added",
      G_CALLBACK (src_pad_added_cb), fspipeline);

  codecs = g_list_prepend (NULL, fs_codec_new (96, "THEORA",
          FS_MEDIA_TYPE_VIDEO, 90000));
  fail_unless (fs_stream_set_remote_codecs (stream, codecs,
          &error),
      "Unable to set remote codec: %s",
      error ? error->message : "UNKNOWN");

  fs_codec_list_destroy (codecs);


  pipeline = gst_parse_launch (
      "videotestsrc is-live=1 name=src num-buffers="G_STRINGIFY (BUFFER_COUNT) " !"
      " video/x-raw-yuv, framerate=(fraction)30/1 ! theoraenc !"
      " rtptheorapay name=pay config-interval=0 name=pay !"
      " application/x-rtp, payload=96, ssrc=(uint)12345678 !"
      " udpsink host=127.0.0.1 name=sink", NULL);

  gst_element_set_state (fspipeline, GST_STATE_PLAYING);


  bus = gst_element_get_bus (fspipeline);
  while (port == 0)
  {
    const GstStructure *s;

    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_ELEMENT);
    fail_unless (msg != NULL);
    s = gst_message_get_structure (msg);

    fail_if (gst_structure_has_name (s, "farsight-local-candidates-prepared"));

    if (gst_structure_has_name (s, "farsight-new-local-candidate"))
    {
      const GValue *value;
      FsCandidate *candidate;

      ts_fail_unless (
          gst_structure_has_field_typed (s, "candidate", FS_TYPE_CANDIDATE),
          "farsight-new-local-candidate structure has no candidate field");

      value = gst_structure_get_value (s, "candidate");
      candidate = g_value_get_boxed (value);

      if (candidate->type == FS_CANDIDATE_TYPE_HOST)
        port = candidate->port;

      GST_DEBUG ("Got port %u", port);
    }

    gst_message_unref (msg);
  }

  gst_object_unref (bus);

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  g_object_set (sink, "port", port, NULL);
  gst_object_unref (sink);


  pay = gst_bin_get_by_name (GST_BIN (pipeline), "pay");
  ts_fail_unless (pay != NULL);
  pad = gst_element_get_static_pad (pay, "src");
  ts_fail_unless (pad != NULL);
  gst_pad_add_buffer_probe (pad, G_CALLBACK (drop_theora_config), NULL);
  g_signal_connect (pad, "notify::caps", G_CALLBACK (caps_changed), stream);
  caps_changed (pad, NULL, stream);
  gst_object_unref (pad);
  gst_object_unref (pay);


  gst_element_set_state (pipeline, GST_STATE_PLAYING);


  g_mutex_lock (count_mutex);
  while (buffer_count < BUFFER_COUNT)
    g_cond_wait (count_cond, count_mutex);
  buffer_count = 0;
  g_mutex_unlock (count_mutex);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  g_object_set (src, "num-buffers", SEND_BUFFER_COUNT, NULL);
  gst_object_unref (src);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_mutex_lock (count_mutex);
  while (buffer_count < BUFFER_COUNT)
    g_cond_wait (count_cond, count_mutex);
  buffer_count = 0;
  g_mutex_unlock (count_mutex);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  bus = gst_element_get_bus (fspipeline);
  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_ERROR);
  if (msg)
  {
    GError *error;
    gchar *debug;

    gst_message_parse_error (msg, &error, &debug);

    ts_fail ("Got an error on the BUS (%d): %s (%s)", error->code,
        error->message, debug);
    g_error_free (error);
    g_free (debug);
    gst_message_unref (msg);
  }
  gst_object_unref (bus);


  gst_object_unref (pipeline);
  gst_object_unref (participant);
  gst_object_unref (stream);
  gst_object_unref (session);

  gst_element_set_state (fspipeline, GST_STATE_NULL);

  gst_object_unref (fspipeline);

  g_mutex_free (count_mutex);
  g_cond_free (count_cond);
}
GST_END_TEST;


static Suite *
fsrtprecvcodecs_suite (void)
{
  Suite *s = suite_create ("fsrtprecvcodecs");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);

  tc_chain = tcase_create ("fsrtprecv_inband_config_data");
  tcase_add_test (tc_chain, test_rtprecv_inband_config_data);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (fsrtprecvcodecs);

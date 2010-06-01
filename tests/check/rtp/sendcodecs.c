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

#include "check-threadsafe.h"
#include "generic.h"

GMainLoop *loop = NULL;

FsDTMFMethod method = FS_DTMF_METHOD_AUTO;
guint dtmf_id = 0;
gint digit = 0;
gboolean sending = FALSE;
gboolean received = FALSE;
gboolean ready_to_send = FALSE;
gboolean change_codec = FALSE;

struct SimpleTestConference *dat = NULL;
FsStream *stream = NULL;

static gboolean
_start_pipeline (gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;

  GST_DEBUG ("%d: Starting pipeline", dat->id);

  ts_fail_if (gst_element_set_state (dat->pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  dat->started = TRUE;

  return FALSE;
}

static gboolean
_bus_callback (GstBus *bus, GstMessage *message, gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;

  switch (GST_MESSAGE_TYPE (message))
  {

    case GST_MESSAGE_ELEMENT:
      {
        const GstStructure *s = gst_message_get_structure (message);

        if (gst_implements_interface_check (GST_MESSAGE_SRC (message),
                FS_TYPE_CONFERENCE) &&
            gst_structure_has_name (s, "farsight-error"))
        {
          const GValue *value;
          FsError errorno;
          const gchar *error, *debug;
          GEnumClass *enumclass = NULL;
          GEnumValue *enumvalue = NULL;

          ts_fail_unless (
              gst_implements_interface_check (GST_MESSAGE_SRC (message),
                  FS_TYPE_CONFERENCE),
              "Received farsight-error from non-farsight element");

          ts_fail_unless (
              gst_structure_has_field_typed (s, "src-object", G_TYPE_OBJECT),
              "farsight-error structure has no src-object field");
          ts_fail_unless (
              gst_structure_has_field_typed (s, "error-no", FS_TYPE_ERROR),
              "farsight-error structure has no src-object field");
          ts_fail_unless (
              gst_structure_has_field_typed (s, "error-msg", G_TYPE_STRING),
              "farsight-error structure has no src-object field");
          ts_fail_unless (
              gst_structure_has_field_typed (s, "debug-msg", G_TYPE_STRING),
              "farsight-error structure has no src-object field");

          value = gst_structure_get_value (s, "error-no");
          errorno = g_value_get_enum (value);
          error = gst_structure_get_string (s, "error-msg");
          debug = gst_structure_get_string (s, "debug-msg");


          enumclass = g_type_class_ref (FS_TYPE_ERROR);
          enumvalue = g_enum_get_value (enumclass, errorno);
          ts_fail ("Error on BUS %s (%d, %s) %s .. %s",
              enumvalue->value_name, errorno, enumvalue->value_nick,
              error, debug);
          g_type_class_unref (enumclass);
        }
        else if (gst_structure_has_name (s, "farsight-send-codec-changed"))
        {
          FsCodec *codec = NULL;
          GList *secondary_codec_list = NULL;
          GList *item;

          ts_fail_unless (gst_structure_get ((GstStructure *) s,
                  "secondary-codecs", FS_TYPE_CODEC_LIST, &secondary_codec_list,
                  "codec", FS_TYPE_CODEC, &codec,
                  NULL));

          ts_fail_unless (codec != NULL);
          ts_fail_unless (secondary_codec_list != NULL);

          for (item = secondary_codec_list; item; item = item->next)
          {
            FsCodec *codec = item->data;

            if (codec->clock_rate == 8000 &&
                !g_strcasecmp ("telephone-event", codec->encoding_name))
            {
              ts_fail_unless (codec->id == dtmf_id);
              ready_to_send = TRUE;
            }
          }

          fail_unless (ready_to_send == TRUE);

          fs_codec_list_destroy (secondary_codec_list);
          fs_codec_destroy (codec);
        }

      }
      break;
    case GST_MESSAGE_ERROR:
      {
        GError *error = NULL;
        gchar *debug = NULL;
        gst_message_parse_error (message, &error, &debug);

        ts_fail ("Got an error on the BUS (%d): %s (%s)", error->code,
            error->message, debug);
        g_error_free (error);
        g_free (debug);
      }
      break;
    case GST_MESSAGE_WARNING:
      {
        GError *error = NULL;
        gchar *debug = NULL;
        gst_message_parse_warning (message, &error, &debug);

        GST_WARNING ("%d: Got a warning on the BUS (%d): %s (%s)", dat->id,
            error->code,
            error->message, debug);
        g_error_free (error);
        g_free (debug);
      }
      break;
    default:
      break;
  }

  return TRUE;
}

static GstElement *
build_recv_pipeline (GCallback havedata_handler, gpointer data, gint *port)
{
  GstElement *pipeline;
  GstElement *src;
  GstElement *sink;
  GstPad *pad = NULL;

  pipeline = gst_pipeline_new (NULL);

  src = gst_element_factory_make ("udpsrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  g_object_set (sink, "sync", FALSE, NULL);

  ts_fail_unless (pipeline && src && sink, "Could not make pipeline(%p)"
      " or src(%p) or sink(%p)", pipeline, src, sink);

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);

  ts_fail_unless (gst_element_link (src, sink), "Could not link udpsrc"
      " and fakesink");

  pad = gst_element_get_static_pad (sink, "sink");

  gst_pad_add_buffer_probe (pad, havedata_handler, data);

  gst_object_ref (pad);

  ts_fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE, "Could not start recv pipeline");

  g_object_get (G_OBJECT (src), "port", port, NULL);

  return pipeline;
}

static void
set_codecs (struct SimpleTestConference *dat, FsStream *stream)
{
  GList *codecs = NULL;
  GList *filtered_codecs = NULL;
  GList *item = NULL;
  GError *error = NULL;
  FsCodec *dtmf_codec = NULL;

  g_object_get (dat->session, "codecs", &codecs, NULL);

  ts_fail_if (codecs == NULL, "Could not get the local codecs");

  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == 0)
    {
      filtered_codecs = g_list_append (filtered_codecs, codec);
    }
    else if (codec->clock_rate == 8000 &&
        !g_ascii_strcasecmp (codec->encoding_name, "telephone-event"))
    {
      ts_fail_unless (dtmf_codec == NULL,
          "More than one copy of telephone-event");
      dtmf_codec = codec;
      filtered_codecs = g_list_append (filtered_codecs, codec);
    }
  }

  ts_fail_if (filtered_codecs == NULL, "PCMA and PCMU are not in the codecs"
      " you must install gst-plugins-good");

  ts_fail_unless (dtmf_codec != NULL);
  dtmf_codec->id = dtmf_id;

  if (!fs_stream_set_remote_codecs (stream, filtered_codecs, &error))
  {
    if (error)
      ts_fail ("Could not set the remote codecs on stream (%d): %s",
          error->code,
          error->message);
    else
      ts_fail ("Could not set the remote codecs on stream"
          " and we did NOT get a GError!!");
  }

  g_list_free (filtered_codecs);
  fs_codec_list_destroy (codecs);
}

static void
one_way (GstElement *recv_pipeline, gint port)
{
  FsParticipant *participant = NULL;
  GError *error = NULL;
  GList *candidates = NULL;
  GstBus *bus = NULL;

  dtmf_id = 105;
  digit = 0;
  sending = FALSE;
  received = FALSE;
  ready_to_send = FALSE;

  loop = g_main_loop_new (NULL, FALSE);

  dat = setup_simple_conference (1, "fsrtpconference", "tester@123445");

  bus = gst_element_get_bus (dat->pipeline);
  gst_bus_add_watch (bus, _bus_callback, dat);
  gst_object_unref (bus);

  g_idle_add (_start_pipeline, dat);

  participant = fs_conference_new_participant (
      FS_CONFERENCE (dat->conference), "blob@blob.com", &error);
  if (error)
    ts_fail ("Error while creating new participant (%d): %s",
        error->code, error->message);
  ts_fail_if (dat->session == NULL,
      "Could not make participant, but no GError!");

  stream = fs_session_new_stream (dat->session, participant,
      FS_DIRECTION_SEND, "rawudp", 0, NULL, &error);
  if (error)
    ts_fail ("Error while creating new stream (%d): %s",
        error->code, error->message);
  ts_fail_if (stream == NULL, "Could not make stream, but no GError!");

  GST_DEBUG ("port is %d", port);

  candidates = g_list_prepend (NULL,
      fs_candidate_new ("1", FS_COMPONENT_RTP, FS_CANDIDATE_TYPE_HOST,
          FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", port));
  ts_fail_unless (fs_stream_set_remote_candidates (stream, candidates, &error),
      "Could not set remote candidate");
  fs_candidate_list_destroy (candidates);

  set_codecs (dat, stream);

  setup_fakesrc (dat);

  g_main_loop_run (loop);

  gst_element_set_state (dat->pipeline, GST_STATE_NULL);
  gst_element_set_state (recv_pipeline, GST_STATE_NULL);

  cleanup_simple_conference (dat);
  gst_object_unref (recv_pipeline);

  g_main_loop_unref (loop);
}


static void
send_dmtf_havedata_handler (GstPad *pad, GstBuffer *buf, gpointer user_data)
{
  gchar *data;

  ts_fail_unless (gst_rtp_buffer_validate (buf), "Buffer is not valid rtp");

  if (gst_rtp_buffer_get_payload_type (buf) != dtmf_id)
    return;

  data = gst_rtp_buffer_get_payload (buf);

  if (data[0] < digit)
  {
    /* Still on previous digit */
    return;
  }

  GST_LOG ("Got digit %d", data[0]);

  ts_fail_if (data[0] != digit, "Not sending the right digit"
      " (sending %d, should be %d", data[0], digit);

  received = TRUE;
}


static gboolean
start_stop_sending_dtmf (gpointer data)
{
  GstState state;
  GstStateChangeReturn ret;

  if (!dat || !dat->pipeline || !dat->session)
    return TRUE;

  ret = gst_element_get_state (dat->pipeline, &state, NULL, 0);
  ts_fail_if (ret == GST_STATE_CHANGE_FAILURE);

  if (ret != GST_STATE_CHANGE_SUCCESS || state != GST_STATE_PLAYING)
    return TRUE;

  if (!ready_to_send)
    return TRUE;


  if (sending)
  {
    ts_fail_unless (fs_session_stop_telephony_event (dat->session, method),
        "Could not stop telephony event");
    sending = FALSE;
  }
  else
  {
    if (digit)
      ts_fail_unless (received == TRUE,
          "Did not receive any buffer for digit %d", digit);

    if (digit >= FS_DTMF_EVENT_D)
    {
      if (change_codec)
      {
        digit = 0;
        dtmf_id++;
        ready_to_send = FALSE;
        change_codec = FALSE;
        set_codecs (dat, stream);
        return TRUE;
      }
      else
      {
        g_main_loop_quit (loop);
        return FALSE;
      }
    }
    digit++;

    received = FALSE;
    ts_fail_unless (fs_session_start_telephony_event (dat->session,
            digit, digit, method),
        "Could not start telephony event");
    sending = TRUE;
  }

  return TRUE;
}

GST_START_TEST (test_senddtmf_event)
{
  gint port;
  GstElement *recv_pipeline = build_recv_pipeline (
      G_CALLBACK (send_dmtf_havedata_handler), NULL, &port);

  method = FS_DTMF_METHOD_RTP_RFC4733;
  g_timeout_add (200, start_stop_sending_dtmf, NULL);
  one_way (recv_pipeline, port);
}
GST_END_TEST;


GST_START_TEST (test_senddtmf_auto)
{
  gint port;
  GstElement *recv_pipeline = build_recv_pipeline (
      G_CALLBACK (send_dmtf_havedata_handler), NULL, &port);

  method = FS_DTMF_METHOD_AUTO;
  g_timeout_add (200, start_stop_sending_dtmf, NULL);
  one_way (recv_pipeline, port);
}
GST_END_TEST;


static gboolean
dtmf_bus_watch (GstBus *bus, GstMessage *message, gpointer data)
{
  const GstStructure *s;
  int d;

  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return TRUE;

  s = gst_message_get_structure (message);

  if (!gst_structure_has_name (s, "dtmf-event"))
    return TRUE;


  if (gst_structure_get_int (s, "number", &d)) {
    GST_LOG ("Got digit %d", d);
    if (digit == d)
      received = TRUE;
  }


  return TRUE;
}

static GstElement *
build_dtmf_sound_recv_pipeline (gint *port)
{
  GstElement *pipeline;
  GstElement *src;
  GstBus *bus;

  pipeline = gst_parse_launch (
      "udpsrc name=src caps=\"application/x-rtp, payload=0\" !"
      " rtppcmudepay ! mulawdec ! dtmfdetect ! fakesink sync=0", NULL);
  fail_if (pipeline == NULL);

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, dtmf_bus_watch, NULL);
  gst_object_unref (bus);

  ts_fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE, "Could not start recv pipeline");

  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  fail_if (src == NULL);
  g_object_get (G_OBJECT (src), "port", port, NULL);
  gst_object_unref (src);

  return pipeline;
}


GST_START_TEST (test_senddtmf_sound)
{
  gint port = 0;
  GstElement *recv_pipeline = build_dtmf_sound_recv_pipeline (&port);

  method = FS_DTMF_METHOD_IN_BAND;
  g_timeout_add (200, start_stop_sending_dtmf, NULL);
  one_way (recv_pipeline, port);
}
GST_END_TEST;


GST_START_TEST (test_senddtmf_change_auto)
{
  gint port;
  GstElement *recv_pipeline = build_recv_pipeline (
      G_CALLBACK (send_dmtf_havedata_handler), NULL, &port);

  method = FS_DTMF_METHOD_AUTO;
  change_codec = TRUE;
  g_timeout_add (200, start_stop_sending_dtmf, NULL);
  one_way (recv_pipeline, port);
}
GST_END_TEST;

gboolean checked = FALSE;

static void
change_ssrc_handler (GstPad *pad, GstBuffer *buf, gpointer user_data)
{
  guint sess_ssrc;
  guint buf_ssrc;

  ts_fail_unless (gst_rtp_buffer_validate (buf));

  buf_ssrc = gst_rtp_buffer_get_ssrc (buf);

  g_object_get (dat->session, "ssrc", &sess_ssrc, NULL);

  if (buf_ssrc == 12345)
  {
    /* Step two, set it to 6789 */
    ts_fail_unless (buf_ssrc == sess_ssrc || sess_ssrc == 6789);

    g_object_set (dat->session, "ssrc", 6789, NULL);
  }
  else if (buf_ssrc == 6789)
  {
    /* Step three, quit */
    ts_fail_unless (buf_ssrc == sess_ssrc);

    g_main_loop_quit (loop);
  }
  else
  {
    ts_fail_unless (checked || buf_ssrc == sess_ssrc);
    checked = TRUE;

    /* Step one, set the ssrc to 12345 */
    if (sess_ssrc != 12345)
      g_object_set (dat->session, "ssrc", 12345, NULL);
  }
}

GST_START_TEST (test_change_ssrc)
{
  gint port;
  GstElement *recv_pipeline = build_recv_pipeline (
      G_CALLBACK (change_ssrc_handler), NULL, &port);

  checked = FALSE;
  one_way (recv_pipeline, port);
}
GST_END_TEST;


static Suite *
fsrtpsendcodecs_suite (void)
{
  Suite *s = suite_create ("fsrtpsendcodecs");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);


  tc_chain = tcase_create ("fsrtpsenddtmf_event");
  tcase_add_test (tc_chain, test_senddtmf_event);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpsenddtmf_auto");
  tcase_add_test (tc_chain, test_senddtmf_auto);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpsenddtmf_sound");
  tcase_add_test (tc_chain, test_senddtmf_sound);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpsenddtmf_change_auto");
  tcase_add_test (tc_chain, test_senddtmf_change_auto);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpchangessrc");
  tcase_add_test (tc_chain, test_change_ssrc);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (fsrtpsendcodecs);

/* Farsight 2 unit tests for FsRtpConferenceu
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
#include <gst/farsight/fs-conference-iface.h>

#include "generic.h"

struct SimpleTestConference *dat1 = NULL;
struct SimpleTestConference *dat2 = NULL;
GMainLoop *loop;


GST_START_TEST (test_rtpconference_new)
{
  struct SimpleTestConference *dat = NULL;
  guint id = 999;
  GList *local_codecs = NULL;
  FsMediaType *media_type;
  GstPad *sinkpad = NULL;
  gchar *str;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");

  g_object_get (dat->session,
      "id", &id,
      "local-codecs", &local_codecs,
      "media-type", &media_type,
      "sink-pad", &sinkpad,
      NULL);

  fail_unless (id == 1, "The id of the first session should be 1 not %d", id);
  fail_if (local_codecs == NULL, "Local codecs should not be NULL");
  fail_unless (media_type == FS_MEDIA_TYPE_AUDIO, "Media type isnt audio,"
      " its %d", media_type);
  fail_if (sinkpad == NULL, "Sink pad should not be null");
  str = g_strdup_printf ("sink_%d", id);
  fail_unless (!strcmp (str, GST_OBJECT_NAME (sinkpad)), "Sink pad is %s"
      " instead of being %d", GST_OBJECT_NAME (sinkpad), str);
  g_free (str);

  cleanup_simple_conference (dat);
}
GST_END_TEST;

static gboolean
_simple_bus_callback (GstBus *bus, GstMessage *message, gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;

  switch (GST_MESSAGE_TYPE (message))
  {
    case GST_MESSAGE_ELEMENT:
      if (gst_implements_interface_check (GST_MESSAGE_SRC (message),
              FS_TYPE_CONFERENCE))
      {
        const GValue *errorvalue, *debugvalue;
        gint errno;

        gst_structure_get_int (message->structure, "error-no", &errno);
        errorvalue = gst_structure_get_value (message->structure, "error-msg");
        debugvalue = gst_structure_get_value (message->structure, "debug-msg");

        fail ("Error on BUS (%d) %s .. %s", errno,
            g_value_get_string (errorvalue),
            g_value_get_string (debugvalue));
      }

      break;
    case GST_MESSAGE_ERROR:
      {
        GError *error = NULL;
        gchar *debug = NULL;
        gst_message_parse_error (message, &error, &debug);

        fail ("Got an error on the BUS (%d): %s (%s)", error->code,
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

        g_debug ("%d: Got a warning on the BUS (%d): %s (%s)", dat->id,
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

static void
_simple_send_codec_changed (FsSession *session, gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;
  FsCodec *codec = NULL;
  gchar *str = NULL;

  g_object_get (session, "current-send-codec", &codec, NULL);
  fail_if (codec == NULL, "Could not get new send codec");

  str = fs_codec_to_string (codec);
  g_debug ("%d: New send codec: %s", dat->id, str);
  g_free (str);

  fs_codec_destroy (codec);
}

static void
_handoff_handler (GstElement *element, GstBuffer *buffer, GstPad *pad,
  gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;

  dat->buffer_count++;

  if (dat->buffer_count % 10 == 0)
    g_debug ("%d: Buffer %d", dat->id, dat->buffer_count);

  /*
  fail_if (dat->buffer_count > 20,
    "Too many buffers %d > 20", dat->buffer_count);
  */

  if (dat1->buffer_count >= 20 && dat2->buffer_count >= 20) {
    /* TEST OVER */
    g_main_loop_quit (loop);
  }
}

static void
_src_pad_added (FsStream *self, GstPad *pad, FsCodec *codec, gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);
  GstPad *fakesink_pad = NULL;
  GstPadLinkReturn ret;

  g_assert (fakesink);

  g_object_set (fakesink,
      "signal-handoffs", TRUE,
      "sync", TRUE,
      "async", TRUE,
      NULL);

  g_signal_connect (fakesink, "handoff", G_CALLBACK (_handoff_handler), dat);

  gst_bin_add (GST_BIN (dat->pipeline), fakesink);

  fakesink_pad = gst_element_get_static_pad (fakesink, "sink");
  ret = gst_pad_link (pad, fakesink_pad);
  gst_object_unref (fakesink_pad);

  fail_if (GST_PAD_LINK_FAILED(ret), "Could not link fakesink");

  fail_if (gst_element_set_state (fakesink, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE, "Could not set the fakesink to playing");

  g_debug ("%d: Added Fakesink", dat->id);
}


static void
_new_active_candidate_pair (FsStream *stream, FsCandidate *local,
    FsCandidate *remote, gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;

  fail_if (local == NULL, "Local candidate NULL");
  fail_if (remote == NULL, "Remote candidate NULL");

  if (local->component_id != 1)
    return;

  if (!dat->fakesrc)
    setup_fakesrc (dat);
}


void
rtpconference_simple_connect_signals (struct SimpleTestConference *dat)
{
  GstBus *bus = NULL;

  bus = gst_element_get_bus (dat->pipeline);
  gst_bus_add_watch (bus, _simple_bus_callback, dat);
  gst_object_unref (bus);

  g_signal_connect (dat->session, "send-codec-changed",
      G_CALLBACK (_simple_send_codec_changed), dat);

  g_signal_connect (dat->stream, "src-pad-added", G_CALLBACK (_src_pad_added),
      dat);

  g_signal_connect (dat->stream, "new-active-candidate-pair",
      G_CALLBACK (_new_active_candidate_pair), dat);
}


static gboolean
_start_pipeline (gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;

  g_debug ("%d: Starting pipeline", dat->id);

  fail_if (gst_element_set_state (dat->pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  dat->started = TRUE;

  return FALSE;
}

static void
_new_negotiated_codecs (FsSession *session, gpointer user_data)
{
  GList *codecs = NULL;
  GError *error = NULL;
  struct SimpleTestConference *dat = user_data;

  g_debug ("%d: New negotiated codecs", dat->id);

  fail_if (session != dat2->session, "Got signal from the wrong object");

  g_object_get (dat2->session, "negotiated-codecs", &codecs, NULL);
  fail_if (codecs == NULL, "Could not get the negotiated codecs");


  if (!fs_stream_set_remote_codecs (dat1->stream, codecs, &error))
  {
    if (error)
      fail ("Could not set the remote codecs on dat1 (%d): %s", error->code,
          error->message);
    else
      fail ("Could not set the remote codecs on dat1"
          " and we DID not get a GError!!");
  }

  fs_codec_list_destroy (codecs);
}


static void
_new_local_candidate (FsStream *stream, FsCandidate *candidate,
    gpointer user_data)
{
  struct SimpleTestConference *other_dat = user_data;
  gboolean ret;
  GError *error = NULL;

  g_debug ("%d: Setting remove candidate", other_dat->id);

  ret = fs_stream_add_remote_candidate (other_dat->stream, candidate, &error);

  if (error)
    fail ("Error while adding candidate: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);

  fail_unless(ret == TRUE, "No detailed error from add_remote_candidate");

}

void
set_local_codecs (void)
{
  GList *local_codecs = NULL;
  GList *filtered_codecs = NULL;
  GList *item = NULL;
  GError *error = NULL;

  g_object_get (dat1->session, "local-codecs", &local_codecs, NULL);

  fail_if (local_codecs == NULL, "Could not get the local codecs");

  for (item = g_list_first (local_codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == 0 || codec->id == 8)
      filtered_codecs = g_list_append (filtered_codecs, codec);
  }

  fail_if (filtered_codecs == NULL, "PCMA and PCMU are not in the codecs"
      " you must install gst-plugins-good");


  if (!fs_stream_set_remote_codecs (dat2->stream, filtered_codecs, &error))
  {
    if (error)
      fail ("Could not set the remote codecs on dat2 (%d): %s", error->code,
          error->message);
    else
      fail ("Could not set the remote codecs on dat2"
          " and we DID not get a GError!!");
  }

  g_list_free (filtered_codecs);
  fs_codec_list_destroy (local_codecs);
}


GST_START_TEST (test_rtpconference_simple)
{

  loop = g_main_loop_new (NULL, FALSE);

  dat1 = setup_simple_conference (1, "fsrtpconference", "tester@TesterTop3");
  dat2 = setup_simple_conference (2, "fsrtpconference", "tester@TesterTop3");

  rtpconference_simple_connect_signals (dat1);
  rtpconference_simple_connect_signals (dat2);

  g_idle_add (_start_pipeline, dat1);
  g_idle_add (_start_pipeline, dat2);

  g_signal_connect (dat2->session, "new-negotiated-codecs",
      G_CALLBACK (_new_negotiated_codecs), dat2);

  set_local_codecs ();

  g_signal_connect (dat1->stream, "new-local-candidate",
      G_CALLBACK (_new_local_candidate), dat2);
  g_signal_connect (dat2->stream, "new-local-candidate",
      G_CALLBACK (_new_local_candidate), dat1);

  g_main_loop_run (loop);

  gst_element_set_state (dat1->pipeline, GST_STATE_NULL);
  gst_element_set_state (dat2->pipeline, GST_STATE_NULL);

  cleanup_simple_conference (dat1);
  cleanup_simple_conference (dat2);

  g_main_loop_unref (loop);
}
GST_END_TEST;



static Suite *
fsrtpconference_suite (void)
{
  Suite *s = suite_create ("fsrtpconference");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);


  tc_chain = tcase_create ("fsrtpconfence_base");
  tcase_set_timeout (tc_chain, 1);
  tcase_add_test (tc_chain, test_rtpconference_new);
  suite_add_tcase (s, tc_chain);


  tc_chain = tcase_create ("fsrtpconfence_simple");
  tcase_set_timeout (tc_chain, 10);
  tcase_add_test (tc_chain, test_rtpconference_simple);
  suite_add_tcase (s, tc_chain);


  return s;
}


GST_CHECK_MAIN (fsrtpconference);

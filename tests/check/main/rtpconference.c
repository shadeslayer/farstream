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
#include <gst/farsight/fs-stream-transmitter.h>

#include "check-threadsafe.h"

#include "generic.h"

struct SimpleTestConference **dats;
GMainLoop *loop;
int count = 0;

// Options
gboolean select_last_codec = FALSE;
gboolean reset_to_last_codec = FALSE;

#define WAITING_ON_LAST_CODEC   (1<<0)
#define SHOULD_BE_LAST_CODEC    (1<<1)
#define HAS_BEEN_RESET          (1<<2)

gint max_buffer_count = 20;

GST_START_TEST (test_rtpconference_new)
{
  struct SimpleTestConference *dat = NULL;
  struct SimpleTestStream *st = NULL;
  guint id = 999;
  GList *local_codecs = NULL;
  FsMediaType *media_type;
  GstPad *sinkpad = NULL;
  gchar *str = NULL;
  GstElement *conf = NULL;
  FsSession *sess = NULL;
  FsParticipant *part = NULL;
  FsStreamTransmitter *stt = NULL;
  FsStreamDirection dir;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");
  st = simple_conference_add_stream (dat, dat);

  g_object_get (dat->conference, "sdes-cname", &str, NULL);
  ts_fail_unless (!strcmp (str, "bob@127.0.0.1"), "Conference CNAME is wrong");
  g_free (str);

  g_object_get (st->participant, "cname", &str, NULL);
  ts_fail_unless (!strcmp (str, "bob@127.0.0.1"), "Participant CNAME is wrong");
  g_free (str);

  g_object_get (dat->session,
      "id", &id,
      "local-codecs", &local_codecs,
      "media-type", &media_type,
      "sink-pad", &sinkpad,
      "conference", &conf,
      NULL);

  ts_fail_unless (id == 1, "The id of the first session should be 1 not %d",
      id);
  ts_fail_if (local_codecs == NULL, "Local codecs should not be NULL");
  fs_codec_list_destroy (local_codecs);
  ts_fail_unless (media_type == FS_MEDIA_TYPE_AUDIO, "Media type isnt audio,"
      " its %d", media_type);
  ts_fail_if (sinkpad == NULL, "Sink pad should not be null");
  str = g_strdup_printf ("sink_%d", id);
  ts_fail_unless (!strcmp (str, GST_OBJECT_NAME (sinkpad)), "Sink pad is %s"
      " instead of being %d", GST_OBJECT_NAME (sinkpad), str);
  gst_object_unref (sinkpad);
  g_free (str);
  ts_fail_unless (conf == dat->conference, "Conference pointer from the session"
      " is wrong");
  gst_object_unref (conf);


  g_object_get (st->stream,
      "participant", &part,
      "session", &sess,
      "stream-transmitter", &stt,
      "direction", &dir,
      NULL);
  ts_fail_unless (part == st->participant, "The stream does not have the right"
      " participant");
  g_object_unref (part);
  ts_fail_unless (sess == dat->session, "The stream does not have the right"
      " session");
  g_object_unref (sess);
  ts_fail_unless (FS_IS_STREAM_TRANSMITTER (stt), "The stream transmitter is not"
      " a stream transmitter");
  g_object_unref (stt);
  ts_fail_unless (dir == FS_DIRECTION_BOTH, "The direction is not both");

  g_object_set (st->stream, "direction", FS_DIRECTION_NONE, NULL);
  g_object_get (st->stream, "direction", &dir, NULL);
  ts_fail_unless (dir == FS_DIRECTION_NONE, "The direction is not both");

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

        ts_fail ("Error on BUS (%d) %s .. %s", errno,
            g_value_get_string (errorvalue),
            g_value_get_string (debugvalue));
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
  ts_fail_if (codec == NULL, "Could not get new send codec");

  str = fs_codec_to_string (codec);
  g_debug ("%d: New send codec: %s", dat->id, str);
  g_free (str);

  fs_codec_destroy (codec);
}

static void
_handoff_handler (GstElement *element, GstBuffer *buffer, GstPad *pad,
  gpointer user_data)
{
  struct SimpleTestStream *st = user_data;
  int i;
  gboolean stop = TRUE;
  GList *negotiated_codecs = NULL;

  g_object_get (st->dat->session,
      "negotiated-codecs", &negotiated_codecs,
      NULL);

  ts_fail_if (negotiated_codecs == NULL, "Could not get negotiated codecs");

  if (st->flags & WAITING_ON_LAST_CODEC)
  {
    if (fs_codec_are_equal (
        g_list_last (negotiated_codecs)->data,
        g_object_get_data (G_OBJECT (element), "codec")))
    {
      st->flags &= ~WAITING_ON_LAST_CODEC;
      st->flags |= SHOULD_BE_LAST_CODEC;
      max_buffer_count += st->buffer_count;
      g_debug ("We HAVE last codec");
    }
    else
    {
      gchar *str = fs_codec_to_string (
          g_object_get_data (G_OBJECT (element), "codec"));
      gchar *str2 = fs_codec_to_string (g_list_last (negotiated_codecs)->data);
      g_debug ("not yet the last codec, skipping (we have %s, we want %s)",
          str, str2);
      g_free (str);
      g_free (str2);
      fs_codec_list_destroy (negotiated_codecs);
      return;
    }
  }


  if (select_last_codec || st->flags & SHOULD_BE_LAST_CODEC)
    ts_fail_unless (
        fs_codec_are_equal (
            g_list_last (negotiated_codecs)->data,
            g_object_get_data (G_OBJECT (element), "codec")),
        "The handoff handler got a buffer from the wrong codec");
  else
    ts_fail_unless (
        fs_codec_are_equal (
            g_list_first (negotiated_codecs)->data,
            g_object_get_data (G_OBJECT (element), "codec")),
        "The handoff handler got a buffer from the wrong codec");

  fs_codec_list_destroy (negotiated_codecs);


  st->buffer_count++;

  if (st->buffer_count % 10 == 0)
    g_debug ("%d:%d: Buffer %d", st->dat->id, st->target->id, st->buffer_count);

  /*
  ts_fail_if (dat->buffer_count > max_buffer_count,
    "Too many buffers %d > max_buffer_count", dat->buffer_count);
  */

  for (i = 0; i < count && !stop ; i++)
  {
    GList *item;


    for (item = g_list_first (dats[i]->streams);
         item;
         item = g_list_next (item))
    {
      struct SimpleTestStream *st2 = item->data;

      if (st2->buffer_count < max_buffer_count)
      {
        stop = FALSE;
        break;
      }
    }
  }

  if (stop)
  {
    if (reset_to_last_codec && !(st->flags & HAS_BEEN_RESET)) {
      GError *error = NULL;
      GList *nego_codecs = NULL;
      gchar *str = NULL;

      g_object_get (st->target->session,
          "negotiated-codecs", &nego_codecs,
          NULL);

      ts_fail_if (nego_codecs == NULL, "No negotiated codecs ??");
      ts_fail_if (g_list_length (nego_codecs) < 2, "Only one negotiated codec");

      str = fs_codec_to_string (g_list_last (nego_codecs)->data);
      g_debug ("Setting codec to: %s", str);
      g_free (str);

      ts_fail_unless (fs_session_set_send_codec (st->target->session,
              g_list_last (nego_codecs)->data, &error),
          "Could not set the send codec: %s",
          error ? error->message : "NO GError!!!");
      g_clear_error (&error);

      fs_codec_list_destroy (nego_codecs);

      st->flags |= HAS_BEEN_RESET | WAITING_ON_LAST_CODEC;

      g_debug ("RESET TO LAST CODEC");

    } else {
      g_main_loop_quit (loop);
    }
  }
}

static void
_src_pad_added (FsStream *self, GstPad *pad, FsCodec *codec, gpointer user_data)
{
  struct SimpleTestStream *st = user_data;
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);
  GstPad *fakesink_pad = NULL;
  GstPadLinkReturn ret;
  FsCodec *codeccopy = fs_codec_copy (codec);
  gchar *str = NULL;

  g_assert (fakesink);

  g_object_set (fakesink,
      "signal-handoffs", TRUE,
      "sync", TRUE,
      "async", TRUE,
      NULL);

  g_object_set_data (G_OBJECT (fakesink), "codec", codeccopy);
  g_object_weak_ref (G_OBJECT (fakesink),
      (GWeakNotify) fs_codec_destroy, codeccopy);

  g_signal_connect (fakesink, "handoff", G_CALLBACK (_handoff_handler), st);

  gst_bin_add (GST_BIN (st->dat->pipeline), fakesink);

  fakesink_pad = gst_element_get_static_pad (fakesink, "sink");
  ret = gst_pad_link (pad, fakesink_pad);
  gst_object_unref (fakesink_pad);

  ts_fail_if (GST_PAD_LINK_FAILED(ret), "Could not link fakesink");

  ts_fail_if (gst_element_set_state (fakesink, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE, "Could not set the fakesink to playing");

  str = fs_codec_to_string (codec);
  g_debug ("%d:%d: Added Fakesink for codec %s", st->dat->id, st->target->id,
           str);
  g_free (str);
}


static void
_new_active_candidate_pair (FsStream *stream, FsCandidate *local,
    FsCandidate *remote, gpointer user_data)
{
  struct SimpleTestStream *st = user_data;

  ts_fail_if (local == NULL, "Local candidate NULL");
  ts_fail_if (remote == NULL, "Remote candidate NULL");

  if (local->component_id != 1)
    return;

  if (!st->dat->fakesrc)
    setup_fakesrc (st->dat);
}


static void
rtpconference_simple_connect_signals (struct SimpleTestConference *dat)
{
  GstBus *bus = NULL;

  bus = gst_element_get_bus (dat->pipeline);
  gst_bus_add_watch (bus, _simple_bus_callback, dat);
  gst_object_unref (bus);

  g_signal_connect (dat->session, "send-codec-changed",
      G_CALLBACK (_simple_send_codec_changed), dat);
}

static void
rtpconference_simple_connect_streams_signals (struct SimpleTestStream *st)
{
 g_signal_connect (st->stream, "src-pad-added", G_CALLBACK (_src_pad_added),
      st);

  g_signal_connect (st->stream, "new-active-candidate-pair",
      G_CALLBACK (_new_active_candidate_pair), st);
}


static gboolean
_start_pipeline (gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;

  g_debug ("%d: Starting pipeline", dat->id);

  ts_fail_if (gst_element_set_state (dat->pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  dat->started = TRUE;

  return FALSE;
}

static struct SimpleTestStream *
find_pointback_stream (
    struct SimpleTestConference *dat,
    struct SimpleTestConference *target)
{
  GList *item = NULL;

  for (item = g_list_first (dat->streams);
       item;
       item = g_list_next (item))
  {
    struct SimpleTestStream *st = item->data;

    if (st->target == target)
      return st;
  }

  ts_fail ("We did not find a return stream for %d in %d", target->id, dat->id);
  return NULL;
}


static gboolean
_compare_codec_lists (GList *list1, GList *list2)
{
  for (; list1 && list2;
       list1 = g_list_next (list1),
       list2 = g_list_next (list2)) {
    if (!fs_codec_are_equal (list1->data, list2->data))
      return FALSE;
  }

  if (list1 == NULL && list2 == NULL)
    return TRUE;
  else
    return FALSE;
}

static void
_new_negotiated_codecs (FsSession *session, gpointer user_data)
{
  struct SimpleTestConference *dat = user_data;
  GList *codecs = NULL;
  GError *error = NULL;
  GList *item = NULL;

  g_debug ("%d: New negotiated codecs", dat->id);

  ts_fail_if (session != dat->session, "Got signal from the wrong object");

  g_object_get (dat->session, "negotiated-codecs", &codecs, NULL);
  ts_fail_if (codecs == NULL, "Could not get the negotiated codecs");


  /* We have to find the stream from the target that points back to us */
  for (item = g_list_first (dat->streams); item; item = g_list_next (item))
  {
    struct SimpleTestStream *st = item->data;
    struct SimpleTestStream *st2 = find_pointback_stream (st->target, dat);
    GList *rcodecs2;

    g_debug ("Setting negotiated remote codecs on %d:%d from %d",st2->dat->id,
        st2->target->id, dat->id);
    if (!fs_stream_set_remote_codecs (st2->stream, codecs, &error))
    {
      if (error)
        ts_fail ("Could not set the remote codecs on stream %d:%d (%d): %s",
            st2->dat->id, st2->target->id,
            error->code,
            error->message);
      else
        ts_fail ("Could not set the remote codecs on stream %d:%d"
            " and we DID not get a GError!!",
            st2->dat->id, st2->target->id);
    }
    g_object_get (st2->stream, "remote-codecs", &rcodecs2, NULL);
    ts_fail_unless (_compare_codec_lists (rcodecs2, codecs),
        "Can not get remote codecs correctly");

    fs_codec_list_destroy (rcodecs2);

    if (select_last_codec)
      ts_fail_unless (
          fs_session_set_send_codec (st2->dat->session,
              g_list_last (codecs)->data,
              &error),
          "Error setting the send codec to the last codec: %s",
          error ? error->message : "No GError");

    g_clear_error (&error);
    break;
  }
  fs_codec_list_destroy (codecs);
}


static void
_new_local_candidate (FsStream *stream, FsCandidate *candidate,
    gpointer user_data)
{
  struct SimpleTestStream *st = user_data;
  gboolean ret;
  GError *error = NULL;
  struct SimpleTestStream *other_st = find_pointback_stream (st->target,
      st->dat);

  g_debug ("%d:%d: Setting remote candidate for component %d",
      other_st->dat->id,
      other_st->target->id,
      candidate->component_id);

  ret = fs_stream_add_remote_candidate (other_st->stream, candidate, &error);

  if (error)
    ts_fail ("Error while adding candidate: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);

  ts_fail_unless(ret == TRUE, "No detailed error from add_remote_candidate");

}

static void
set_initial_codecs (
    struct SimpleTestConference *from,
    struct SimpleTestStream *to)
{
  GList *local_codecs = NULL;
  GList *filtered_codecs = NULL;
  GList *item = NULL;
  GList *rcodecs2 = NULL;
  GError *error = NULL;

  g_object_get (from->session, "local-codecs", &local_codecs, NULL);

  ts_fail_if (local_codecs == NULL, "Could not get the local codecs");

  for (item = g_list_first (local_codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == 0 || codec->id == 8)
      filtered_codecs = g_list_append (filtered_codecs, codec);
  }

  ts_fail_if (filtered_codecs == NULL, "PCMA and PCMU are not in the codecs"
      " you must install gst-plugins-good");


  g_debug ("Setting initial remote codecs on %d:%d from %d",
      to->dat->id, to->target->id,
      from->id);

  if (!fs_stream_set_remote_codecs (to->stream, filtered_codecs, &error))
  {
    if (error)
      ts_fail ("Could not set the remote codecs on stream %d:%d (%d): %s",
          to->dat->id, to->target->id,
          error->code,
          error->message);
    else
      ts_fail ("Could not set the remote codecs on stream %d"
          " and we DID not get a GError!!", to->target->id);
  }
  g_object_get (to->stream, "remote-codecs", &rcodecs2, NULL);
  ts_fail_unless (_compare_codec_lists (rcodecs2, filtered_codecs),
      "Can not get remote codecs correctly");
  fs_codec_list_destroy (rcodecs2);


  if (select_last_codec)
    ts_fail_unless (
        fs_session_set_send_codec (to->dat->session,
            g_list_last (filtered_codecs)->data,
            &error),
        "Error setting the send codec to the last codec: %s",
        error ? error->message : "No GError");
  g_clear_error (&error);

  g_list_free (filtered_codecs);
  fs_codec_list_destroy (local_codecs);
}


static void
simple_test (int in_count)
{
  int i, j;

  count = in_count;

  loop = g_main_loop_new (NULL, FALSE);

  dats = g_new0 (struct SimpleTestConference *, count);

  for (i = 0; i < count; i++)
  {
    gchar *tmp = g_strdup_printf ("tester%d@TesterTop3", i);
    dats[i] = setup_simple_conference (i, "fsrtpconference", tmp);
    g_free (tmp);

    rtpconference_simple_connect_signals (dats[i]);
    g_idle_add (_start_pipeline, dats[i]);

    if (i != 0)
      g_signal_connect (dats[i]->session, "new-negotiated-codecs",
          G_CALLBACK (_new_negotiated_codecs), dats[i]);
  }

  for (i = 0; i < count; i++)
    for (j = 0; j < count; j++)
      if (i != j)
      {
        struct SimpleTestStream *st = NULL;

        st = simple_conference_add_stream (dats[i], dats[j]);
        rtpconference_simple_connect_streams_signals (st);

        g_signal_connect (st->stream, "new-local-candidate",
            G_CALLBACK (_new_local_candidate), st);
      }

  for (i = 1; i < count; i++)
  {
    struct SimpleTestStream *st = find_pointback_stream (dats[i], dats[0]);
    set_initial_codecs (dats[0], st);
  }

  g_main_loop_run (loop);

  for (i = 0; i < count; i++)
    gst_element_set_state (dats[i]->pipeline, GST_STATE_NULL);

  for (i = 0; i < count; i++)
    cleanup_simple_conference (dats[i]);

  g_free (dats);

  g_main_loop_unref (loop);
}


GST_START_TEST (test_rtpconference_two_way)
{
  simple_test (2);
}
GST_END_TEST;


GST_START_TEST (test_rtpconference_three_way)
{
  simple_test (3);
}
GST_END_TEST;


GST_START_TEST (test_rtpconference_ten_way)
{
  simple_test (10);
}
GST_END_TEST;


GST_START_TEST (test_rtpconference_errors)
{
  struct SimpleTestConference *dat = NULL;
  FsParticipant *participant = NULL;
  FsStream *stream = NULL;
  GError *error = NULL;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");

  participant = fs_conference_new_participant (FS_CONFERENCE (dat->conference),
      "bob2@127.0.0.1",
      NULL);
  ts_fail_if (participant == NULL, "Could not create participant");

  stream = fs_session_new_stream (dat->session, participant, FS_DIRECTION_NONE,
      "invalid-transmitter-name", 0, NULL, &error);

  ts_fail_unless (stream == NULL, "A stream was created with an invalid"
      " transmitter name");
  ts_fail_if (error == NULL, "Error was not set");
  ts_fail_unless (error->domain == FS_ERROR &&
      error->code == FS_ERROR_CONSTRUCTION,
      "The wrong domain or code (%d) was returned", error->code);

  g_object_unref (participant);

  cleanup_simple_conference (dat);

}
GST_END_TEST;


GST_START_TEST (test_rtpconference_select_send_codec)
{
  select_last_codec = TRUE;
  simple_test (2);
}
GST_END_TEST;


GST_START_TEST (test_rtpconference_select_send_codec_while_running)
{
  reset_to_last_codec = TRUE;
  simple_test (2);
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
  tcase_add_test (tc_chain, test_rtpconference_new);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpconfence_two_way");
  tcase_add_test (tc_chain, test_rtpconference_two_way);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpconfence_three_way");
  tcase_add_test (tc_chain, test_rtpconference_three_way);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpconfence_ten_way");
  tcase_add_test (tc_chain, test_rtpconference_ten_way);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpconfence_errors");
  tcase_add_test (tc_chain, test_rtpconference_errors);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpconfence_select_send_codec");
  tcase_add_test (tc_chain, test_rtpconference_select_send_codec);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpconfence_select_send_codec_while_running");
  tcase_add_test (tc_chain, test_rtpconference_select_send_codec_while_running);
  suite_add_tcase (s, tc_chain);
  return s;
}


GST_CHECK_MAIN (fsrtpconference);

/* Farsight 2 unit tests for FsMsnConference
 *
 * Copyright (C) 2009 Collabora
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

#include "check-threadsafe.h"

GMainLoop *loop;
int count = 0;

#define WAITING_ON_LAST_CODEC   (1<<0)
#define SHOULD_BE_LAST_CODEC    (1<<1)
#define HAS_BEEN_RESET          (1<<2)

gint max_buffer_count = 20;


struct SimpleMsnConference {
  GstElement *pipeline;
  FsConference *conf;
  FsSession *session;
  FsParticipant *part;
  FsStream *stream;

  struct SimpleMsnConference *target;
  FsStreamDirection direction;
};

static gboolean
bus_watch (GstBus *bus, GstMessage *message, gpointer user_data)
{
  struct SimpleMsnConference *dat = user_data;

  switch (GST_MESSAGE_TYPE (message))
  {
    case GST_MESSAGE_ELEMENT:
      {
        const GstStructure *s = gst_message_get_structure (message);
        ts_fail_if (s==NULL, "NULL structure in element message");
        if (gst_structure_has_name (s, "farsight-error"))
        {
          const GValue *value;
          FsError errorno;
          const gchar *error, *debug;

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

          ts_fail ("Error on BUS (%d) %s .. %s", errorno, error, debug);
        }
        else if (gst_structure_has_name (s, "farsight-new-local-candidate"))
        {
          FsStream *stream;
          FsCandidate *candidate;
          const GValue *value;

          ts_fail_unless (
              gst_implements_interface_check (GST_MESSAGE_SRC (message),
                  FS_TYPE_CONFERENCE),
              "Received farsight-error from non-farsight element");

          ts_fail_unless (
              gst_structure_has_field_typed (s, "stream", FS_TYPE_STREAM),
              "farsight-new-local-candidate structure has no stream field");
          ts_fail_unless (
              gst_structure_has_field_typed (s, "candidate", FS_TYPE_CANDIDATE),
              "farsight-new-local-candidate structure has no candidate field");

          value = gst_structure_get_value (s, "stream");
          stream = g_value_get_object (value);

          value = gst_structure_get_value (s, "candidate");
          candidate = g_value_get_boxed (value);

          ts_fail_unless (stream && candidate, "new-local-candidate with NULL"
              " stream(%p) or candidate(%p)", stream, candidate);

          if (dat->target)
          {
            GError *error = NULL;
            GList *list = g_list_append (NULL, candidate);

            g_debug ("Setting candidate: %s %d",
                candidate->ip, candidate->port);
            ts_fail_unless (fs_stream_set_remote_candidates (
                    dat->target->stream, list, &error),
                "Could not set remote candidate: %s",
                error ? error->message : "No GError");
            ts_fail_unless (error == NULL);
            g_list_free (list);
          }
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

        g_debug ("%d: Got a warning on the BUS: %s (%s)",
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
pad_probe_cb (GstPad *pad, GstBuffer *buf, gpointer user_data)
{
  count++;

  if (count > 20)
    g_main_loop_quit (loop);
}

static void
stream_src_pad_added (FsStream *stream, GstPad *pad, FsCodec *codec,
    struct SimpleMsnConference *dat)
{
  GstElement *sink = gst_element_factory_make ("fakesink", NULL);
  GstPad *sinkpad;

  g_debug ("pad added");

  ts_fail_unless (sink != NULL);

  ts_fail_unless (gst_bin_add (GST_BIN (dat->pipeline), sink));

  sinkpad = gst_element_get_static_pad (sink, "sink");
  ts_fail_unless (sinkpad != NULL);

  gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (pad_probe_cb), dat);

  ts_fail_if (GST_PAD_LINK_FAILED (gst_pad_link (pad, sinkpad)));

  gst_object_unref (sinkpad);

  ts_fail_if (gst_element_set_state (sink, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

}

struct SimpleMsnConference *
setup_conference (FsStreamDirection dir, struct SimpleMsnConference *target)
{
  struct SimpleMsnConference *dat = g_new0 (struct SimpleMsnConference, 1);
  GError *error = NULL;
  GstBus *bus;
  GParameter param = {NULL, {0}};
  gint n_params = 0;
  guint tos;

  dat->target = target;
  dat->direction = dir;

  dat->pipeline = gst_pipeline_new (NULL);

  bus = gst_element_get_bus (dat->pipeline);
  gst_bus_add_watch (bus, bus_watch, dat);
  gst_object_unref (bus);

  if (dir == FS_DIRECTION_SEND)
    dat->conf = FS_CONFERENCE (
        gst_element_factory_make ("fsmsncamsendconference", NULL));
  else
    dat->conf = FS_CONFERENCE (
        gst_element_factory_make ("fsmsncamrecvconference", NULL));
  ts_fail_unless (dat->conf != NULL);

  ts_fail_unless (gst_bin_add (GST_BIN (dat->pipeline),
          GST_ELEMENT (dat->conf)));

  dat->part = fs_conference_new_participant (dat->conf, "", &error);
  ts_fail_unless (error == NULL, "Error: %s", error ? error->message: "");
  ts_fail_unless (dat->part != NULL);

  dat->session = fs_conference_new_session (dat->conf, FS_MEDIA_TYPE_VIDEO,
      &error);
  ts_fail_unless (dat->session != NULL, "Session create error: %s:",
      error ? error->message : "No GError");
  ts_fail_unless (error == NULL);

  g_object_set (dat->session, "tos", 2, NULL);
  g_object_get (dat->session, "tos", &tos, NULL);
  ts_fail_unless (tos == 2);

  if (dir == FS_DIRECTION_SEND)
  {
    GstPad *sinkpad, *srcpad;
    GstElement *src;
    src = gst_element_factory_make ("videotestsrc", NULL);
    ts_fail_unless (src != NULL);
    g_object_set (src, "is-live", TRUE, NULL);
    ts_fail_unless (gst_bin_add (GST_BIN (dat->pipeline),
            GST_ELEMENT (src)));

    g_object_get (dat->session, "sink-pad", &sinkpad, NULL);
    ts_fail_if (sinkpad == NULL);
    srcpad = gst_element_get_static_pad (src, "src");
    ts_fail_if (srcpad == NULL);

    ts_fail_if (GST_PAD_LINK_FAILED (gst_pad_link ( srcpad, sinkpad)));
    gst_object_unref (srcpad);
    gst_object_unref (sinkpad);
  }

  if (target)
  {
    guint session_id = 0;
    n_params = 1;
    g_object_get (target->stream, "session-id", &session_id, NULL);
    ts_fail_unless (session_id >= 9000 && session_id < 10000);
    param.name = "session-id";
    g_value_init (&param.value, G_TYPE_UINT);
    g_value_set_uint (&param.value, session_id);
  }

  dat->stream = fs_session_new_stream (dat->session, dat->part, dir, NULL,
      n_params, &param, &error);
  ts_fail_unless (dat->stream != NULL);
  ts_fail_unless (error == NULL);

  g_signal_connect (dat->stream, "src-pad-added",
      G_CALLBACK (stream_src_pad_added), dat);

  ts_fail_if (gst_element_set_state (dat->pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  return dat;
}

static void
free_conference (struct SimpleMsnConference *dat)
{
  ts_fail_if (gst_element_set_state (dat->pipeline, GST_STATE_NULL) ==
      GST_STATE_CHANGE_FAILURE);

  gst_object_unref (dat->stream);
  gst_object_unref (dat->session);
  gst_object_unref (dat->part);
  gst_object_unref (dat->pipeline);

  free (dat);
}


GST_START_TEST (test_msnconference_new)
{
  struct SimpleMsnConference *senddat = setup_conference (FS_DIRECTION_SEND,
      NULL);
  struct SimpleMsnConference *recvdat = setup_conference (FS_DIRECTION_RECV,
      NULL);


  free_conference (senddat);
  free_conference (recvdat);
}
GST_END_TEST;



GST_START_TEST (test_msnconference_send_to_recv)
{
  struct SimpleMsnConference *senddat = setup_conference (FS_DIRECTION_SEND,
      NULL);
  struct SimpleMsnConference *recvdat = setup_conference (FS_DIRECTION_RECV,
      senddat);

  loop = g_main_loop_new (NULL, FALSE);

  g_main_loop_run (loop);

  free_conference (senddat);
  free_conference (recvdat);
  g_main_loop_unref (loop);
}
GST_END_TEST;


GST_START_TEST (test_msnconference_recv_to_send)
{
  struct SimpleMsnConference *recvdat = setup_conference (FS_DIRECTION_RECV,
      NULL);
  struct SimpleMsnConference *senddat = setup_conference (FS_DIRECTION_SEND,
      recvdat);

  loop = g_main_loop_new (NULL, FALSE);

  g_main_loop_run (loop);

  free_conference (senddat);
  free_conference (recvdat);
  g_main_loop_unref (loop);
}
GST_END_TEST;


GST_START_TEST (test_msnconference_error)
{
  struct SimpleMsnConference *dat = setup_conference (FS_DIRECTION_SEND,
      NULL);
  GError *error = NULL;

  ts_fail_unless (
      fs_conference_new_participant (dat->conf, "", &error) == NULL);
  ts_fail_unless (error->domain == FS_ERROR &&
      error->code == FS_ERROR_ALREADY_EXISTS);
  g_clear_error (&error);


  ts_fail_unless (
      fs_conference_new_session (dat->conf, FS_MEDIA_TYPE_VIDEO, &error) == NULL);
  ts_fail_unless (error->domain == FS_ERROR &&
      error->code == FS_ERROR_ALREADY_EXISTS);
  g_clear_error (&error);


  ts_fail_unless (
      fs_session_new_stream (dat->session, dat->part, FS_DIRECTION_SEND,
          NULL, 0, NULL, &error) == NULL);
  ts_fail_unless (error->domain == FS_ERROR &&
      error->code == FS_ERROR_ALREADY_EXISTS);
  g_clear_error (&error);


  free_conference (dat);
}
GST_END_TEST;

static Suite *
fsmsnconference_suite (void)
{
  Suite *s = suite_create ("fsmsnconference");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);

  tc_chain = tcase_create ("fsmsnconference_new");
  tcase_add_test (tc_chain, test_msnconference_new);
  suite_add_tcase (s, tc_chain);


  tc_chain = tcase_create ("fsmsnconference_send_to_recv");
  tcase_add_test (tc_chain, test_msnconference_send_to_recv);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsmsnconference_recv_to_send");
  tcase_add_test (tc_chain, test_msnconference_recv_to_send);
  suite_add_tcase (s, tc_chain);


  tc_chain = tcase_create ("fsmsnconference_error");
  tcase_add_test (tc_chain, test_msnconference_error);
  suite_add_tcase (s, tc_chain);


  return s;
}

GST_CHECK_MAIN (fsmsnconference);

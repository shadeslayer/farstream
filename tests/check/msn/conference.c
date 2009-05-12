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

// Options
gboolean select_last_codec = FALSE;
gboolean reset_to_last_codec = FALSE;
gboolean no_rtcp = FALSE;

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
  //struct SimpleMsnConference *dat = user_data;

  return TRUE;
}

struct SimpleMsnConference *
setup_conference (FsStreamDirection dir, struct SimpleMsnConference *target)
{
  struct SimpleMsnConference *dat = g_new0 (struct SimpleMsnConference, 1);
  GError *error = NULL;
  GstBus *bus;

  dat->target = target;
  dat->direction = dir;

  dat->pipeline = gst_pipeline_new (NULL);
  dat->conf = FS_CONFERENCE (
      gst_element_factory_make ("fsmsnconference", NULL));
  ts_fail_unless (dat->conf != NULL);

  bus = gst_element_get_bus (dat->pipeline);
  gst_bus_add_watch (bus, bus_watch, dat);
  gst_object_unref (bus);

  ts_fail_unless (gst_bin_add (GST_BIN (dat->pipeline),
          GST_ELEMENT (dat->conf)));

  dat->part = fs_conference_new_participant (dat->conf, "", &error);
  ts_fail_unless (dat->part != NULL);
  ts_fail_unless (error == NULL);

  dat->session = fs_conference_new_session (dat->conf, dir, &error);
  ts_fail_unless (dat->session != NULL);
  ts_fail_unless (error == NULL);

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

    g_object_get (target->session, "session-id", &session_id, NULL);
    ts_fail_unless (session_id >= 9000 && session_id < 10000);
    g_object_get (dat->session, "session-id", session_id, NULL);
  }

  dat->stream = fs_session_new_stream (dat->session, dat->part, dir, NULL, 0,
      NULL, &error);
  ts_fail_unless (dat->stream != NULL);
  ts_fail_unless (error == NULL);

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
  struct SimpleMsnConference *recvdat = setup_conference (FS_DIRECTION_SEND,
      NULL);

  ts_fail_if (gst_element_set_state (senddat->pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);
  ts_fail_if (gst_element_set_state (recvdat->pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  free_conference (senddat);
  free_conference (recvdat);
}
GST_END_TEST;


  /*

GST_START_TEST (test_msnconference_error)
{

  ts_fail_unless (
      fs_conference_new_participant (dat->conf, "", &error) == NULL);
  ts_fail_unless (error->domain == FS_ERROR &&
      error->code == FS_ERROR_ALREADY_EXISTS);
  g_clear_error (&error);


}
GST_END_TEST;

  */

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

  return s;
}


GST_CHECK_MAIN (fsmsnconference);

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

GST_START_TEST (test_rtpconference_new)
{
  GstElement *pipeline = gst_pipeline_new ("pipeline");
  GstElement *conference = NULL;
  FsSession *session;
  FsParticipant *participant;
  FsStream *stream;
  GError *error = NULL;

  conference = gst_element_factory_make ("fsrtpconference", NULL);
  fail_if (conference == NULL, "Could not buld fsrtpconference");
  fail_unless (gst_bin_add (GST_BIN (pipeline), conference),
      "Could not add conference to the pipeline");

  session = fs_conference_new_session (FS_CONFERENCE (conference),
      FS_MEDIA_TYPE_AUDIO, &error);
  if (error)
    fail ("Error while creating new session (%d): %s",
        error->code, error->message);
  fail_if (session == NULL, "Could not make session, but no GError!");

  participant = fs_conference_new_participant (FS_CONFERENCE (conference),
      "bob@127.0.0.1", &error);
  if (error)
    fail ("Error while creating new participant (%d): %s",
        error->code, error->message);
  fail_if (session == NULL, "Could not make participant, but no GError!");

  stream = fs_session_new_stream (session, participant, FS_DIRECTION_NONE,
      "rawudp", 0, NULL, &error);
  if (error)
    fail ("Error while creating new stream (%d): %s",
        error->code, error->message);
  fail_if (session == NULL, "Could not make stream, but no GError!");

  g_object_unref (stream);
  g_object_unref (session);
  g_object_unref (participant);
  gst_object_unref (pipeline);
}
GST_END_TEST;



static Suite *
rawudptransmitter_suite (void)
{
  Suite *s = suite_create ("fsrtpconference");
  TCase *tc_chain;


  tc_chain = tcase_create ("fsrtpconfence_base");
  tcase_set_timeout (tc_chain, 5);
  tcase_add_test (tc_chain, test_rtpconference_new);
  suite_add_tcase (s, tc_chain);


  return s;
}


GST_CHECK_MAIN (rawudptransmitter);

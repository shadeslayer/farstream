/* Farsigh2 unit tests for FsRawUdpTransmitter
 *
 * Copyright (C) 2007 Collabora, Nokia
 * @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/farsight/fs-transmitter.h>

#include "generic.h"

gint buffer_count[2] = {0, 0};
GMainLoop *loop = NULL;
gint candidates[2] = {0, 0};
GstElement *pipeline = NULL;
gboolean src_setup[2] = {FALSE, FALSE};


GST_START_TEST (test_rawudptransmitter_new)
{
  GError *error = NULL;
  FsTransmitter *trans;
  GstElement *pipeline;
  GstElement *trans_sink, *trans_src;

  trans = fs_transmitter_new ("rawudp", &error);

  if (error) {
    fail("Error creating transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans, NULL);

  g_object_get (trans, "gst-sink", &trans_sink, "gst-src", &trans_src, NULL);

  g_object_unref (trans);

  gst_object_unref (pipeline);

}
GST_END_TEST;

static void
_new_local_candidate (FsStreamTransmitter *st, FsCandidate *candidate,
  gpointer user_data)
{
  gboolean has_stun = GPOINTER_TO_INT (user_data);
  GError *error = NULL;
  gboolean ret;

  fail_if (candidate == NULL, "Passed NULL candidate");
  fail_unless (candidate->ip != NULL, "Null IP in candidate");
  fail_if (candidate->port == 0, "Candidate has port 0");
  fail_unless (candidate->proto == FS_NETWORK_PROTOCOL_UDP,
    "Protocol is not UDP");

  if (candidate->component_id == FS_COMPONENT_RTP)
    fail_unless (strcmp (candidate->proto_subtype, "RTP") == 0,
      "Proto subtype %s does not match component %d", candidate->proto_subtype,
      candidate->component_id);
  else if (candidate->component_id == FS_COMPONENT_RTCP)
    fail_unless (strcmp (candidate->proto_subtype, "RTCP") == 0,
      "Proto subtype %s does not match component %d", candidate->proto_subtype,
      candidate->component_id);
  else
    fail ("Invalid component %d", candidate->component_id);

  if (has_stun)
    fail_unless (candidate->type == FS_CANDIDATE_TYPE_SRFLX,
      "Has stun, but candidate is not server reflexive,"
      " it is: %s:%u of type %d on component %u",
      candidate->ip, candidate->port, candidate->type, candidate->component_id);
  else
    fail_unless (candidate->type == FS_CANDIDATE_TYPE_HOST,
      "Does not have stun, but candidate is not host");

  candidates[candidate->component_id-1] = 1;

  g_debug ("New local candidate %s:%d of type %d for component %d",
    candidate->ip, candidate->port, candidate->type, candidate->component_id);

  ret = fs_stream_transmitter_add_remote_candidate (st, candidate, &error);

  if (error)
    fail ("Error while adding candidate: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);

  fail_unless(ret == TRUE, "No detailed error from add_remote_candidate");

}

static void
_local_candidates_prepared (FsStreamTransmitter *st, gpointer user_data)
{
  gboolean has_stun = GPOINTER_TO_INT (user_data);

  fail_if (candidates[0] == 0, "candidates-prepared with no RTP candidate");
  fail_if (candidates[1] == 0, "candidates-prepared with no RTCP candidate");

  g_debug ("Local Candidates Prepared");

  /*
   * This doesn't work on my router
   */

  if (has_stun)
    g_main_loop_quit (loop);
}


static void
_new_active_candidate_pair (FsStreamTransmitter *st, FsCandidate *local,
  FsCandidate *remote, gpointer user_data)
{
  fail_if (local == NULL, "Local candidate NULL");
  fail_if (remote == NULL, "Remote candidate NULL");

  fail_unless (local->component_id == remote->component_id,
    "Local and remote candidates dont have the same component id");

  g_debug ("New active candidate pair for component %d", local->component_id);

  if (!src_setup[local->component_id-1])
    setup_fakesrc (user_data, pipeline, local->component_id);
  src_setup[local->component_id-1] = TRUE;
}

static gboolean
_start_pipeline (gpointer user_data)
{
  GstElement *pipeline = user_data;

  g_debug ("Starting pipeline");

  fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  return FALSE;
}

static void
_handoff_handler (GstElement *element, GstBuffer *buffer, GstPad *pad,
  gpointer user_data)
{
  gint component_id = GPOINTER_TO_INT (user_data);

  fail_unless (GST_BUFFER_SIZE (buffer) == component_id * 10,
    "Buffer is size %d but component_id is %d", GST_BUFFER_SIZE (buffer),
    component_id);

  buffer_count[component_id-1]++;

  /*
  g_debug ("Buffer %d component: %d size: %u", buffer_count[component_id-1],
    component_id, GST_BUFFER_SIZE (buffer));
  */

  fail_if (buffer_count[component_id-1] > 20,
    "Too many buffers %d > 20 for component",
    buffer_count[component_id-1], component_id);

  if (buffer_count[0] == 20 && buffer_count[1] == 20) {
    /* TEST OVER */
    g_main_loop_quit (loop);
  }
}


static void
run_rawudp_transmitter_test (gint n_parameters, GParameter *params,
  gboolean has_stun)
{
  GError *error = NULL;
  FsTransmitter *trans;
  FsStreamTransmitter *st;
  GstElement *trans_sink, *trans_src;

  loop = g_main_loop_new (NULL, FALSE);
  trans = fs_transmitter_new ("rawudp", &error);

  if (error) {
    fail("Error creating transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans, G_CALLBACK (_handoff_handler));

  g_object_get (trans, "gst-sink", &trans_sink, "gst-src", &trans_src, NULL);

  st = fs_transmitter_new_stream_transmitter (trans, NULL, n_parameters, params,
    &error);

  if (error) {
    fail("Error creating stream transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  fail_if (st == NULL, "No stream transmitter created, yet error is NULL");

  fail_unless (g_signal_connect (st, "new-local-candidate",
      G_CALLBACK (_new_local_candidate), GINT_TO_POINTER (has_stun)),
    "Coult not connect new-local-candidate signal");
  fail_unless (g_signal_connect (st, "local-candidates-prepared",
      G_CALLBACK (_local_candidates_prepared), GINT_TO_POINTER (has_stun)),
    "Coult not connect local-candidates-prepared signal");
  fail_unless (g_signal_connect (st, "new-active-candidate-pair",
      G_CALLBACK (_new_active_candidate_pair), trans),
    "Coult not connect new-active-candidate-pair signal");
  fail_unless (g_signal_connect (st, "error",
      G_CALLBACK (_stream_transmitter_error), NULL),
    "Could not connect error signal");

  g_idle_add (_start_pipeline, pipeline);

  g_main_run (loop);


  g_object_unref (st);

  g_object_unref (trans);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);

  g_main_loop_unref (loop);

}

GST_START_TEST (test_rawudptransmitter_run_nostun)
{
  g_message ("No stun, nothing");
  run_rawudp_transmitter_test (0, NULL, FALSE);
}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_run_invalid_stun)
{
  GParameter params[3];

  /*
   * Hopefully not one is runing a stun server on local port 7777
   */

  g_message ("Invalid STUN, it should timeout");

  memset (params, 0, sizeof(GParameter) * 3);

  params[0].name = "stun-ip";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_static_string (&params[0].value, "127.0.0.1");

  params[1].name = "stun-port";
  g_value_init (&params[1].value, G_TYPE_UINT);
  g_value_set_uint (&params[1].value, 7777);

  params[2].name = "stun-timeout";
  g_value_init (&params[2].value, G_TYPE_UINT);
  g_value_set_uint (&params[2].value, 3);

  run_rawudp_transmitter_test (3, params, FALSE);

}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_run_stunserver_dot_org)
{
  GParameter params[3];

  /*
   * Hopefully not one is runing a stun server on local port 7777
   */

  g_message ("Using stunserver.org (192.245.12.229)");

  memset (params, 0, sizeof(GParameter) * 3);

  params[0].name = "stun-ip";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_static_string (&params[0].value, "192.245.12.229");

  params[1].name = "stun-port";
  g_value_init (&params[1].value, G_TYPE_UINT);
  g_value_set_uint (&params[1].value, 3478);

  params[2].name = "stun-timeout";
  g_value_init (&params[2].value, G_TYPE_UINT);
  g_value_set_uint (&params[2].value, 5);

  run_rawudp_transmitter_test (3, params, TRUE);

}
GST_END_TEST;



static Suite *
rawudptransmitter_suite (void)
{
  Suite *s = suite_create ("rawudptransmitter");
  TCase *tc_chain = tcase_create ("rawudptransmitter");
  TCase *tc_chain2 = tcase_create ("rawudptransmitter-stun-timeout");
  TCase *tc_chain3 = tcase_create ("rawudptransmitter-stunserver-org");

  suite_add_tcase (s, tc_chain);
  suite_add_tcase (s, tc_chain2);
  suite_add_tcase (s, tc_chain3);

  tcase_set_timeout (tc_chain, 5);
  tcase_add_test (tc_chain, test_rawudptransmitter_new);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_nostun);

  tcase_set_timeout (tc_chain2, 5);
  tcase_add_test (tc_chain2, test_rawudptransmitter_run_invalid_stun);

  tcase_set_timeout (tc_chain3, 10);
  tcase_add_test (tc_chain3, test_rawudptransmitter_run_stunserver_dot_org);

  return s;
}


GST_CHECK_MAIN (rawudptransmitter);

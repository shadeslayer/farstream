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


GST_START_TEST (test_rawudptransmitter_new)
{
  GError *error = NULL;
  FsTransmitter *trans;
  GstElement *pipeline;
  GstElement *fakesrc;
  GstElement *trans_sink, *trans_src;

  trans = fs_transmitter_new ("rawudp", &error);

  if (error) {
    fail("Error creating transmitter: %s", error->message);
  }

  fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans, &fakesrc);

  g_object_get (trans, "gst-sink", &trans_sink, "gst-src", &trans_src, NULL);

  gst_object_unref (fakesrc);

  g_object_unref (trans);

  gst_object_unref (pipeline);

}
GST_END_TEST;

gint candidates[2] = {0,0};

static void
_new_local_candidate (FsStreamTransmitter *st, FsCandidate *candidate,
  gpointer user_data)
{
  gboolean has_stun = GPOINTER_TO_INT (user_data);

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
    fail_unless (strcmp (candidate->proto_subtype, "RTP") == 0,
      "Proto subtype %s does not match component %d", candidate->proto_subtype,
      candidate->component_id);
  else
    fail ("Invalid component %d", candidate->component_id);

  if (has_stun)
    fail_unless (candidate->type == FS_CANDIDATE_TYPE_SRFLX,
      "Has stun, but candidate is not server reflexive");
  else
    fail_unless (candidate->type == FS_CANDIDATE_TYPE_HOST,
      "Has stun, but candidate is not host");

  candidates[candidate->component_id-1] = 1;
}

GST_START_TEST (test_rawudptransmitter_run_nostun)
{
  GError *error = NULL;
  FsTransmitter *trans;
  FsStreamTransmitter *st;
  GstElement *pipeline;
  GstElement *fakesrc;
  GstElement *trans_sink, *trans_src;

  const gint N_PARAMS = 0;
  GParameter params[N_PARAMS];

  trans = fs_transmitter_new ("rawudp", &error);

  if (error) {
    fail("Error creating transmitter: %s", error->message);
  }

  fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans, &fakesrc);

  g_object_get (trans, "gst-sink", &trans_sink, "gst-src", &trans_src, NULL);


  fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  g_debug ("PRE-ALLO");

  /*
  params[0].name = "stun-ip";
  g_value_set_static_string (params[0].value, "192.245.12.229");
  params[1].name = "stun-port";
  g_value_set_uint (params[0].value, 3478);
  */
  st = fs_transmitter_new_stream_transmitter (trans, NULL, N_PARAMS, params,
    &error);

  g_debug ("ALLO");

  if (error) {
    fail("Error creating stream transmitter: %s", error->message);
  }

  fail_if (st == NULL, "No stream transmitter created, yet error is NULL");

  g_debug ("ALLO2");

  g_signal_connect (st, "new-local-candidate",
    G_CALLBACK (_new_local_candidate), GINT_TO_POINTER (0));
  g_debug ("ALLO2.5");
  g_signal_connect (st, "error", G_CALLBACK (_stream_transmitter_error), NULL);

  g_debug ("ALLO3");

  g_object_unref (st);

  g_object_unref (trans);

  gst_object_unref (pipeline);

}
GST_END_TEST;



static Suite *
rawudptransmitter_suite (void)
{
  Suite *s = suite_create ("rawudptransmitter");
  TCase *tc_chain = tcase_create ("rawudptransmitter");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_rawudptransmitter_new);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_nostun);

  return s;
}


GST_CHECK_MAIN (rawudptransmitter);

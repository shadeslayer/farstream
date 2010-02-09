/* Farsight 2 unit tests for FsShmTransmitter
 *
 * Copyright (C) 2009 Collabora Ltd
 * @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright (C) 2009 Nokia
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
#include <gst/farsight/fs-transmitter.h>
#include <gst/farsight/fs-conference-iface.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <unistd.h>

#include "check-threadsafe.h"
#include "generic.h"

gint buffer_count[2] = {0, 0};
gboolean got_candidates[2];
gboolean got_prepared[2];
GstElement *pipeline = NULL;
gboolean src_setup[2] = {FALSE, FALSE};
guint received_known[2] = {0, 0};
gboolean associate_on_source = TRUE;

GMutex *mutex;
GCond *cond;
gboolean done = FALSE;
guint connected_count;


enum {
  FLAG_NO_SOURCE = 1 << 2,
  FLAG_NOT_SENDING = 1 << 3,
  FLAG_RECVONLY_FILTER = 1 << 4,
  FLAG_LOCAL_CANDIDATES = 1 << 5
};

#define RTP_PORT 9828
#define RTCP_PORT 9829

GST_START_TEST (test_shmtransmitter_new)
{
  gchar **transmitters;
  gint i;
  gboolean found_it = FALSE;

  transmitters = fs_transmitter_list_available ();
  for (i=0; transmitters[i]; i++)
  {
    if (!strcmp ("shm", transmitters[i]))
    {
      found_it = TRUE;
      break;
    }
  }
  g_strfreev (transmitters);

  ts_fail_unless (found_it, "Did not find shm transmitter");

  test_transmitter_creation ("shm");
  test_transmitter_creation ("shm");
}
GST_END_TEST;

static void
_new_local_candidate (FsStreamTransmitter *st, FsCandidate *candidate,
  gpointer user_data)
{
  ts_fail_if (candidate == NULL, "Passed NULL candidate");
  ts_fail_unless (candidate->ip != NULL, "Null IP in candidate");
  ts_fail_unless (candidate->proto == FS_NETWORK_PROTOCOL_UDP,
    "Protocol is not UDP");

  ts_fail_unless (candidate->type == FS_CANDIDATE_TYPE_HOST,
      "Candidate is not host");
  ts_fail_unless (got_candidates[candidate->component_id-1] == FALSE);
  got_candidates[candidate->component_id-1] = TRUE;

  GST_DEBUG ("New local candidate %s of type %d for component %d",
      candidate->ip, candidate->type, candidate->component_id);
}


static void
_candidate_prepared (FsStreamTransmitter *st,gpointer user_data)
{
  GST_DEBUG ("Local candidates prepared");

  fail_unless (got_candidates[0] == TRUE || got_candidates[1] == TRUE);

  if (got_candidates[0])
    got_prepared[0] = TRUE;
  if (got_candidates[1])
    got_prepared[1] = TRUE;
}

static void
_state_changed (FsStreamTransmitter *st, guint component_id,
    FsStreamState state, gpointer user_data)
{
  g_mutex_lock (mutex);
  connected_count++;
  g_mutex_unlock (mutex);
  g_cond_signal (cond);
}

static void
_handoff_handler (GstElement *element, GstBuffer *buffer, GstPad *pad,
  gpointer user_data)
{
  gint component_id = GPOINTER_TO_INT (user_data);

  ts_fail_unless (GST_BUFFER_SIZE (buffer) == component_id * 10,
    "Buffer is size %d but component_id is %d", GST_BUFFER_SIZE (buffer),
    component_id);

  buffer_count[component_id-1]++;

  GST_LOG ("Buffer %d component: %d size: %u", buffer_count[component_id-1],
    component_id, GST_BUFFER_SIZE (buffer));

  ts_fail_if (buffer_count[component_id-1] > 20,
    "Too many buffers %d > 20 for component",
    buffer_count[component_id-1], component_id);

  if (buffer_count[0] == 20 && buffer_count[1] == 20) {
    GST_DEBUG ("Test complete, got 20 buffers twice");
    /* TEST OVER */
    if (associate_on_source)
      ts_fail_unless (buffer_count[0] == received_known[0] &&
          buffer_count[1] == received_known[1], "Some known buffers from known"
          " sources have not been reported (%d != %u || %d != %u)",
          buffer_count[0], received_known[0],
          buffer_count[1], received_known[1]);
    else
      ts_fail_unless (received_known[0] == 0 && received_known[1] == 0,
          "Got a known-source-packet-received signal when we shouldn't have");

    g_mutex_lock (mutex);
    done = TRUE;
    g_mutex_unlock (mutex);
    g_cond_signal (cond);
  }
}

static void
_known_source_packet_received (FsStreamTransmitter *st, guint component_id,
    GstBuffer *buffer, gpointer user_data)
{
  ts_fail_unless (associate_on_source == TRUE,
      "Got known-source-packet-received when we shouldn't have");

  ts_fail_unless (component_id == 1 || component_id == 2,
      "Invalid component id %u", component_id);

  ts_fail_unless (GST_IS_BUFFER (buffer), "Invalid buffer received at %p",
      buffer);

  received_known[component_id - 1]++;

  GST_LOG ("Known source buffer %d component: %d size: %u",
      received_known[component_id-1], component_id, GST_BUFFER_SIZE (buffer));
}

void
sync_error_handler (GstBus *bus, GstMessage *message, gpointer blob)
{
  GError *error = NULL;
  gchar *debug;
  gst_message_parse_error (message, &error, &debug);
  g_error ("bus sync error %s debug: %s", error->message, debug);
}


static GstElement *
get_recvonly_filter (FsTransmitter *trans, guint component, gpointer user_data)
{
  if (component == 1)
    return NULL;

  return gst_element_factory_make ("identity", NULL);
}

static void
run_shm_transmitter_test (gint flags)
{
  GError *error = NULL;
  FsTransmitter *trans;
  FsStreamTransmitter *st;
  GstBus *bus = NULL;
  GParameter params[1];
  GList *local_cands = NULL;
  GstStateChangeReturn ret;
  FsCandidate *cand;
  GList *remote_cands = NULL;
  int param_count = 0;

  done = FALSE;
  connected_count = 0;
  cond = g_cond_new ();
  mutex = g_mutex_new ();

  buffer_count[0] = 0;
  buffer_count[1] = 0;
  received_known[0] = 0;
  received_known[1] = 0;

  got_candidates[0] = FALSE;
  got_candidates[1] = FALSE;
  got_prepared[0] = FALSE;
  got_prepared[1] = FALSE;

  if (unlink ("/tmp/src1") < 0 && errno != ENOENT)
    fail ("Could not unlink /tmp/src1: %s", strerror (errno));
  if (unlink ("/tmp/src2") < 0 && errno != ENOENT)
    fail ("Could not unlink /tmp/src2: %s", strerror (errno));


  local_cands = g_list_append (local_cands, fs_candidate_new (NULL, 1,
          FS_CANDIDATE_TYPE_HOST, FS_NETWORK_PROTOCOL_UDP, "/tmp/src1", 0));
  local_cands = g_list_append (local_cands, fs_candidate_new (NULL, 2,
          FS_CANDIDATE_TYPE_HOST, FS_NETWORK_PROTOCOL_UDP, "/tmp/src2", 0));

  if (flags & FLAG_LOCAL_CANDIDATES)
  {
    memset (params, 0, sizeof (GParameter));

    params[0].name = "preferred-local-candidates";
    g_value_init (&params[0].value, FS_TYPE_CANDIDATE_LIST);
    g_value_take_boxed (&params[0].value, local_cands);

    param_count = 1;
  }


  associate_on_source = !(flags & FLAG_NO_SOURCE);

  if ((flags & FLAG_NOT_SENDING) && (flags & FLAG_RECVONLY_FILTER))
  {
    buffer_count[0] = 20;
    received_known[0] = 20;
  }

  trans = fs_transmitter_new ("shm", 2, 0, &error);

  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  ts_fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  if (flags & FLAG_RECVONLY_FILTER)
    ts_fail_unless (g_signal_connect (trans, "get-recvonly-filter",
            G_CALLBACK (get_recvonly_filter), NULL));


  pipeline = setup_pipeline (trans, G_CALLBACK (_handoff_handler));

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, bus_error_callback, NULL);

  gst_bus_enable_sync_message_emission (bus);
  g_signal_connect (bus, "sync-message::error",
      G_CALLBACK (sync_error_handler), NULL);

  gst_object_unref (bus);

  st = fs_transmitter_new_stream_transmitter (trans, NULL,
      param_count, params, &error);

  if (param_count)
    g_value_unset (&params[0].value);

  if (error)
    ts_fail ("Error creating stream transmitter: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);

  ts_fail_if (st == NULL, "No stream transmitter created, yet error is NULL");

  g_object_set (st, "sending", !(flags & FLAG_NOT_SENDING), NULL);

  ts_fail_unless (g_signal_connect (st, "new-local-candidate",
      G_CALLBACK (_new_local_candidate), trans),
    "Could not connect new-local-candidate signal");
  ts_fail_unless (g_signal_connect (st, "local-candidates-prepared",
      G_CALLBACK (_candidate_prepared), NULL),
    "Could not connect local-candidates-prepared signal");
  ts_fail_unless (g_signal_connect (st, "error",
      G_CALLBACK (stream_transmitter_error), NULL),
    "Could not connect error signal");
  ts_fail_unless (g_signal_connect (st, "known-source-packet-received",
      G_CALLBACK (_known_source_packet_received), NULL),
    "Could not connect known-source-packet-received signal");
  ts_fail_unless (g_signal_connect (st, "state-changed",
      G_CALLBACK (_state_changed), NULL),
    "Could not connect state-changed signal");

  if (!fs_stream_transmitter_gather_local_candidates (st, &error))
  {
    if (error)
    {
      ts_fail ("Could not start gathering local candidates (%s:%d) %s",
          g_quark_to_string (error->domain), error->code, error->message);
    }
    else
      ts_fail ("Could not start gathering candidates"
          " (without a specified error)");
  }

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  ts_fail_if (ret == GST_STATE_CHANGE_FAILURE,
      "Could not set the pipeline to playing");

  if (!(flags & FLAG_LOCAL_CANDIDATES))
  {
    ret = fs_stream_transmitter_set_remote_candidates (st, local_cands,
        &error);
    fs_candidate_list_destroy (local_cands);
    if (error)
      ts_fail ("Error while adding candidate: (%s:%d) %s",
          g_quark_to_string (error->domain), error->code, error->message);
    ts_fail_unless (ret == TRUE, "No detailed error from add_remote_candidate");
  }

  cand = fs_candidate_new (NULL, 1,
          FS_CANDIDATE_TYPE_HOST, FS_NETWORK_PROTOCOL_UDP, NULL, 0);
  cand->username = g_strdup ("/tmp/src1");
  remote_cands = g_list_prepend (remote_cands, cand);
  cand = fs_candidate_new (NULL, 2,
          FS_CANDIDATE_TYPE_HOST, FS_NETWORK_PROTOCOL_UDP, NULL, 0);
  cand->username = g_strdup ("/tmp/src2");
  remote_cands = g_list_prepend (remote_cands, cand);
  ret = fs_stream_transmitter_set_remote_candidates (st, remote_cands, &error);
  fs_candidate_list_destroy (remote_cands);
  if (error)
    ts_fail ("Error while adding candidate: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  ts_fail_unless (ret == TRUE, "No detailed error from add_remote_candidate");

  g_mutex_lock (mutex);
  while (connected_count < 2)
    g_cond_wait (cond, mutex);
  g_mutex_unlock (mutex);

  setup_fakesrc (trans, pipeline, 1);
  setup_fakesrc (trans, pipeline, 2);

  g_mutex_lock (mutex);
  while (!done)
    g_cond_wait (cond, mutex);
  g_mutex_unlock (mutex);

  fail_unless (got_prepared[0] == TRUE);
  fail_unless (got_prepared[1] == TRUE);
  fail_unless (got_candidates[0] == TRUE);
  fail_unless (got_candidates[1] == TRUE);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  if (st)
  {
    fs_stream_transmitter_stop (st);
    g_object_unref (st);
  }

  g_object_unref (trans);

  gst_object_unref (pipeline);

  g_cond_free (cond);
  g_mutex_free (mutex);
}

GST_START_TEST (test_shmtransmitter_run_basic)
{
  run_shm_transmitter_test (0);
}
GST_END_TEST;

GST_START_TEST (test_shmtransmitter_with_filter)
{
  run_shm_transmitter_test (FLAG_RECVONLY_FILTER);
}
GST_END_TEST;

GST_START_TEST (test_shmtransmitter_sending_half)
{
  run_shm_transmitter_test (FLAG_NOT_SENDING | FLAG_RECVONLY_FILTER);
}
GST_END_TEST;

GST_START_TEST (test_shmtransmitter_local_cands)
{
  run_shm_transmitter_test (FLAG_LOCAL_CANDIDATES);
}
GST_END_TEST;


static Suite *
shmtransmitter_suite (void)
{
  Suite *s = suite_create ("shmtransmitter");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);

  tc_chain = tcase_create ("shmtransmitter_new");
  tcase_add_test (tc_chain, test_shmtransmitter_new);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("shmtransmitter_basic");
  tcase_add_test (tc_chain, test_shmtransmitter_run_basic);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("shmtransmitter-with-filter");
  tcase_add_test (tc_chain, test_shmtransmitter_with_filter);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("shmtransmitter-sending-half");
  tcase_add_test (tc_chain, test_shmtransmitter_sending_half);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("shmtransmitter-local-candidates");
  tcase_add_test (tc_chain, test_shmtransmitter_local_cands);
  suite_add_tcase (s, tc_chain);

  return s;
}


GST_CHECK_MAIN (shmtransmitter);

/* Farstream unit tests for FsRawUdpTransmitter
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
#include <farstream/fs-transmitter.h>
#include <farstream/fs-conference.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <unistd.h>

#include "check-threadsafe.h"
#include "generic.h"
#include "transmitter/rawudp-upnp.h"
#include "testutils.h"

#include "stunalternd.h"


gint buffer_count[2] = {0, 0};
GMainLoop *loop = NULL;
gint candidates[2] = {0, 0};
GstElement *pipeline = NULL;
gboolean src_setup[2] = {FALSE, FALSE};
volatile gint running = TRUE;
guint received_known[2] = {0, 0};
gboolean has_stun = FALSE;
gboolean associate_on_source = TRUE;

gboolean pipeline_done = FALSE;
GStaticMutex pipeline_mod_mutex = G_STATIC_MUTEX_INIT;

void *stun_alternd_data = NULL;

enum {
  FLAG_HAS_STUN  = 1 << 0,
  FLAG_IS_LOCAL  = 1 << 1,
  FLAG_NO_SOURCE = 1 << 2,
  FLAG_NOT_SENDING = 1 << 3,
  FLAG_RECVONLY_FILTER = 1 << 4
};

#define RTP_PORT 9828
#define RTCP_PORT 9829


GST_START_TEST (test_rawudptransmitter_new)
{
  gchar **transmitters;
  gint i;
  gboolean found_it = FALSE;

  transmitters = fs_transmitter_list_available ();
  for (i=0; transmitters[i]; i++)
  {
    if (!strcmp ("rawudp", transmitters[i]))
    {
      found_it = TRUE;
      break;
    }
  }
  g_strfreev (transmitters);

  ts_fail_unless (found_it, "Did not find rawudp transmitter");

  test_transmitter_creation ("rawudp");
  test_transmitter_creation ("rawudp");
}
GST_END_TEST;

static void
_new_local_candidate (FsStreamTransmitter *st, FsCandidate *candidate,
  gpointer user_data)
{
  gboolean is_local = GPOINTER_TO_INT (user_data) & FLAG_IS_LOCAL;
  GError *error = NULL;
  GList *item = NULL;
  gboolean ret;

  GST_DEBUG ("Has local candidate %s:%u of type %d",
    candidate->ip, candidate->port, candidate->type);

  ts_fail_if (candidate == NULL, "Passed NULL candidate");
  ts_fail_unless (candidate->ip != NULL, "Null IP in candidate");
  ts_fail_if (candidate->port == 0, "Candidate has port 0");
  ts_fail_unless (candidate->proto == FS_NETWORK_PROTOCOL_UDP,
    "Protocol is not UDP");

  if (has_stun)
    ts_fail_unless (candidate->type == FS_CANDIDATE_TYPE_SRFLX,
      "Has stun, but candidate is not server reflexive,"
      " it is: %s:%u of type %d on component %u",
      candidate->ip, candidate->port, candidate->type, candidate->component_id);
  else {
    ts_fail_unless (candidate->type == FS_CANDIDATE_TYPE_HOST,
        "Does not have stun, but candidate is not host");
    if (candidate->component_id == FS_COMPONENT_RTP) {
      ts_fail_unless (candidate->port % 2 == 0, "RTP port should be odd");
    } else if (candidate->component_id == FS_COMPONENT_RTCP) {
      ts_fail_unless (candidate->port % 2 == 1, "RTCP port should be event");
    }
  }

  if (is_local) {
    ts_fail_unless (!strcmp (candidate->ip, "127.0.0.1"),
      "IP is wrong, it is %s but should be 127.0.0.1 when local candidate set",
      candidate->ip);

    if (candidate->component_id == FS_COMPONENT_RTP) {
      ts_fail_unless (candidate->port >= RTP_PORT  , "RTP port invalid");
    } else if (candidate->component_id == FS_COMPONENT_RTCP) {
      ts_fail_unless (candidate->port >= RTCP_PORT, "RTCP port invalid");
    }
  }


  candidates[candidate->component_id-1] = 1;

  GST_DEBUG ("New local candidate %s:%d of type %d for component %d",
    candidate->ip, candidate->port, candidate->type, candidate->component_id);

  item = g_list_prepend (NULL, candidate);

  ret = fs_stream_transmitter_force_remote_candidates (st, item, &error);

  g_list_free (item);

  if (error)
    ts_fail ("Error while adding candidate: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);

  ts_fail_unless (ret == TRUE, "No detailed error from add_remote_candidate");

}

static void
_local_candidates_prepared (FsStreamTransmitter *st, gpointer user_data)
{
  ts_fail_if (candidates[0] == 0, "candidates-prepared with no RTP candidate");
  ts_fail_if (candidates[1] == 0, "candidates-prepared with no RTCP candidate");

  GST_DEBUG ("Local Candidates Prepared");

  /*
   * This doesn't work on my router
   */

  if (has_stun)
  {
    g_main_loop_quit (loop);
    g_atomic_int_set(&running, FALSE);
  }
}


static void
_new_active_candidate_pair (FsStreamTransmitter *st, FsCandidate *local,
  FsCandidate *remote, gpointer user_data)
{
  ts_fail_if (local == NULL, "Local candidate NULL");
  ts_fail_if (remote == NULL, "Remote candidate NULL");

  ts_fail_unless (local->component_id == remote->component_id,
    "Local and remote candidates dont have the same component id");

  GST_DEBUG ("New active candidate pair for component %d", local->component_id);

  g_static_mutex_lock (&pipeline_mod_mutex);
  if (!pipeline_done && !src_setup[local->component_id-1])
    setup_fakesrc (user_data, pipeline, local->component_id);
  src_setup[local->component_id-1] = TRUE;
  g_static_mutex_unlock (&pipeline_mod_mutex);
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
    g_atomic_int_set(&running, FALSE);
    g_main_loop_quit (loop);
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
}

static gboolean
check_running (gpointer data)
{
  if (g_atomic_int_get (&running) == FALSE)
    g_main_loop_quit (loop);

  return FALSE;
}

void
sync_error_handler (GstBus *bus, GstMessage *message, gpointer blob)
{
  GError *error = NULL;
  gchar *debug;
  gst_message_parse_error (message, &error, &debug);
  g_error ("bus sync error %s", error->message);
}


static GstElement *
get_recvonly_filter (FsTransmitter *trans, guint component, gpointer user_data)
{
  if (component == 1)
    return NULL;

  return gst_element_factory_make ("identity", NULL);
}

static void
run_rawudp_transmitter_test (gint n_parameters, GParameter *params,
  gint flags)
{
  GError *error = NULL;
  FsTransmitter *trans;
  FsStreamTransmitter *st;
  GstBus *bus = NULL;
  guint tos;

  buffer_count[0] = 0;
  buffer_count[1] = 0;
  received_known[0] = 0;
  received_known[1] = 0;
  pipeline_done = FALSE;

  has_stun = flags & FLAG_HAS_STUN;
  associate_on_source = !(flags & FLAG_NO_SOURCE);

  if ((flags & FLAG_NOT_SENDING) && (flags & FLAG_RECVONLY_FILTER))
  {
    buffer_count[0] = 20;
    received_known[0] = 20;
  }

  loop = g_main_loop_new (NULL, FALSE);
  trans = fs_transmitter_new ("rawudp", 2, 0, &error);

  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  ts_fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  g_object_set (trans, "tos", 2, NULL);
  g_object_get (trans, "tos", &tos, NULL);
  ts_fail_unless (tos == 2);

  if (flags & FLAG_RECVONLY_FILTER)
    ts_fail_unless (g_signal_connect (trans, "get-recvonly-filter",
            G_CALLBACK (get_recvonly_filter), NULL));


  pipeline = setup_pipeline (trans, G_CALLBACK (_handoff_handler));

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, bus_error_callback, NULL);

  gst_bus_enable_sync_message_emission (bus);
  g_signal_connect (bus, "sync-message::error", G_CALLBACK (sync_error_handler), NULL);

  gst_object_unref (bus);

  st = fs_transmitter_new_stream_transmitter (trans, NULL, n_parameters, params,
    &error);

  if (error) {
    if (has_stun &&
        error->domain == FS_ERROR &&
        error->code == FS_ERROR_NETWORK &&
        error->message && strstr (error->message, "unreachable"))
    {
      GST_WARNING ("Skipping stunserver test, we have no network");
      goto skip;
    }
    else
      ts_fail ("Error creating stream transmitter: (%s:%d) %s",
          g_quark_to_string (error->domain), error->code, error->message);
  }

  ts_fail_if (st == NULL, "No stream transmitter created, yet error is NULL");

  g_object_set (st, "sending", !(flags & FLAG_NOT_SENDING), NULL);

  ts_fail_unless (g_signal_connect (st, "new-local-candidate",
      G_CALLBACK (_new_local_candidate), GINT_TO_POINTER (flags)),
    "Could not connect new-local-candidate signal");
  ts_fail_unless (g_signal_connect (st, "local-candidates-prepared",
      G_CALLBACK (_local_candidates_prepared), GINT_TO_POINTER (flags)),
    "Could not connect local-candidates-prepared signal");
  ts_fail_unless (g_signal_connect (st, "new-active-candidate-pair",
      G_CALLBACK (_new_active_candidate_pair), trans),
    "Could not connect new-active-candidate-pair signal");
  ts_fail_unless (g_signal_connect (st, "error",
      G_CALLBACK (stream_transmitter_error), NULL),
    "Could not connect error signal");
  ts_fail_unless (g_signal_connect (st, "known-source-packet-received",
      G_CALLBACK (_known_source_packet_received), NULL),
    "Could not connect known-source-packet-received signal");

  ts_fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

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

  g_idle_add (check_running, NULL);

  g_main_loop_run (loop);

 skip:

  g_static_mutex_lock (&pipeline_mod_mutex);
  pipeline_done = TRUE;
  g_static_mutex_unlock (&pipeline_mod_mutex);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  if (st)
  {
    fs_stream_transmitter_stop (st);
    g_object_unref (st);
  }

  g_object_unref (trans);

  gst_object_unref (pipeline);

  g_main_loop_unref (loop);
}

GST_START_TEST (test_rawudptransmitter_run_nostun)
{
  GParameter params[1];

  memset (params, 0, sizeof (GParameter));

  params[0].name = "upnp-discovery";
  g_value_init (&params[0].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[0].value, FALSE);

  run_rawudp_transmitter_test (1, params, 0);
}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_run_nostun_nosource)
{
  GParameter params[2];

  memset (params, 0, sizeof (GParameter) * 2);

  params[0].name = "associate-on-source";
  g_value_init (&params[0].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[0].value, FALSE);

  params[1].name = "upnp-discovery";
  g_value_init (&params[1].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[1].value, FALSE);

  run_rawudp_transmitter_test (2, params, FLAG_NO_SOURCE);
}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_run_invalid_stun)
{
  GParameter params[4];

  /*
   * Hopefully not one is runing a stun server on local port 7777
   */

  memset (params, 0, sizeof (GParameter) * 4);

  params[0].name = "stun-ip";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_static_string (&params[0].value, "127.0.0.1");

  params[1].name = "stun-port";
  g_value_init (&params[1].value, G_TYPE_UINT);
  g_value_set_uint (&params[1].value, 7777);

  params[2].name = "stun-timeout";
  g_value_init (&params[2].value, G_TYPE_UINT);
  g_value_set_uint (&params[2].value, 3);

  params[3].name = "upnp-discovery";
  g_value_init (&params[3].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[3].value, FALSE);

  run_rawudp_transmitter_test (4, params, 0);
}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_run_stund)
{
  GParameter params[4];

  if (stund_pid <= 0)
    return;

  memset (params, 0, sizeof (GParameter) * 4);

  params[0].name = "stun-ip";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_static_string (&params[0].value, "127.0.0.1");

  params[1].name = "stun-port";
  g_value_init (&params[1].value, G_TYPE_UINT);
  g_value_set_uint (&params[1].value, 3478);

  params[2].name = "stun-timeout";
  g_value_init (&params[2].value, G_TYPE_UINT);
  g_value_set_uint (&params[2].value, 5);

  params[3].name = "upnp-discovery";
  g_value_init (&params[3].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[3].value, FALSE);


  run_rawudp_transmitter_test (3, params, FLAG_HAS_STUN);
}
GST_END_TEST;


GST_START_TEST (test_rawudptransmitter_run_local_candidates)
{
  GParameter params[2];
  GList *list = NULL;
  FsCandidate *candidate;

  memset (params, 0, sizeof (GParameter) * 2);

  candidate = fs_candidate_new ("L1",
      FS_COMPONENT_RTP, FS_CANDIDATE_TYPE_HOST,
      FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", RTP_PORT);
  list = g_list_prepend (list, candidate);

  candidate = fs_candidate_new ("L1",
      FS_COMPONENT_RTCP, FS_CANDIDATE_TYPE_HOST,
      FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", RTCP_PORT);
  list = g_list_prepend (list, candidate);

  params[0].name = "preferred-local-candidates";
  g_value_init (&params[0].value, FS_TYPE_CANDIDATE_LIST);
  g_value_set_boxed (&params[0].value, list);

  params[1].name = "upnp-discovery";
  g_value_init (&params[1].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[1].value, FALSE);


  run_rawudp_transmitter_test (2, params, FLAG_IS_LOCAL);

  g_value_reset (&params[0].value);

  fs_candidate_list_destroy (list);
}
GST_END_TEST;

static gboolean
_bus_stop_stream_cb (GstBus *bus, GstMessage *message, gpointer user_data)
{
  FsStreamTransmitter *st = user_data;
  GstState oldstate, newstate, pending;

  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_STATE_CHANGED ||
      G_OBJECT_TYPE (GST_MESSAGE_SRC (message)) != GST_TYPE_PIPELINE)
    return bus_error_callback (bus, message, user_data);

  gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);

  if (newstate != GST_STATE_PLAYING)
    return TRUE;

  if (pending != GST_STATE_VOID_PENDING)
    ts_fail ("New state playing, but pending is %d", pending);

  GST_DEBUG ("Stopping stream transmitter");

  fs_stream_transmitter_stop (st);
  g_object_unref (st);

  GST_DEBUG ("Stopped stream transmitter");

  g_atomic_int_set(&running, FALSE);
  g_main_loop_quit (loop);

  return TRUE;
}

static void
_handoff_handler_empty (GstElement *element, GstBuffer *buffer, GstPad *pad,
  gpointer user_data)
{
}


/*
 * This test checks that starting a stream, getting it to playing
 * then stopping it, while the pipeline is playing works
 */

GST_START_TEST (test_rawudptransmitter_stop_stream)
{
  GError *error = NULL;
  FsTransmitter *trans;
  FsStreamTransmitter *st;
  GstBus *bus = NULL;
  GParameter params[1];

  memset (params, 0, sizeof (GParameter));

  params[0].name = "upnp-discovery";
  g_value_init (&params[0].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[0].value, FALSE);

  has_stun = FALSE;

  loop = g_main_loop_new (NULL, FALSE);
  trans = fs_transmitter_new ("rawudp", 2, 0, &error);

  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  ts_fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans, G_CALLBACK (_handoff_handler_empty));

  st = fs_transmitter_new_stream_transmitter (trans, NULL, 1, params, &error);

  if (error)
    ts_fail ("Error creating stream transmitter: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);

  ts_fail_if (st == NULL, "No stream transmitter created, yet error is NULL");

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, _bus_stop_stream_cb, st);
  gst_object_unref (bus);

  ts_fail_unless (g_signal_connect (st, "new-local-candidate",
          G_CALLBACK (_new_local_candidate), NULL),
      "Could not connect new-local-candidate signal");
  ts_fail_unless (g_signal_connect (st, "new-active-candidate-pair",
          G_CALLBACK (_new_active_candidate_pair), trans),
      "Could not connect new-active-candidate-pair signal");
  ts_fail_unless (g_signal_connect (st, "error",
          G_CALLBACK (stream_transmitter_error), NULL),
      "Could not connect error signal");

  ts_fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  if (!fs_stream_transmitter_gather_local_candidates (st, &error))
  {
    if (error)
      ts_fail ("Could not start gathering local candidates %s",
          error->message);
    else
      ts_fail ("Could not start gathering candidates"
          " (without a specified error)");
  }

  g_idle_add (check_running, NULL);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_object_unref (trans);

  gst_object_unref (pipeline);

  g_main_loop_unref (loop);
}
GST_END_TEST;

#ifdef HAVE_GUPNP

GST_START_TEST (test_rawudptransmitter_run_upnp_discovery)
{
  GParameter params[2];
  GObject *context;
  gboolean got_address = FALSE;
  gboolean added_mapping = FALSE;

  memset (params, 0, sizeof (GParameter) * 2);

  params[0].name = "associate-on-source";
  g_value_init (&params[0].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[0].value, TRUE);

  params[1].name = "upnp-discovery";
  g_value_init (&params[1].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[1].value, TRUE);

  context = start_upnp_server ();

  run_rawudp_transmitter_test (2, params, 0);


  get_vars (&got_address, &added_mapping);
  ts_fail_unless (got_address, "did not get address");
  ts_fail_unless (added_mapping, "did not add mapping");
  g_object_unref (context);
}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_run_upnp_fallback)
{
  GParameter params[6];
  GObject *context;
  gboolean got_address = FALSE;
  gboolean added_mapping = FALSE;

  memset (params, 0, sizeof (GParameter) * 6);

  params[0].name = "associate-on-source";
  g_value_init (&params[0].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[0].value, TRUE);

  params[1].name = "upnp-discovery";
  g_value_init (&params[1].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[1].value, FALSE);

  params[2].name = "stun-ip";
  g_value_init (&params[2].value, G_TYPE_STRING);
  g_value_set_static_string (&params[2].value, "127.0.0.1");

  params[3].name = "stun-port";
  g_value_init (&params[3].value, G_TYPE_UINT);
  g_value_set_uint (&params[3].value, 3232);

  params[4].name = "stun-timeout";
  g_value_init (&params[4].value, G_TYPE_UINT);
  g_value_set_uint (&params[4].value, 6);

  params[5].name = "upnp-discovery-timeout";
  g_value_init (&params[5].value, G_TYPE_UINT);
  g_value_set_uint (&params[5].value, 3);

  context = start_upnp_server ();

  run_rawudp_transmitter_test (6, params, 0);

  get_vars (&got_address, &added_mapping);
  ts_fail_unless (got_address, "did not get address");
  ts_fail_unless (added_mapping, "did not add mapping");

  g_object_unref (context);
}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_run_upnp_ignored)
{
  GParameter params[6];
  GObject *context;

  if (stund_pid <= 0)
    return;

  memset (params, 0, sizeof (GParameter) * 6);

  params[0].name = "associate-on-source";
  g_value_init (&params[0].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[0].value, TRUE);

  params[1].name = "upnp-discovery";
  g_value_init (&params[1].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[1].value, FALSE);

  params[2].name = "stun-ip";
  g_value_init (&params[2].value, G_TYPE_STRING);
  g_value_set_static_string (&params[2].value, "127.0.0.1");

  params[3].name = "stun-port";
  g_value_init (&params[3].value, G_TYPE_UINT);
  g_value_set_uint (&params[3].value, 3478);

  params[4].name = "stun-timeout";
  g_value_init (&params[4].value, G_TYPE_UINT);
  g_value_set_uint (&params[4].value, 6);

  params[5].name = "upnp-discovery-timeout";
  g_value_init (&params[5].value, G_TYPE_UINT);
  g_value_set_uint (&params[5].value, 3);

  context = start_upnp_server ();

  run_rawudp_transmitter_test (6, params, FLAG_HAS_STUN);

  g_object_unref (context);
}
GST_END_TEST;

#endif /* HAVE_GUPNP */


GST_START_TEST (test_rawudptransmitter_with_filter)
{
  GParameter params[2];

  memset (params, 0, sizeof (GParameter) * 2);

  params[0].name = "associate-on-source";
  g_value_init (&params[0].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[0].value, TRUE);

  params[1].name = "upnp-discovery";
  g_value_init (&params[1].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[1].value, FALSE);

  run_rawudp_transmitter_test (2, params,
      FLAG_RECVONLY_FILTER);
}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_sending_half)
{
  GParameter params[2];

  memset (params, 0, sizeof (GParameter) * 2);

  params[0].name = "associate-on-source";
  g_value_init (&params[0].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[0].value, TRUE);

  params[1].name = "upnp-discovery";
  g_value_init (&params[1].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[1].value, FALSE);

  run_rawudp_transmitter_test (2, params,
      FLAG_NOT_SENDING | FLAG_RECVONLY_FILTER);
}
GST_END_TEST;


GST_START_TEST (test_rawudptransmitter_run_stunalternd)
{
  GParameter params[4];

  if (stund_pid <= 0 || stun_alternd_data == NULL)
    return;

  memset (params, 0, sizeof (GParameter) * 4);

  params[0].name = "stun-ip";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_static_string (&params[0].value, "127.0.0.1");

  params[1].name = "stun-port";
  g_value_init (&params[1].value, G_TYPE_UINT);
  g_value_set_uint (&params[1].value, 3480);

  params[2].name = "stun-timeout";
  g_value_init (&params[2].value, G_TYPE_UINT);
  g_value_set_uint (&params[2].value, 5);

  params[3].name = "upnp-discovery";
  g_value_init (&params[3].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[3].value, FALSE);


  run_rawudp_transmitter_test (3, params, FLAG_HAS_STUN);
}
GST_END_TEST;


GST_START_TEST (test_rawudptransmitter_run_stun_altern_to_nowhere)
{
  GParameter params[4];

  if (stun_alternd_data == NULL)
    return;

  /*
   * Hopefully not one is runing a stun server on local port 3478
   */

  memset (params, 0, sizeof (GParameter) * 4);

  params[0].name = "stun-ip";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_static_string (&params[0].value, "127.0.0.1");

  params[1].name = "stun-port";
  g_value_init (&params[1].value, G_TYPE_UINT);
  g_value_set_uint (&params[1].value, 3480);

  params[2].name = "stun-timeout";
  g_value_init (&params[2].value, G_TYPE_UINT);
  g_value_set_uint (&params[2].value, 10);

  params[3].name = "upnp-discovery";
  g_value_init (&params[3].value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&params[3].value, FALSE);

  run_rawudp_transmitter_test (4, params, 0);
}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_strange_arguments)
{
  FsTransmitter *trans = NULL;
  FsStreamTransmitter *st = NULL;
  GError *error = NULL;
  guint comps = 0;
  FsCandidate *cand;
  GList *list;

  trans = fs_transmitter_new ("rawudp", 3, 0, &error);
  ts_fail_if (trans == NULL);
  ts_fail_unless (error == NULL);

  g_object_get (trans, "components", &comps, NULL);
  ts_fail_unless (comps == 3);

  /* valid */
  st = fs_transmitter_new_stream_transmitter (trans, NULL, 0, NULL, &error);
  ts_fail_if (st == NULL);
  ts_fail_unless (error == NULL);

  /* Valid candidate, port 0 */
  cand = fs_candidate_new ("abc", 1,
      FS_CANDIDATE_TYPE_HOST, FS_NETWORK_PROTOCOL_UDP, "1.2.3.4", 0);
  list = g_list_prepend (NULL, cand);
  ts_fail_unless (fs_stream_transmitter_force_remote_candidates (st, list,
          &error));
  ts_fail_unless (error == NULL);
  fs_candidate_list_destroy (list);

  fs_stream_transmitter_stop (st);
  g_object_unref (st);
  g_object_unref (trans);
}
GST_END_TEST;

void
setup_stunalternd_valid (void)
{
  stun_alternd_data = stun_alternd_init (AF_INET,
      "127.0.0.1", 3478, 3480);

  if (!stun_alternd_data)
    GST_WARNING ("Could not spawn stunalternd,"
        " skipping stun alternate server testing");
}

static void
setup_stunalternd_loop (void)
{
  stun_alternd_data = stun_alternd_init (AF_INET,
      "127.0.0.1", 3478, 3478);

  if (!stun_alternd_data)
    GST_WARNING ("Could not spawn stunalternd,"
        " skipping stun alternate server testing");
}

static void
teardown_stunalternd (void)
{
  if (!stun_alternd_data)
    return;

  stun_alternd_stop (stun_alternd_data);
  stun_alternd_data = NULL;
}

static void
setup_stund_stunalternd (void)
{
  setup_stund ();
  setup_stunalternd_valid ();
}


static void
teardown_stund_stunalternd (void)
{
  teardown_stund ();
  teardown_stunalternd ();
}


static Suite *
rawudptransmitter_suite (void)
{
  Suite *s = suite_create ("rawudptransmitter");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);

  tc_chain = tcase_create ("rawudptransmitter_new");
  tcase_add_test (tc_chain, test_rawudptransmitter_new);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter_nostun");
  tcase_add_test (tc_chain, test_rawudptransmitter_run_nostun);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter_nostun_nosource");
  tcase_add_test (tc_chain, test_rawudptransmitter_run_nostun_nosource);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-stun-timeout");
  tcase_set_timeout (tc_chain, 5);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_invalid_stun);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-stund");
  tcase_set_timeout (tc_chain, 15);
  tcase_add_checked_fixture (tc_chain, setup_stund, teardown_stund);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_stund);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-local-candidates");
  tcase_add_test (tc_chain, test_rawudptransmitter_run_local_candidates);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-stop-stream");
  tcase_add_test (tc_chain, test_rawudptransmitter_stop_stream);
  suite_add_tcase (s, tc_chain);

#ifdef HAVE_GUPNP
  {
    gchar *multicast_addr;

    multicast_addr = find_multicast_capable_address ();
    g_free (multicast_addr);

    if (multicast_addr)
    {
      tc_chain = tcase_create ("rawudptransmitter-upnp-discovery");
      tcase_add_test (tc_chain, test_rawudptransmitter_run_upnp_discovery);
      suite_add_tcase (s, tc_chain);

      tc_chain = tcase_create ("rawudptransmitter-upnp-fallback");
      tcase_add_test (tc_chain, test_rawudptransmitter_run_upnp_fallback);
      suite_add_tcase (s, tc_chain);

      tc_chain = tcase_create ("rawudptransmitter-upnp-ignored");
      tcase_add_checked_fixture (tc_chain, setup_stund, teardown_stund);
      tcase_add_test (tc_chain, test_rawudptransmitter_run_upnp_ignored);
      suite_add_tcase (s, tc_chain);
    }
  }
#endif

  tc_chain = tcase_create ("rawudptransmitter-with-filter");
  tcase_add_test (tc_chain, test_rawudptransmitter_with_filter);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-sending-half");
  tcase_add_test (tc_chain, test_rawudptransmitter_sending_half);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-stunalternd");
  tcase_set_timeout (tc_chain, 5);
  tcase_add_checked_fixture (tc_chain, setup_stund_stunalternd,
      teardown_stund_stunalternd);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_stunalternd);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-stunalternd-to-nowhere");
  tcase_set_timeout (tc_chain, 12);
  tcase_add_checked_fixture (tc_chain, setup_stunalternd_valid,
      teardown_stunalternd);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_stun_altern_to_nowhere);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-stunalternd-loop");
  tcase_set_timeout (tc_chain, 12);
  tcase_add_checked_fixture (tc_chain, setup_stunalternd_loop,
      teardown_stunalternd);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_stun_altern_to_nowhere);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-strange-arguments");
  tcase_add_test (tc_chain, test_rawudptransmitter_strange_arguments);
  suite_add_tcase (s, tc_chain);

  return s;
}


GST_CHECK_MAIN (rawudptransmitter);

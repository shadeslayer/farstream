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
#include <gst/farstream/fs-transmitter.h>
#include <gst/farstream/fs-conference.h>

#include <unistd.h>

#include "check-threadsafe.h"
#include "generic.h"
#include "fake-filter.h"


enum {
  FLAG_NO_SOURCE = 1 << 0,
  FLAG_IS_LOCAL = 1 << 1,
  FLAG_FORCE_CANDIDATES = 1 << 2,
  FLAG_NOT_SENDING = 1 << 3,
  FLAG_RECVONLY_FILTER = 1 << 4
};


gint buffer_count[2][2] = {{0,0}, {0,0}};
guint received_known[2][2] = {{0,0}, {0,0}};
GMainLoop *loop = NULL;
volatile gint running = TRUE;
gboolean associate_on_source = TRUE;
gboolean is_address_local = FALSE;
gboolean force_candidates = FALSE;

GStaticMutex count_mutex = G_STATIC_MUTEX_INIT;

GST_START_TEST (test_nicetransmitter_new)
{
  test_transmitter_creation ("nice");
}
GST_END_TEST;

static void
_new_local_candidate (FsStreamTransmitter *st, FsCandidate *candidate,
  gpointer user_data)
{
  GST_DEBUG ("Has local candidate %s:%u of type %d",
    candidate->ip, candidate->port, candidate->type);

  ts_fail_if (candidate == NULL, "Passed NULL candidate");
  ts_fail_unless (candidate->ip != NULL, "Null IP in candidate");
  ts_fail_if (candidate->port == 0, "Candidate has port 0");
  ts_fail_unless (candidate->proto == FS_NETWORK_PROTOCOL_UDP,
    "Protocol is not UDP");
  ts_fail_if (candidate->foundation == NULL,
      "Candidate doenst have a foundation");
  ts_fail_if (candidate->component_id == 0, "Component id is 0");
  if (candidate->type == FS_CANDIDATE_TYPE_HOST)
  {
    ts_fail_if (candidate->base_ip != NULL, "Host candidate has a base ip");
    ts_fail_if (candidate->base_port != 0, "Host candidate has a base port");
  }
  else
  {
    ts_fail_if (candidate->base_ip == NULL, "Candidate doesnt have a base ip");
    ts_fail_if (candidate->base_port == 0, "Candidate doesnt have a base port");
  }
  ts_fail_if (candidate->username == NULL, "Candidate doenst have a username");
  ts_fail_if (candidate->password == NULL, "Candidate doenst have a password");

  GST_DEBUG ("New local candidate %s:%d of type %d for component %d",
    candidate->ip, candidate->port, candidate->type, candidate->component_id);
  GST_DEBUG ("username: %s password: %s", candidate->username,
      candidate->password);

  if (is_address_local)
    ts_fail_unless (!strcmp (candidate->ip, "127.0.0.1"));

  g_object_set_data (G_OBJECT (st), "candidates",
      g_list_append (g_object_get_data (G_OBJECT (st), "candidates"),
          fs_candidate_copy (candidate)));
}

static gboolean
set_the_candidates (gpointer user_data)
{
  FsStreamTransmitter *st = FS_STREAM_TRANSMITTER (user_data);
  GList *candidates = g_object_get_data (G_OBJECT (st), "candidates-set");
  gboolean ret;
  GError *error = NULL;

  if (!candidates)
  {
    g_debug ("Skipping libnice check because it found NO local candidates");
    g_atomic_int_set(&running, FALSE);
    g_main_loop_quit (loop);
    return FALSE;
  }

  if (force_candidates)
  {
    GList *item = NULL;
    GList *next = NULL;
    GList *new_list = NULL;
    for (item = candidates; item; item = next)
    {
      FsCandidate *cand = item->data;
      GList *item2 = NULL;
      next = g_list_next (item);

      for (item2 = new_list; item2; item2 = g_list_next (item2))
      {
        FsCandidate *cand2 = item2->data;
        if (cand2->component_id == cand->component_id)
          break;
      }
      if (!item2)
      {
        candidates = g_list_remove (candidates, cand);
        new_list = g_list_append (new_list, cand);
      }
    }

    ret = fs_stream_transmitter_force_remote_candidates (st, new_list, &error);

    fs_candidate_list_destroy (new_list);
  }
  else
  {
    ret = fs_stream_transmitter_add_remote_candidates (st, candidates, &error);
  }

  if (error)
    ts_fail ("Error while adding candidate: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);

  ts_fail_unless (ret == TRUE, "No detailed error setting remote_candidate");

  fs_candidate_list_destroy (candidates);

  return FALSE;
}


static void
_local_candidates_prepared (FsStreamTransmitter *st, gpointer user_data)
{
  FsStreamTransmitter *st2 = FS_STREAM_TRANSMITTER (user_data);
  GList *candidates = g_object_get_data (G_OBJECT (st), "candidates");

  g_object_set_data (G_OBJECT (st), "candidates", NULL);

  ts_fail_if (g_list_length (candidates) < 2,
      "We don't have at least 2 candidates");

  GST_DEBUG ("Local Candidates Prepared");

  g_object_set_data (G_OBJECT (st2), "candidates-set", candidates);

  g_idle_add (set_the_candidates, st2);

}

static void
_new_active_candidate_pair (FsStreamTransmitter *st, FsCandidate *local,
  FsCandidate *remote, gpointer user_data)
{
  ts_fail_if (local == NULL, "Local candidate NULL");
  ts_fail_if (remote == NULL, "Remote candidate NULL");

  ts_fail_unless (local->component_id == remote->component_id,
    "Local and remote candidates dont have the same component id");

  GST_DEBUG ("New active candidate pair");
}

static void
_handoff_handler (GstElement *element, GstBuffer *buffer, GstPad *pad,
    guint stream, gint component_id)
{
  ts_fail_unless (GST_BUFFER_SIZE (buffer) == component_id * 10,
    "Buffer is size %d but component_id is %d", GST_BUFFER_SIZE (buffer),
    component_id);

  g_static_mutex_lock (&count_mutex);

  buffer_count[stream][component_id-1]++;


  if (buffer_count[stream][component_id-1] % 10 == 0)
  {
    GST_DEBUG ("Buffer %d stream: %u component: %d size: %u",
        buffer_count[stream][component_id-1], stream,
        component_id, GST_BUFFER_SIZE (buffer));
    GST_DEBUG ("Received %d %d %d %d",
        buffer_count[0][0], buffer_count[0][1],
        buffer_count[1][0], buffer_count[1][1]);
  }

  ts_fail_if (buffer_count[stream][component_id-1] > 20,
    "Too many buffers %d > 20 for component",
    buffer_count[stream][component_id-1], component_id);

  if (buffer_count[0][0] == 20 && buffer_count[0][1] == 20 &&
      buffer_count[1][0] == 20 && buffer_count[1][1] == 20) {
    if (associate_on_source)
      ts_fail_unless (buffer_count[0][0] == received_known[0][0] &&
          buffer_count[0][1] == received_known[0][1] &&
          buffer_count[1][0] == received_known[1][0] &&
          buffer_count[1][1] == received_known[1][1],
          "Some known buffers from known sources have not been reported"
          " (%d != %u || %d != %u || %d != %u || %d != %u)",
          buffer_count[0][0], received_known[0][0],
          buffer_count[0][1], received_known[0][1],
          buffer_count[1][0], received_known[1][0],
          buffer_count[1][1], received_known[1][1]);
    else
      ts_fail_unless (received_known[0][0] == 0 &&
          received_known[0][1] == 0 &&
          received_known[1][0] == 0 &&
          received_known[1][1] == 0,
          "Got a known-source-packet-received signal when we shouldn't have");

    /* TEST OVER */
    g_atomic_int_set(&running, FALSE);
    g_main_loop_quit (loop);
  }

  g_static_mutex_unlock (&count_mutex);
}

static void
_handoff_handler1 (GstElement *element, GstBuffer *buffer, GstPad *pad,
    gpointer user_data)
{
  gint component_id = GPOINTER_TO_INT (user_data);

  _handoff_handler (element, buffer, pad, 0, component_id);
}

static void
_handoff_handler2 (GstElement *element, GstBuffer *buffer, GstPad *pad,
    gpointer user_data)
{
  gint component_id = GPOINTER_TO_INT (user_data);

  _handoff_handler (element, buffer, pad, 1, component_id);
}


static void
_known_source_packet_received (FsStreamTransmitter *st, guint component_id,
    GstBuffer *buffer, gpointer user_data)
{
  guint stream = GPOINTER_TO_UINT (user_data);

  ts_fail_unless (associate_on_source == TRUE,
      "Got known-source-packet-received when we shouldn't have");

  ts_fail_unless (component_id == 1 || component_id == 2,
      "Invalid component id %u", component_id);

  ts_fail_unless (GST_IS_BUFFER (buffer), "Invalid buffer received at %p",
      buffer);

  received_known[stream - 1][component_id - 1]++;
}

static void
_stream_state_changed (FsStreamTransmitter *st, guint component,
    FsStreamState state, gpointer user_data)
{
  FsTransmitter *trans = FS_TRANSMITTER (user_data);
  GEnumClass *enumclass = NULL;
  GEnumValue *enumvalue = NULL;
  gchar *prop = NULL;
  FsStreamState oldstate = 0;

  enumclass = g_type_class_ref (FS_TYPE_STREAM_STATE);
  enumvalue = g_enum_get_value (enumclass, state);

  GST_DEBUG ("%p: Stream state for component %u is now %s (%u)", st,
      component, enumvalue->value_nick, state);

  ts_fail_if (state == FS_STREAM_STATE_FAILED,
      "Failed to establish a connection");

  if (component == 1)
    prop = "last-state-1";
  else if (component == 2)
    prop = "last-state-2";
  else
    ts_fail ("Invalid component %u, component");

  oldstate = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (st), prop));

  ts_fail_if (state < FS_STREAM_STATE_CONNECTED && state < oldstate,
      "State went in wrong direction %d -> %d for component %u",
      oldstate, state, component);

  g_object_set_data (G_OBJECT (st), prop, GINT_TO_POINTER (state));

  if (state < FS_STREAM_STATE_READY)
    return;

  if (component == 1)
    prop = "src_setup_1";
  else if (component == 2)
    prop = "src_setup_2";

  if (g_object_get_data (G_OBJECT (trans), prop) == NULL)
  {
    GstElement *pipeline = GST_ELEMENT (
        g_object_get_data (G_OBJECT (trans), "pipeline"));
    GST_DEBUG ("%p: Setting up fakesrc for component %u", st, component);
    setup_fakesrc (trans, pipeline, component);
    g_object_set_data (G_OBJECT (trans), prop, "");
  }
  else
    GST_DEBUG ("FAKESRC ALREADY SETUP for component %u", component);
}


static gboolean
check_running (gpointer data)
{
  if (g_atomic_int_get (&running) == FALSE)
    g_main_loop_quit (loop);

  return FALSE;
}

typedef FsParticipant FsNiceTestParticipant;
typedef FsParticipantClass FsNiceTestParticipantClass;

G_DEFINE_TYPE (FsNiceTestParticipant, fs_nice_test_participant,
    FS_TYPE_PARTICIPANT)

static void
fs_nice_test_participant_init (FsNiceTestParticipant *self)
{
}

static void
fs_nice_test_participant_class_init (FsNiceTestParticipantClass *klass)
{
}

static GstElement *
_get_recvonly_filter (FsTransmitter *trans, guint component, gpointer user_data)
{
  if (component == 1)
    return NULL;

  return gst_element_factory_make ("fsfakefilter", NULL);
}

static void
run_nice_transmitter_test (gint n_parameters, GParameter *params,
  gint flags)
{
  GError *error = NULL;
  FsTransmitter *trans = NULL, *trans2 = NULL;
  FsStreamTransmitter *st = NULL, *st2 = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GstElement *pipeline2 = NULL;
  FsNiceTestParticipant *p1 = NULL, *p2 = NULL;

  memset (buffer_count, 0, sizeof(gint)*4);
  memset (received_known, 0, sizeof(guint)*4);
  running = TRUE;

  associate_on_source = !(flags & FLAG_NO_SOURCE);
  is_address_local = (flags & FLAG_IS_LOCAL);
  force_candidates = (flags & FLAG_FORCE_CANDIDATES);

  if (flags & FLAG_RECVONLY_FILTER)
    ts_fail_unless (fs_fake_filter_register ());

  if (flags & FLAG_NOT_SENDING)
  {
    buffer_count[0][0] = 20;
    received_known[0][0] = 20;
    buffer_count[1][0] = 20;
    received_known[1][0] = 20;
  }

  loop = g_main_loop_new (NULL, FALSE);

  trans = fs_transmitter_new ("nice", 2, 0, &error);
  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);
  }
  ts_fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  if (flags & FLAG_RECVONLY_FILTER)
    ts_fail_unless (g_signal_connect (trans, "get-recvonly-filter",
            G_CALLBACK (_get_recvonly_filter), NULL));

  trans2 = fs_transmitter_new ("nice", 2, 0, &error);
  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);
  }
  ts_fail_if (trans2 == NULL, "No transmitter create, yet error is still NULL");

 if (flags & FLAG_RECVONLY_FILTER)
    ts_fail_unless (g_signal_connect (trans2, "get-recvonly-filter",
            G_CALLBACK (_get_recvonly_filter), NULL));

  pipeline = setup_pipeline (trans, G_CALLBACK (_handoff_handler1));
  pipeline2 = setup_pipeline (trans2, G_CALLBACK (_handoff_handler2));

  g_object_set_data (G_OBJECT (trans), "pipeline", pipeline);
  g_object_set_data (G_OBJECT (trans2), "pipeline", pipeline2);

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, bus_error_callback, NULL);
  gst_object_unref (bus);

  bus = gst_element_get_bus (pipeline2);
  gst_bus_add_watch (bus, bus_error_callback, NULL);
  gst_object_unref (bus);

  /*
   * I'm passing the participant because any gobject will work,
   * but it should be the participant
   */

  p1 = g_object_new (fs_nice_test_participant_get_type (), NULL);
  p2 = g_object_new (fs_nice_test_participant_get_type (), NULL);

  st = fs_transmitter_new_stream_transmitter (trans, FS_PARTICIPANT (p1),
      n_parameters,  params, &error);
  if (error)
    ts_fail ("Error creating stream transmitter: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);
  ts_fail_if (st == NULL, "No stream transmitter created, yet error is NULL");

  st2 = fs_transmitter_new_stream_transmitter (trans2, FS_PARTICIPANT (p2),
      n_parameters, params, &error);
  if (error)
    ts_fail ("Error creating stream transmitter: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);
  ts_fail_if (st2 == NULL, "No stream transmitter created, yet error is NULL");

  g_object_set (st, "sending", !(flags & FLAG_NOT_SENDING), NULL);
  g_object_set (st2, "sending", !(flags & FLAG_NOT_SENDING), NULL);

  ts_fail_unless (g_signal_connect (st, "new-local-candidate",
      G_CALLBACK (_new_local_candidate), st2),
    "Could not connect new-local-candidate signal");
  ts_fail_unless (g_signal_connect (st, "local-candidates-prepared",
      G_CALLBACK (_local_candidates_prepared), st2),
    "Could not connect local-candidates-prepared signal");
  ts_fail_unless (g_signal_connect (st, "new-active-candidate-pair",
      G_CALLBACK (_new_active_candidate_pair), trans),
    "Could not connect new-active-candidate-pair signal");
  ts_fail_unless (g_signal_connect (st, "error",
      G_CALLBACK (stream_transmitter_error), NULL),
    "Could not connect error signal");
  ts_fail_unless (g_signal_connect (st, "state-changed",
          G_CALLBACK (_stream_state_changed), trans),
      "Could not connect to state-changed signal");
  ts_fail_unless (g_signal_connect (st, "known-source-packet-received",
          G_CALLBACK (_known_source_packet_received), GUINT_TO_POINTER (1)),
      "Could not connect to known-source-packet-received signal");

  ts_fail_unless (g_signal_connect (st2, "new-local-candidate",
      G_CALLBACK (_new_local_candidate), st),
    "Could not connect new-local-candidate signal");
  ts_fail_unless (g_signal_connect (st2, "local-candidates-prepared",
      G_CALLBACK (_local_candidates_prepared), st),
    "Could not connect local-candidates-prepared signal");
  ts_fail_unless (g_signal_connect (st2, "new-active-candidate-pair",
      G_CALLBACK (_new_active_candidate_pair), trans2),
    "Could not connect new-active-candidate-pair signal");
  ts_fail_unless (g_signal_connect (st2, "error",
      G_CALLBACK (stream_transmitter_error), NULL),
    "Could not connect error signal");
  ts_fail_unless (g_signal_connect (st2, "state-changed",
          G_CALLBACK (_stream_state_changed), trans2),
      "Could not connect to state-changed signal");
  ts_fail_unless (g_signal_connect (st2, "known-source-packet-received",
          G_CALLBACK (_known_source_packet_received), GUINT_TO_POINTER (2)),
      "Could not connect to known-source-packet-received signal");

  ts_fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  ts_fail_if (gst_element_set_state (pipeline2, GST_STATE_PLAYING) ==
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

  if (!fs_stream_transmitter_gather_local_candidates (st2, &error))
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

  fs_stream_transmitter_stop (st);
  fs_stream_transmitter_stop (st2);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_element_set_state (pipeline2, GST_STATE_NULL);
  gst_element_get_state (pipeline2, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (st)
    g_object_unref (st);
  if (st2)
    g_object_unref (st2);

  g_object_unref (trans);
  g_object_unref (trans2);

  g_object_unref (p1);
  g_object_unref (p2);

  gst_object_unref (pipeline);

  g_main_loop_unref (loop);

}

GST_START_TEST (test_nicetransmitter_basic)
{
  run_nice_transmitter_test (0, NULL, 0);
}
GST_END_TEST;

GST_START_TEST (test_nicetransmitter_no_associate_on_source)
{
  GParameter param = {NULL, {0}};

  param.name = "associate-on-source";
  g_value_init (&param.value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&param.value, FALSE);

  run_nice_transmitter_test (1, &param, FLAG_NO_SOURCE);
}
GST_END_TEST;

GST_START_TEST (test_nicetransmitter_preferred_candidates)
{
  GParameter param = {NULL, {0}};
  FsCandidate *candidate;
  GList *list = NULL;

  candidate = fs_candidate_new ("L1",
      FS_COMPONENT_NONE, FS_CANDIDATE_TYPE_HOST,
      FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", 0);
  list = g_list_prepend (list, candidate);

  candidate = fs_candidate_new ("L1",
      FS_COMPONENT_NONE, FS_CANDIDATE_TYPE_HOST,
      FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", 0);
  list = g_list_prepend (list, candidate);

  param.name = "preferred-local-candidates";
  g_value_init (&param.value, FS_TYPE_CANDIDATE_LIST);
  g_value_take_boxed (&param.value, list);

  run_nice_transmitter_test (1, &param, FLAG_IS_LOCAL);

  g_value_unset (&param.value);
}
GST_END_TEST;

GST_START_TEST (test_nicetransmitter_stund)
{
  GParameter params[2];

  if (stund_pid <= 0)
    return;

  memset (params, 0, sizeof (GParameter) * 2);

  params[0].name = "stun-ip";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_static_string (&params[0].value, "127.0.0.1");

  params[1].name = "stun-port";
  g_value_init (&params[1].value, G_TYPE_UINT);
  g_value_set_uint (&params[1].value, 3478);

  run_nice_transmitter_test (2, params, 0);
}
GST_END_TEST;


GST_START_TEST (test_nicetransmitter_force_candidates)
{
  run_nice_transmitter_test (0, NULL, FLAG_FORCE_CANDIDATES);
}
GST_END_TEST;


GST_START_TEST (test_nicetransmitter_invalid_arguments)
{
  FsTransmitter *trans = NULL;
  FsStreamTransmitter *st = NULL;
  FsNiceTestParticipant *p = NULL;
  GError *error = NULL;
  guint comps = 0;
  GParameter params[1];
  GValueArray *va;
  GstStructure *s;
  GValue val = {0};
  FsCandidate *cand;
  GList *list;

  memset (params, 0, sizeof(GParameter) * 1);

  trans = fs_transmitter_new ("nice", 3, 0, &error);
  ts_fail_if (trans == NULL);
  ts_fail_unless (error == NULL);

  g_object_get (trans, "components", &comps, NULL);
  ts_fail_unless (comps == 3);

  st = fs_transmitter_new_stream_transmitter (trans, NULL, 0, NULL, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);

  p = g_object_new (fs_nice_test_participant_get_type (), NULL);

  params[0].name = "preferred-local-candidates";
  g_value_init (&params[0].value, FS_TYPE_CANDIDATE_LIST);


  /* invalid port */
  g_value_take_boxed (&params[0].value, g_list_append (NULL,
          fs_candidate_new (NULL, 0, FS_CANDIDATE_TYPE_HOST,
              FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", 7777)));

  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);


  /* invalid componnent */
  g_value_take_boxed (&params[0].value, g_list_append (NULL,
          fs_candidate_new (NULL, 1, FS_CANDIDATE_TYPE_HOST,
              FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", 0)));

  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);

  /* invalid IP */
  g_value_take_boxed (&params[0].value, g_list_append (NULL,
          fs_candidate_new (NULL, 0, FS_CANDIDATE_TYPE_HOST,
              FS_NETWORK_PROTOCOL_UDP, NULL, 0)));
  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);

  /* invalid type */
  g_value_take_boxed (&params[0].value, g_list_append (NULL,
          fs_candidate_new (NULL, 0, FS_CANDIDATE_TYPE_MULTICAST,
              FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", 0)));

  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);

  /* invalid proto */
  g_value_take_boxed (&params[0].value, g_list_append (NULL,
          fs_candidate_new (NULL, 0, FS_CANDIDATE_TYPE_HOST,
              FS_NETWORK_PROTOCOL_TCP, "127.0.0.1", 0)));

  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);
  g_value_unset (&params[0].value);

  params[0].name = "relay-info";
  g_value_init (&params[0].value, G_TYPE_VALUE_ARRAY);

  /* no IP */
  va = g_value_array_new (1);
  s = gst_structure_new ("aa",
      "port", G_TYPE_UINT, 7654,
      "username", G_TYPE_STRING, "blah",
      "password", G_TYPE_STRING, "blah2",
      NULL);
  g_value_init (&val, GST_TYPE_STRUCTURE);
  g_value_take_boxed (&val, s);
  g_value_array_append (va, &val);
  g_value_take_boxed (&params[0].value, va);
  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);

  /* no port */
  va = g_value_array_new (1);
  s = gst_structure_new ("aa",
      "ip", G_TYPE_STRING, "127.0.0.1",
      "username", G_TYPE_STRING, "blah",
      "password", G_TYPE_STRING, "blah2",
      NULL);
  g_value_take_boxed (&val, s);
  g_value_array_append (va, &val);
  g_value_take_boxed (&params[0].value, va);
  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);


  /* invalid port */
  va = g_value_array_new (1);
  s = gst_structure_new ("aa",
      "ip", G_TYPE_STRING, "127.0.0.1",
      "port", G_TYPE_UINT, 65536,
      "username", G_TYPE_STRING, "blah",
      "password", G_TYPE_STRING, "blah2",
      NULL);
  g_value_take_boxed (&val, s);
  g_value_array_append (va, &val);
  g_value_take_boxed (&params[0].value, va);
  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);

  /* no username */
  va = g_value_array_new (1);
  s = gst_structure_new ("aa",
      "ip", G_TYPE_STRING, "127.0.0.1",
      "port", G_TYPE_UINT, 7654,
      "password", G_TYPE_STRING, "blah2",
      NULL);
  g_value_take_boxed (&val, s);
  g_value_array_append (va, &val);
  g_value_take_boxed (&params[0].value, va);
  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);

  /* no password */
  va = g_value_array_new (1);
  s = gst_structure_new ("aa",
      "ip", G_TYPE_STRING, "127.0.0.1",
      "port", G_TYPE_UINT, 7654,
      "username", G_TYPE_STRING, "blah",
      NULL);
  g_value_take_boxed (&val, s);
  g_value_array_append (va, &val);
  g_value_take_boxed (&params[0].value, va);
  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_unless (st == NULL);
  ts_fail_unless (error &&
      error->domain == FS_ERROR &&
      error->code == FS_ERROR_INVALID_ARGUMENTS);
  g_clear_error (&error);

  /* valid */
  va = g_value_array_new (1);
  s = gst_structure_new ("aa",
      "ip", G_TYPE_STRING, "127.0.0.1",
      "port", G_TYPE_UINT, 7654,
      "username", G_TYPE_STRING, "blah",
      "password", G_TYPE_STRING, "blah2",
      NULL);
  g_value_take_boxed (&val, s);
  g_value_array_append (va, &val);
  g_value_take_boxed (&params[0].value, va);
  st = fs_transmitter_new_stream_transmitter (trans, p, 1, params, &error);
  ts_fail_if (st == NULL);
  ts_fail_unless (error == NULL);
  g_value_unset (&val);
  g_value_unset (&params[0].value);

  /* Valid candidate, port 0 */
  cand = fs_candidate_new ("abc", 1,
      FS_CANDIDATE_TYPE_HOST, FS_NETWORK_PROTOCOL_UDP, "1.2.3.4", 0);
  cand->username = g_strdup ("a1");
  cand->password = g_strdup ("a1");
  list = g_list_prepend (NULL, cand);
  ts_fail_unless (fs_stream_transmitter_add_remote_candidates (st, list,
          &error));
  ts_fail_unless (error == NULL);
  fs_candidate_list_destroy (list);

  fs_stream_transmitter_stop (st);
  g_object_unref (st);
  g_object_unref (p);
  g_object_unref (trans);
}
GST_END_TEST;

GST_START_TEST (test_nicetransmitter_with_filter)
{
  run_nice_transmitter_test (0, NULL, FLAG_RECVONLY_FILTER);
}
GST_END_TEST;

GST_START_TEST (test_nicetransmitter_sending_half)
{
  run_nice_transmitter_test (0, NULL, FLAG_NOT_SENDING | FLAG_RECVONLY_FILTER);
}
GST_END_TEST;


static Suite *
nicetransmitter_suite (void)
{
  Suite *s = suite_create ("nicetransmitter");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);

  tc_chain = tcase_create ("nicetransmitter");
  tcase_add_test (tc_chain, test_nicetransmitter_new);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("nicetransmitter-basic");
  tcase_add_test (tc_chain, test_nicetransmitter_basic);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("nicetransmitter-no-assoc-on-source");
  tcase_add_test (tc_chain, test_nicetransmitter_no_associate_on_source);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("nicetransmitter-preferred-candidates");
  tcase_add_test (tc_chain, test_nicetransmitter_preferred_candidates);
  suite_add_tcase (s, tc_chain);

  if (g_getenv ("STUND"))
  {
    tc_chain = tcase_create ("nicetransmitter-stund");
    tcase_add_checked_fixture (tc_chain, setup_stund, teardown_stund);
    tcase_add_test (tc_chain, test_nicetransmitter_stund);
    suite_add_tcase (s, tc_chain);
  }

  tc_chain = tcase_create ("nicetransmitter-force-candidates");
  tcase_add_test (tc_chain, test_nicetransmitter_force_candidates);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("nicetransmitter-invalid-arguments");
  tcase_add_test (tc_chain, test_nicetransmitter_invalid_arguments);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("nicetransmitter-with-filter");
  tcase_add_test (tc_chain, test_nicetransmitter_with_filter);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("nicetransmitter-sending-half");
  tcase_add_test (tc_chain, test_nicetransmitter_sending_half);
  suite_add_tcase (s, tc_chain);

  return s;
}


GST_CHECK_MAIN (nicetransmitter);

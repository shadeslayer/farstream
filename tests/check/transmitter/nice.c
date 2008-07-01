/* Farsight 2 unit tests for FsRawUdpTransmitter
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
#include <gst/farsight/fs-transmitter.h>
#include <gst/farsight/fs-conference-iface.h>

#include "check-threadsafe.h"
#include "generic.h"


gint buffer_count[2][2] = {{0,0}, {0,0}};
GMainLoop *loop = NULL;
volatile gint running = TRUE;

GST_START_TEST (test_nicetransmitter_new)
{
  test_transmitter_creation ("nice");
}
GST_END_TEST;

static void
_new_local_candidate (FsStreamTransmitter *st, FsCandidate *candidate,
  gpointer user_data)
{
  GError *error = NULL;
  gboolean ret;
  FsStreamTransmitter *st2 = FS_STREAM_TRANSMITTER (user_data);

  g_debug ("Has local candidate %s:%u of type %d",
    candidate->ip, candidate->port, candidate->type);

  ts_fail_if (candidate == NULL, "Passed NULL candidate");
  ts_fail_unless (candidate->ip != NULL, "Null IP in candidate");
  ts_fail_if (candidate->port == 0, "Candidate has port 0");
  ts_fail_unless (candidate->proto == FS_NETWORK_PROTOCOL_UDP,
    "Protocol is not UDP");
  ts_fail_if (candidate->foundation == NULL,
      "Candidate doenst have a foundation");
  ts_fail_if (candidate->component_id == 0, "Component id is 0");
  ts_fail_if (candidate->base_ip == NULL, "Candidate doesnt have a base ip");
  ts_fail_if (candidate->base_port == 0, "Candidate doesnt have a base port");
  ts_fail_if (candidate->username == NULL, "Candidate doenst have a username");
  ts_fail_if (candidate->password == NULL, "Candidate doenst have a password");

  g_object_set_data (G_OBJECT (st), "candidates",
      GUINT_TO_POINTER (1 +
          GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (st), "candidates"))));

  g_debug ("New local candidate %s:%d of type %d for component %d",
    candidate->ip, candidate->port, candidate->type, candidate->component_id);
  g_debug ("username: %s password: %s", candidate->username,
      candidate->password);

  ret = fs_stream_transmitter_add_remote_candidate (st2, candidate, &error);

  if (error)
    ts_fail ("Error while adding candidate: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);

  ts_fail_unless (ret == TRUE, "No detailed error from add_remote_candidate");

}

static void
_local_candidates_prepared (FsStreamTransmitter *st, gpointer user_data)
{
  FsStreamTransmitter *st2 = FS_STREAM_TRANSMITTER (user_data);
  ts_fail_if (
      GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (st), "candidates")) < 2,
      "We don't have at least 2 candidates");


  g_debug ("Local Candidates Prepared");

  fs_stream_transmitter_remote_candidates_added (st2);
}


static void
_new_active_candidate_pair (FsStreamTransmitter *st, FsCandidate *local,
  FsCandidate *remote, gpointer user_data)
{
  ts_fail_if (local == NULL, "Local candidate NULL");
  ts_fail_if (remote == NULL, "Remote candidate NULL");

  ts_fail_unless (local->component_id == remote->component_id,
    "Local and remote candidates dont have the same component id");

  g_debug ("New active candidate pair");
}

static void
_handoff_handler (GstElement *element, GstBuffer *buffer, GstPad *pad,
    guint stream, gint component_id)
{
  ts_fail_unless (GST_BUFFER_SIZE (buffer) == component_id * 10,
    "Buffer is size %d but component_id is %d", GST_BUFFER_SIZE (buffer),
    component_id);

  buffer_count[stream][component_id-1]++;


  g_debug ("Buffer %d stream: %u component: %d size: %u",
      buffer_count[stream][component_id-1], stream,
      component_id, GST_BUFFER_SIZE (buffer));

  g_debug ("has %d %d %d %d",
      buffer_count[0][0], buffer_count[0][1],
      buffer_count[1][0], buffer_count[1][1]);

  ts_fail_if (buffer_count[stream][component_id-1] > 20,
    "Too many buffers %d > 20 for component",
    buffer_count[stream][component_id-1], component_id);

  if (buffer_count[0][0] == 20 && buffer_count[0][1] == 20 &&
      buffer_count[1][0] == 20 && buffer_count[1][1] == 20) {
    /* TEST OVER */
    g_atomic_int_set(&running, FALSE);
    g_main_loop_quit (loop);
  }
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

  g_debug ("%p: Stream state for component %u is now %s (%u)", st,
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

  fail_if (state < FS_STREAM_STATE_CONNECTED && state < oldstate,
      "State went in wrong direction %d -> %d for component %u",
      oldstate, state, component);

  g_object_set_data (G_OBJECT (st), prop, GINT_TO_POINTER (state));

  if (state < FS_STREAM_STATE_CONNECTED)
    return;

  if (component == 1)
    prop = "src_setup_1";
  else if (component == 2)
    prop = "src_setup_2";

  if (g_object_get_data (G_OBJECT (trans), prop) == NULL)
  {
    GstElement *pipeline = GST_ELEMENT (
        g_object_get_data (G_OBJECT (trans), "pipeline"));
    g_debug ("%p: Setting up fakesrc for component %u", st, component);
    setup_fakesrc (trans, pipeline, component);
    g_object_set_data (G_OBJECT (trans), prop, "");
  }
  else
    g_debug ("FAKESRC ALREADY SETUP for component %u", component);
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

  loop = g_main_loop_new (NULL, FALSE);

  trans = fs_transmitter_new ("nice", 2, &error);
  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);
  }
  ts_fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  trans2 = fs_transmitter_new ("nice", 2, &error);
  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);
  }
  ts_fail_if (trans2 == NULL, "No transmitter create, yet error is still NULL");


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

  g_main_run (loop);

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

  return s;
}


GST_CHECK_MAIN (nicetransmitter);

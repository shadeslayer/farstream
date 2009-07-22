/* Farsigh2 generic unit tests for transmitters
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
#include <gst/farsight/fs-stream-transmitter.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "check-threadsafe.h"
#include "generic.h"

static void
_transmitter_error (FsTransmitter *transmitter, gint errorno, gchar *error_msg,
  gchar *debug_msg, gpointer user_data)
{
  ts_fail ("Transmitter(%x) error(%d) msg:%s debug:%s", transmitter, errorno,
    error_msg, debug_msg);
}

void
stream_transmitter_error (FsStreamTransmitter *streamtransmitter,
  gint errorno, gchar *error_msg, gchar *debug_msg, gpointer user_data)
{
  ts_fail ("StreamTransmitter(%x) error(%d) msg:%s debug:%s", streamtransmitter,
    errorno, error_msg, debug_msg);
}

void
setup_fakesrc (FsTransmitter *trans, GstElement *pipeline, guint component_id)
{
  GstElement *src;
  GstElement *trans_sink;
  gchar *padname;
  gchar *tmp;

  tmp = g_strdup_printf ("fakemediasrc_%d", component_id);
  src = gst_element_factory_make ("fakesrc", tmp);
  g_free (tmp);
  g_object_set (src,
      "num-buffers", 20,
      "sizetype", 2,
      "sizemax", component_id * 10,
      "is-live", TRUE,
      "filltype", 2,
      NULL);

  /*
   * We lock and unlock the state to prevent the source to start
   * playing before we link it
   */
  gst_element_set_locked_state (src, TRUE);

  ts_fail_unless (gst_bin_add (GST_BIN (pipeline), src),
    "Could not add the fakesrc");

  g_object_get (trans, "gst-sink", &trans_sink, NULL);

  padname = g_strdup_printf ("sink%d", component_id);
  ts_fail_unless (gst_element_link_pads (src, "src", trans_sink, padname),
    "Could not link the fakesrc to %s", padname);
  g_free (padname);

  ts_fail_if (gst_element_set_state (src, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the fakesrc to playing");

  gst_element_set_locked_state (src, FALSE);
  gst_element_sync_state_with_parent (src);

  gst_object_unref (trans_sink);
}

GstElement *
setup_pipeline (FsTransmitter *trans, GCallback cb)
{
  GstElement *pipeline;
  GstElement *rtpfakesink, *rtcpfakesink;
  GstElement *trans_sink, *trans_src;

  ts_fail_unless (g_signal_connect (trans, "error",
      G_CALLBACK (_transmitter_error), NULL), "Could not connect signal");

  pipeline = gst_pipeline_new ("pipeline");
  rtpfakesink = gst_element_factory_make ("fakesink", "rtpfakesink");
  rtcpfakesink = gst_element_factory_make ("fakesink", "rtcpfakesink");

  g_object_get (trans, "gst-sink", &trans_sink, "gst-src", &trans_src, NULL);


  g_object_set (rtpfakesink, "signal-handoffs", TRUE, "sync", FALSE, NULL);
  g_object_set (rtcpfakesink, "signal-handoffs", TRUE, "sync", FALSE,
    "async", FALSE, NULL);

  if (cb) {
    g_signal_connect (rtpfakesink, "handoff", cb, GINT_TO_POINTER (1));
    g_signal_connect (rtcpfakesink, "handoff", cb, GINT_TO_POINTER (2));
  }

  ts_fail_if (trans_sink == NULL, "No transmitter sink");
  ts_fail_if (trans_src == NULL, "No transmitter src");

  gst_bin_add_many (GST_BIN (pipeline), rtpfakesink, rtcpfakesink,
    trans_sink, trans_src, NULL);

  ts_fail_unless (gst_element_link_pads (trans_src, "src1",
      rtpfakesink, "sink"),
    "Coult not link transmitter src and fakesink");
  ts_fail_unless (gst_element_link_pads (trans_src, "src2",
      rtcpfakesink, "sink"),
    "Coult not link transmitter src and fakesink");

  g_object_unref (trans_src);
  g_object_unref (trans_sink);

  return pipeline;
}


gboolean
bus_error_callback (GstBus *bus, GstMessage *message, gpointer user_data)
{
  switch (GST_MESSAGE_TYPE (message))
  {
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

        GST_WARNING ("Got a warning on the BUS (%d): %s (%s)",
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

void
test_transmitter_creation (gchar *transmitter_name)
{
  GError *error = NULL;
  FsTransmitter *trans;
  GstElement *pipeline;
  GstElement *trans_sink, *trans_src;

  trans = fs_transmitter_new (transmitter_name, 2, 0, &error);

  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  ts_fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans, NULL);

  g_object_get (trans, "gst-sink", &trans_sink, "gst-src", &trans_src, NULL);

  fail_if (trans_sink == NULL, "Sink is NULL");
  fail_if (trans_src == NULL, "Src is NULL");

  gst_object_unref (trans_sink);
  gst_object_unref (trans_src);

  g_object_unref (trans);

  gst_object_unref (pipeline);

}


GPid stund_pid = 0;

void
setup_stund (void)
{
  GError *error = NULL;
  gchar *argv[] = {"stund", NULL};

  stund_pid = 0;

  if (!g_spawn_async (NULL, argv, NULL,
          G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
          NULL, NULL, &stund_pid, &error))
  {
    GST_WARNING ("Could not spawn stund, skipping stun testing: %s",
        error->message);
    g_clear_error (&error);
    return;
  }
}

void
teardown_stund (void)
{
  if (!stund_pid)
    return;

  kill (stund_pid, SIGTERM);
  waitpid (stund_pid, NULL, 0);
  g_spawn_close_pid (stund_pid);
  stund_pid = 0;
}


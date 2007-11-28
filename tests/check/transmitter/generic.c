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

#include "generic.h"

#include <gst/check/gstcheck.h>
#include <gst/farsight/fs-transmitter.h>
#include <gst/farsight/fs-stream-transmitter.h>


static void
_transmitter_error (FsTransmitter *transmitter, gint errorno, gchar *error_msg,
  gchar *debug_msg, gpointer user_data)
{
  fail ("Transmitter(%x) error(%d) msg:%s debug:%s", transmitter, errorno,
    error_msg, debug_msg);
}

void
_stream_transmitter_error (FsStreamTransmitter *streamtransmitter,
  gint errorno, gchar *error_msg, gchar *debug_msg, gpointer user_data)
{
  fail ("StreamTransmitter(%x) error(%d) msg:%s debug:%s", streamtransmitter,
    errorno, error_msg, debug_msg);
}

void
setup_fakesrc (FsTransmitter *trans, GstElement *pipeline, guint component_id)
{
  GstElement *src;
  GstElement *trans_sink;
  gchar *padname;

  src = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src, "num-buffers", 20, "sizetype", 2, "sizemax",
    component_id * 10, "is-live", TRUE, NULL);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src),
    "Could not add the fakesrc");

  g_object_get (trans, "gst-sink", &trans_sink, NULL);

  padname = g_strdup_printf ("sink%d", component_id);
  fail_unless (gst_element_link_pads (src, "src", trans_sink, padname),
    "Could not link the fakesrc to %s", padname);
  g_free (padname);

  fail_if (gst_element_set_state (src, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the fakesrc to playing");

  gst_object_unref (trans_sink);
}

GstElement *
setup_pipeline (FsTransmitter *trans, GCallback cb)
{
  GstElement *pipeline;
  GstElement *rtpfakesink, *rtcpfakesink;
  GstElement *trans_sink, *trans_src;

  fail_unless (g_signal_connect (trans, "error",
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

  fail_if (trans_sink == NULL, "No transmitter sink");
  fail_if (trans_src == NULL, "No transmitter src");

  gst_bin_add_many (GST_BIN (pipeline), rtpfakesink, rtcpfakesink,
    trans_sink, trans_src, NULL);

  fail_unless (gst_element_link_pads (trans_src, "src1",
      rtpfakesink, "sink"),
    "Coult not link transmitter src and fakesink");
  fail_unless (gst_element_link_pads (trans_src, "src2",
      rtcpfakesink, "sink"),
    "Coult not link transmitter src and fakesink");

  g_object_unref (trans_src);
  g_object_unref (trans_sink);

  return pipeline;
}

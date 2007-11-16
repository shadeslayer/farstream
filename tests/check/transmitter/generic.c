/* Farsigh2 generic unit tests for transmitters
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

GstElement *
setup_pipeline (FsTransmitter *trans, GstElement **fakesrc)
{
  GstElement *pipeline;
  GstElement *fakesink;
  GstElement *trans_sink, *trans_src;

  fail_unless (g_signal_connect (trans, "error",
      G_CALLBACK (_transmitter_error), NULL), "Could not connect signal");

  pipeline = gst_pipeline_new ("pipeline");
  *fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  g_object_get (trans, "gst-sink", &trans_sink, "gst-src", &trans_src, NULL);

  fail_if (trans_sink == NULL, "No transmitter sink");
  fail_if (trans_src == NULL, "No transmitter src");

  gst_bin_add_many (GST_BIN (pipeline), fakesink, trans_sink, trans_src, NULL);

  fail_unless (gst_element_link (trans_src, fakesink),
    "Coult not link transmitter src and fakesink");

  g_object_unref (trans_src);
  g_object_unref (trans_sink);


  return pipeline;
}

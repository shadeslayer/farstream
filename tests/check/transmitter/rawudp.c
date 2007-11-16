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
  GstElement *trans_sink, *trans_src;

  trans = fs_transmitter_new ("rawudp", &error);

  if (error) {
    fail("Error creating transmitter: %s", error->message);
  }

  fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans);

  g_object_get (trans, "gst-sink", &trans_sink, "gst-src", &trans_src, NULL);

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

  return s;
}


GST_CHECK_MAIN (rawudptransmitter);

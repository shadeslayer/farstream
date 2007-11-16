/* Farsigh2 unit tests for FsTransmitter
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
#include <gst/farsight/fs-conference-iface.h>


GST_START_TEST (test_fstransmitter_new_fail)
{
  GError *error = NULL;
  FsTransmitter *transmitter = NULL;

  transmitter = fs_transmitter_new ("invalidname", &error);

  fail_if (transmitter);

  fail_unless (error != NULL, "Error is NULL");
  fail_unless (error->domain == FS_ERROR, "Error domain is wrong");
}
GST_END_TEST;


static Suite *
fstransmitter_suite (void)
{
  Suite *s = suite_create ("fstransmitter");
  TCase *tc_chain = tcase_create ("fstransmitter");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_fstransmitter_new_fail);

  return s;
}


GST_CHECK_MAIN (fstransmitter);

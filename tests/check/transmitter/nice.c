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


GST_START_TEST (test_nicetransmitter_new)
{
  test_transmitter_creation ("nice");
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
  tcase_set_timeout (tc_chain, 5);
  tcase_add_test (tc_chain, test_nicetransmitter_new);
  suite_add_tcase (s, tc_chain);
  return s;
}


GST_CHECK_MAIN (nicetransmitter);

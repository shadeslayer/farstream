/* Farsigh2 unit tests for FsCodec
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
#include <ext/fsupnp/fs-upnp-simple-igd.h>
#include <ext/fsupnp/fs-upnp-simple-igd-thread.h>



GST_START_TEST (test_fsupnp_new)
{
  FsUpnpSimpleIgd *igd = fs_upnp_simple_igd_new (NULL);
  FsUpnpSimpleIgdThread *igdthread = fs_upnp_simple_igd_thread_new ();
  FsUpnpSimpleIgdThread *igdthread1 = fs_upnp_simple_igd_thread_new ();

  g_object_unref (igd);
  g_object_unref (igdthread);
  g_object_unref (igdthread1);
}
GST_END_TEST;

static Suite *
fsupnp_suite (void)
{
  Suite *s = suite_create ("fsupnp");
  TCase *tc_chain = tcase_create ("fsupnp");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_fsupnp_new);

  return s;
}

GST_CHECK_MAIN (fsupnp);

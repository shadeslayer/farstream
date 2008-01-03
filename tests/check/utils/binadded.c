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
#include <gst/farsight/fs-utils.h>

gboolean called = FALSE;
gpointer last_added = NULL;
gpointer last_bin = NULL;

static void
_added_cb (GstBin *bin, GstElement *element, gpointer user_data)
{
  GstObject *parent = NULL;

  fail_unless (user_data == &last_added, "Pass user_data is incorrect");
  called = TRUE;
  last_added = element;
  last_bin = bin;

  if (bin)
  {
    parent = gst_object_get_parent (GST_OBJECT (element));
    fail_if (GST_OBJECT_CAST (bin) != parent,
        "The bin passed to use is not the right parent");
    gst_object_unref (parent);
  }
}


GST_START_TEST (test_bin_added_simple)
{
  GstElement *pipeline = NULL;
  GstElement *identity = NULL;
  gpointer handle = NULL;

  pipeline = gst_pipeline_new (NULL);

  identity = gst_element_factory_make ("identity", NULL);
  gst_object_ref (identity);

  handle = fs_utils_add_recursive_element_added_notification (
      GST_BIN (pipeline),
      _added_cb, &last_added);

  fail_if (handle == NULL, "Could not add notification to pipeline");

  fail_unless (gst_bin_add (GST_BIN (pipeline), identity),
      "Could not add identity to pipeline");

  fail_if (called == FALSE, "AddedCallback not called");
  fail_unless (last_added == identity,
      "The element passed to the callback was wrong"
      " (it was %p, should have been %p",
      last_added, identity);
  fail_unless (last_bin == pipeline,
      "The bin passed to the callback was wrong"
      " (it was %p, should have been %p",
      last_bin, pipeline);

  fail_unless (gst_bin_remove (GST_BIN (pipeline), identity),
      "Could not remove identity from pipeline");

  called = FALSE;
  last_added = last_bin = NULL;


  fail_unless (
      fs_utils_remove_recursive_element_added_notification (GST_BIN (pipeline),
          handle),
      "Could not remove notification handle %p", handle);

  fail_unless (gst_bin_add (GST_BIN (pipeline), identity),
      "Could not add identity to pipeline");

  fail_if (called == TRUE, "AddedCallback was removed, but was still called");

  called = FALSE;
  last_added = last_bin = NULL;

  gst_object_unref (identity);
  gst_object_unref (pipeline);
}
GST_END_TEST;


GST_START_TEST (test_bin_added_recursive)
{
  GstElement *pipeline = NULL;
  GstElement *bin = NULL;
  GstElement *identity = NULL;
  gpointer handle = NULL;

  pipeline = gst_pipeline_new (NULL);

  bin = gst_bin_new (NULL);
  gst_object_ref (bin);

  gst_bin_add (GST_BIN (pipeline), bin);

  identity = gst_element_factory_make ("identity", NULL);
  gst_object_ref (identity);

  handle = fs_utils_add_recursive_element_added_notification (
      GST_BIN (pipeline),
      _added_cb, &last_added);

  fail_if (handle == NULL, "Could not add notification to bin");

  fail_unless (gst_bin_add (GST_BIN (bin), identity),
      "Could not add identity to bin");

  fail_if (called == FALSE, "AddedCallback not called");
  fail_unless (last_added == identity,
      "The element passed to the callback was wrong"
      " (it was %p, should have been %p",
      last_added, identity);
  fail_unless (last_bin == bin,
      "The bin passed to the callback was wrong"
      " (it was %p, should have been %p",
      last_bin, bin);

  fail_unless (gst_bin_remove (GST_BIN (bin), identity),
      "Could not remove identity from bin");

  called = FALSE;
  last_added = last_bin = NULL;


  fail_unless (
      fs_utils_remove_recursive_element_added_notification (GST_BIN (pipeline),
          handle),
      "Could not remove notification handle %p", handle);

  fail_unless (gst_bin_add (GST_BIN (bin), identity),
      "Could not add identity to bin");

  fail_if (called == TRUE, "AddedCallback was removed, but was still called");

  fail_unless (gst_bin_remove (GST_BIN (bin), identity),
      "Could not remove identity from bin");

  handle = fs_utils_add_recursive_element_added_notification (
      GST_BIN (pipeline),
      _added_cb, &last_added);

  fail_if (handle == NULL, "Could not re-add notification to bin");

  called = FALSE;
  last_added = last_bin = NULL;

  gst_bin_remove (GST_BIN (pipeline), bin);

  fail_unless (gst_bin_add (GST_BIN (bin), identity),
      "Could not add identity to bin");

  fail_if (called == TRUE, "The bin was removed from the pipeline,"
      " but the callback was still called");


  gst_object_unref (identity);
  gst_object_unref (bin);
  gst_object_unref (pipeline);
}
GST_END_TEST;


static Suite *
binadded_suite (void)
{
  Suite *s = suite_create ("binadded");
  TCase *tc_chain = tcase_create ("binadded");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_bin_added_simple);
  tcase_add_test (tc_chain, test_bin_added_recursive);

  return s;
}

GST_CHECK_MAIN (binadded);

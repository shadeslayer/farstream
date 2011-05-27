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
#include <gst/farsight/fs-element-added-notifier.h>

#include "testutils.h"

gboolean called = FALSE;
gpointer last_added = NULL;
gpointer last_bin = NULL;

static void
_added_cb (FsElementAddedNotifier *notifier, GstBin *bin, GstElement *element,
    gpointer user_data)
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
  FsElementAddedNotifier *notifier = NULL;

  pipeline = gst_pipeline_new (NULL);

  identity = gst_element_factory_make ("identity", NULL);
  gst_object_ref (identity);

  notifier = fs_element_added_notifier_new ();

  g_signal_connect (notifier, "element-added",
      G_CALLBACK (_added_cb), &last_added);

  fs_element_added_notifier_add (notifier, GST_BIN (pipeline));

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
      fs_element_added_notifier_remove (notifier, GST_BIN (pipeline)),
      "Could not remove notification");

  fail_unless (gst_bin_add (GST_BIN (pipeline), identity),
      "Could not add identity to pipeline");

  fail_if (called == TRUE, "AddedCallback was removed, but was still called");

  called = FALSE;
  last_added = last_bin = NULL;

  g_object_unref (notifier);
  gst_object_unref (identity);
  gst_object_unref (pipeline);
}
GST_END_TEST;


GST_START_TEST (test_bin_added_recursive)
{
  GstElement *pipeline = NULL;
  GstElement *bin = NULL;
  GstElement *identity = NULL;
  FsElementAddedNotifier *notifier = NULL;

  pipeline = gst_pipeline_new (NULL);

  bin = gst_bin_new (NULL);
  gst_object_ref (bin);

  gst_bin_add (GST_BIN (pipeline), bin);

  identity = gst_element_factory_make ("identity", NULL);
  gst_object_ref (identity);

  notifier = fs_element_added_notifier_new ();

  g_signal_connect (notifier, "element-added",
      G_CALLBACK (_added_cb), &last_added);

  fs_element_added_notifier_add (notifier, GST_BIN (pipeline));

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
      fs_element_added_notifier_remove (notifier, GST_BIN (pipeline)),
      "Could not remove notification");

  fail_unless (gst_bin_add (GST_BIN (bin), identity),
      "Could not add identity to bin");

  fail_if (called == TRUE, "AddedCallback was removed, but was still called");

  fail_unless (gst_bin_remove (GST_BIN (bin), identity),
      "Could not remove identity from bin");

  fs_element_added_notifier_add (notifier, GST_BIN (pipeline));

  called = FALSE;
  last_added = last_bin = NULL;

  gst_bin_remove (GST_BIN (pipeline), bin);

  fail_unless (gst_bin_add (GST_BIN (bin), identity),
      "Could not add identity to bin");

  fail_if (called == TRUE, "The bin was removed from the pipeline,"
      " but the callback was still called");


  g_object_unref (notifier);
  gst_object_unref (identity);
  gst_object_unref (bin);
  gst_object_unref (pipeline);
}
GST_END_TEST;

static void
test_keyfile (FsElementAddedNotifier *notifier)
{
  GstElement *pipeline;
  GstElement *identity = NULL;
  gboolean sync;

  pipeline = gst_pipeline_new (NULL);

  identity = gst_element_factory_make ("identity", NULL);
  gst_object_ref (identity);

  g_object_get (identity, "sync", &sync, NULL);
  fail_unless (sync == FALSE, "sync prop on identity does not start at FALSE");

  fs_element_added_notifier_add (notifier, GST_BIN (pipeline));

  fail_unless (gst_bin_add (GST_BIN (pipeline), identity),
      "Could not add identity to pipeline");

  g_object_get (identity, "sync", &sync, NULL);
  fail_unless (sync == TRUE, "sync prop on identity is not changed to TRUE");


  fail_unless (gst_bin_remove (GST_BIN (pipeline), identity),
      "Could not remove identity from pipeline");

  g_object_set (identity, "sync", FALSE, NULL);

  g_object_get (identity, "sync", &sync, NULL);
  fail_unless (sync == FALSE, "sync prop on identity not reset to FALSE");

  fail_unless (
      fs_element_added_notifier_remove (notifier, GST_BIN (pipeline)),
      "Could not remove notification");

  fail_unless (gst_bin_add (GST_BIN (pipeline), identity),
      "Could not add identity to bin");

  g_object_get (identity, "sync", &sync, NULL);
  fail_if (sync == TRUE, "sync prop on identity changed to TRUE");

  fs_element_added_notifier_add (notifier, GST_BIN (pipeline));

  g_object_get (identity, "sync", &sync, NULL);
  fail_unless (sync == TRUE, "sync prop on identity is not changed to TRUE");

  gst_object_unref (identity);
  gst_object_unref (pipeline);
}


GST_START_TEST (test_bin_keyfile)
{
  GKeyFile *keyfile = g_key_file_new ();
  FsElementAddedNotifier *notifier = NULL;

  g_key_file_set_boolean (keyfile, "identity", "sync", TRUE);
  g_key_file_set_boolean (keyfile, "identity", "invalid-property", TRUE);

  notifier = fs_element_added_notifier_new ();

  fs_element_added_notifier_set_properties_from_keyfile (notifier, keyfile);

  test_keyfile (notifier);
}
GST_END_TEST;

GST_START_TEST (test_bin_file)
{
  FsElementAddedNotifier *notifier = NULL;
  GError *error = NULL;
  gchar *filename = NULL;

  notifier = fs_element_added_notifier_new ();

  fail_if (fs_element_added_notifier_set_properties_from_file (notifier,
          "invalid-filename", &error));
  fail_if (error == NULL);
  fail_unless (error->domain == G_FILE_ERROR);
  g_clear_error (&error);

  filename = get_fullpath ("utils/gstelements.conf");
  fail_unless (fs_element_added_notifier_set_properties_from_file (notifier,
          filename, &error));
  g_free (filename);
  fail_if (error != NULL);

  test_keyfile (notifier);
}
GST_END_TEST;

GST_START_TEST (test_bin_errors)
{
  FsElementAddedNotifier *notifier = NULL;

  g_log_set_always_fatal (0);
  g_log_set_fatal_mask (NULL, 0);

  ASSERT_CRITICAL (fs_element_added_notifier_add (NULL, NULL));
  ASSERT_CRITICAL (fs_element_added_notifier_remove (NULL, NULL));
  ASSERT_CRITICAL (fs_element_added_notifier_set_properties_from_keyfile (
          NULL, NULL));

  notifier = fs_element_added_notifier_new ();

  ASSERT_CRITICAL (fs_element_added_notifier_add (notifier, NULL));
  ASSERT_CRITICAL (fs_element_added_notifier_remove (notifier, NULL));
  ASSERT_CRITICAL (fs_element_added_notifier_set_properties_from_keyfile (
          notifier, NULL));

  g_object_unref (notifier);
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
  tcase_add_test (tc_chain, test_bin_keyfile);
  tcase_add_test (tc_chain, test_bin_file);
  tcase_add_test (tc_chain, test_bin_errors);

  return s;
}

GST_CHECK_MAIN (binadded);

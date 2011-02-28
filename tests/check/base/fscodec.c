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
#include "gst/farsight/fs-codec.h"
#include "gst/farsight/fs-rtp.h"

#include "testutils.h"

GST_START_TEST (test_fscodec_new)
{
  FsCodec *codec = NULL;

  codec = fs_codec_new (1, "aa", FS_MEDIA_TYPE_VIDEO, 650);

  fail_if (codec == NULL, "Allocation failed");

  fail_unless (codec->id == 1, "Codec is incorrect");
  fail_unless (!strcmp (codec->encoding_name, "aa"),
      "Codec encoding name incorrect");
  fail_unless (codec->media_type == FS_MEDIA_TYPE_VIDEO,
      "Codec media type incorrect");
  fail_unless (codec->clock_rate == 650, "Codec clock rate incorrect");

  fs_codec_destroy (codec);
}
GST_END_TEST;


GST_START_TEST (test_fscodec_are_equal)
{
  FsCodec *codec1 = fs_codec_new (1, "aa", FS_MEDIA_TYPE_VIDEO, 650);
  FsCodec *codec2 = fs_codec_new (1, "aa", FS_MEDIA_TYPE_VIDEO, 650);

  fail_unless (fs_codec_are_equal (codec1, codec2) == TRUE,
      "Identical codecs not recognized");

  fs_codec_destroy (codec2);

  codec2 = fs_codec_new (2, "aa", FS_MEDIA_TYPE_VIDEO, 650);
  fail_unless (fs_codec_are_equal (codec1, codec2) == FALSE,
      "Different codec ids not recognized");
  fs_codec_destroy (codec2);

  codec2 = fs_codec_new (1, "aaa", FS_MEDIA_TYPE_VIDEO, 650);
  fail_unless (fs_codec_are_equal (codec1, codec2) == FALSE,
      "Different codec types not recognized");
  fs_codec_destroy (codec2);

  codec2 = fs_codec_new (1, "aa", FS_MEDIA_TYPE_AUDIO, 650);
  fail_unless (fs_codec_are_equal (codec1, codec2) == FALSE,
      "Different media types not recognized");
  fs_codec_destroy (codec2);

  codec2 = fs_codec_new (1, "aa", FS_MEDIA_TYPE_VIDEO, 651);
  fail_unless (fs_codec_are_equal (codec1, codec2) == FALSE,
      "Different clock rates not recognized");
  fs_codec_destroy (codec2);

  codec2 = fs_codec_new (1, NULL, FS_MEDIA_TYPE_VIDEO, 650);
  fail_unless (fs_codec_are_equal (codec1, codec2) == FALSE,
      "NULL encoding name not ignored");
  fs_codec_destroy (codec2);

  fs_codec_destroy (codec1);
}
GST_END_TEST;

static FsCodec *
init_codec_with_three_params (void)
{
  FsCodec *codec = fs_codec_new (1, "aa", FS_MEDIA_TYPE_VIDEO, 650);

  fs_codec_add_optional_parameter (codec, "aa1", "bb1");
  fs_codec_add_optional_parameter (codec, "aa2", "bb2");
  fs_codec_add_optional_parameter (codec, "aa3", "bb3");

  fs_codec_add_feedback_parameter (codec, "aa1", "bb1", "cc1");
  fs_codec_add_feedback_parameter (codec, "aa2", "bb2", "cc2");
  fs_codec_add_feedback_parameter (codec, "aa3", "bb3", "cc3");

  codec->ABI.ABI.ptime = 12;
  codec->ABI.ABI.maxptime = 12;

  return codec;
}

GST_START_TEST (test_fscodec_are_equal_opt_params)
{
  FsCodec *codec1;
  FsCodec *codec2;

  codec1 = init_codec_with_three_params ();
  codec2 = init_codec_with_three_params ();

  fail_unless (fs_codec_are_equal (codec1, codec2) == TRUE,
      "Identical codecs (with params) not recognized");

  fs_codec_remove_optional_parameter (codec1,
      g_list_first (codec1->optional_params)->data);
  fs_codec_add_optional_parameter (codec1, "aa1", "bb1");

  fail_unless (fs_codec_are_equal (codec1, codec2) == TRUE,
      "Identical codecs (with params in different order 1) not recognized");

  fs_codec_remove_optional_parameter (codec1,
      g_list_first (codec1->optional_params)->data);
  fs_codec_add_optional_parameter (codec1, "aa2", "bb2");

  fail_unless (fs_codec_are_equal (codec1, codec2) == TRUE,
      "Identical codecs (with params in different order 2) not recognized");

  fs_codec_destroy (codec1);

  codec1 = init_codec_with_three_params ();

  fs_codec_remove_optional_parameter (codec1,
      g_list_first (codec1->optional_params)->data);

  fail_unless (fs_codec_are_equal (codec1, codec2) == FALSE,
      "Did not detect removal of first parameter of first codec");
  fail_unless (fs_codec_are_equal (codec2, codec1) == FALSE,
      "Did not detect removal of first parameter of second codec");

  fs_codec_destroy (codec1);

  codec1 = init_codec_with_three_params ();
  fs_codec_remove_optional_parameter (codec1,
      g_list_last (codec1->optional_params)->data);

  fail_unless (fs_codec_are_equal (codec1, codec2) == FALSE,
      "Did not detect removal of last parameter of first codec");
  fail_unless (fs_codec_are_equal (codec2, codec1) == FALSE,
      "Did not detect removal of last parameter of second codec");

  fs_codec_destroy (codec1);
  fs_codec_destroy (codec2);
}
GST_END_TEST;


GST_START_TEST (test_fscodec_are_equal_feedback_params)
{
  FsCodec *codec1;
  FsCodec *codec2;

  codec1 = init_codec_with_three_params ();
  codec2 = init_codec_with_three_params ();

  fail_unless (fs_codec_are_equal (codec1, codec2) == TRUE,
      "Identical codecs (with params) not recognized");

  fs_codec_remove_feedback_parameter (codec1,
      g_list_first (codec1->ABI.ABI.feedback_params));
  fs_codec_add_feedback_parameter (codec1, "aa1", "bb1", "cc1");

  fail_unless (fs_codec_are_equal (codec1, codec2) == TRUE,
      "Identical codecs (with params in different order 1) not recognized");

  fs_codec_remove_feedback_parameter (codec1,
      g_list_first (codec1->ABI.ABI.feedback_params));
  fs_codec_add_feedback_parameter (codec1, "aa2", "bb2", "cc2");

  fail_unless (fs_codec_are_equal (codec1, codec2) == TRUE,
      "Identical codecs (with params in different order 2) not recognized");

  fs_codec_destroy (codec1);

  codec1 = init_codec_with_three_params ();

  fs_codec_remove_feedback_parameter (codec1,
      g_list_first (codec1->ABI.ABI.feedback_params));

  fail_unless (fs_codec_are_equal (codec1, codec2) == FALSE,
      "Did not detect removal of first parameter of first codec");
  fail_unless (fs_codec_are_equal (codec2, codec1) == FALSE,
      "Did not detect removal of first parameter of second codec");

  fs_codec_destroy (codec1);

  codec1 = init_codec_with_three_params ();
  fs_codec_remove_feedback_parameter (codec1,
      g_list_last (codec1->ABI.ABI.feedback_params));

  fail_unless (fs_codec_are_equal (codec1, codec2) == FALSE,
      "Did not detect removal of last parameter of first codec");
  fail_unless (fs_codec_are_equal (codec2, codec1) == FALSE,
      "Did not detect removal of last parameter of second codec");

  fs_codec_destroy (codec1);
  fs_codec_destroy (codec2);
}
GST_END_TEST;


GST_START_TEST (test_fscodec_copy)
{
  FsCodec *codec1 = init_codec_with_three_params ();
  FsCodec *codec2 = NULL;

  codec2 = fs_codec_copy (codec1);

  fail_unless (fs_codec_are_equal (codec1, codec2) == TRUE,
      "Copy is not identical to the original");

  fs_codec_destroy (codec1);
  fs_codec_destroy (codec2);
}
GST_END_TEST;

GST_START_TEST (test_fscodec_null)
{
  gchar *str;

  fs_codec_destroy (NULL);
  fail_unless (fs_codec_copy (NULL) == NULL, "Failed to copy NULL codec");
  fs_codec_list_destroy (NULL);
  fail_unless (fs_codec_list_copy (NULL) == NULL,
      "Failed to copy NULL codec list");
  str = fs_codec_to_string (NULL);
  fail_unless (str && !strcmp (str, "(NULL)"),
      "Failed to print NULL codec");
  g_free (str);
  fail_unless (fs_codec_are_equal (NULL,NULL), "NULL codecs are not equal");
}
GST_END_TEST;

GST_START_TEST (test_fscodec_keyfile)
{
  GList *codecs = NULL;
  GError *error = NULL;
  gchar *filename = NULL;
  GList *comparison = NULL;
  FsCodec *codec = NULL;

  fail_if (fs_codec_list_from_keyfile ("invalid-filename", &error));
  fail_if (error == NULL);
  fail_unless (error->domain == G_FILE_ERROR);
  g_clear_error (&error);

  filename = get_fullpath ("base/test1.conf");
  codecs = fs_codec_list_from_keyfile (filename, &error);
  g_free (filename);
  fail_unless (error == NULL);
  fail_if (codecs == NULL);

#if 0
  {
    GList *item;
    for(item = codecs; item ; item= item->next)
    {
      g_debug("%s", fs_codec_to_string (item->data));
    }
  }
#endif

  codec = fs_codec_new (122, "TEST1", FS_MEDIA_TYPE_AUDIO, 8001);
  codec->channels = 5;
  fs_codec_add_optional_parameter (codec, "test3", "test4");
  fs_codec_add_feedback_parameter (codec, "aa", "bb", "cc");
  fs_codec_add_feedback_parameter (codec, "dd", "ee", "");
  fs_codec_add_feedback_parameter (codec, "ff", "", "");
  comparison = g_list_append (comparison, codec);

  codec = fs_codec_new (123, "TEST2", FS_MEDIA_TYPE_VIDEO, 8002);
  codec->channels = 6;
  codec->ABI.ABI.maxptime = 12;
  codec->ABI.ABI.ptime = 13;
  fs_codec_add_optional_parameter (codec, "test5", "test6");
  comparison = g_list_append (comparison, codec);

  codec = fs_codec_new (FS_CODEC_ID_ANY, "TEST3", FS_MEDIA_TYPE_AUDIO, 0);
  comparison = g_list_append (comparison, codec);

  codec = fs_codec_new (FS_CODEC_ID_DISABLE, "TEST4", FS_MEDIA_TYPE_AUDIO, 0);
  comparison = g_list_append (comparison, codec);

  codec = fs_codec_new (FS_CODEC_ID_ANY, "TEST5", FS_MEDIA_TYPE_AUDIO, 0);
  comparison = g_list_append (comparison, codec);

  codec = fs_codec_new (124, "TEST5", FS_MEDIA_TYPE_AUDIO, 0);
  comparison = g_list_append (comparison, codec);

  fail_unless (fs_codec_list_are_equal (codecs, comparison));

  fs_codec_list_destroy (comparison);
  fs_codec_list_destroy (codecs);

}
GST_END_TEST;

GST_START_TEST (test_fscodec_rtp_hdrext)
{
  FsRtpHeaderExtension *hdrext, *hdrext2;

  hdrext = fs_rtp_header_extension_new (1, FS_DIRECTION_BOTH, "uri");
  hdrext2 = fs_rtp_header_extension_new (1, FS_DIRECTION_BOTH, "uri");

  fail_unless (fs_rtp_header_extension_are_equal (hdrext, hdrext));
  fail_unless (fs_rtp_header_extension_are_equal (hdrext, hdrext2));

  hdrext2->id = 2;
  fail_unless (!fs_rtp_header_extension_are_equal (hdrext, hdrext2));

  hdrext2->id = 1;
  fail_unless (fs_rtp_header_extension_are_equal (hdrext, hdrext2));

  hdrext2->direction = FS_DIRECTION_NONE;
  fail_unless (!fs_rtp_header_extension_are_equal (hdrext, hdrext2));
  fs_rtp_header_extension_destroy (hdrext2);

  hdrext2 = fs_rtp_header_extension_copy (hdrext);

  fail_unless (fs_rtp_header_extension_are_equal (hdrext, hdrext2));

  fs_rtp_header_extension_destroy (hdrext2);
  fs_rtp_header_extension_destroy (hdrext);
}
GST_END_TEST;


GST_START_TEST (test_fscodec_rtp_hdrext_keyfile)
{
  GList *extensions = NULL;
  GError *error = NULL;
  gchar *filename = NULL;
  FsRtpHeaderExtension *comparison;

  fail_if (fs_rtp_header_extension_list_from_keyfile ("invalid-filename",
          FS_MEDIA_TYPE_AUDIO, &error));
  fail_if (error == NULL);
  fail_unless (error->domain == G_FILE_ERROR);
  g_clear_error (&error);

  filename = get_fullpath ("base/test1.conf");
  extensions = fs_rtp_header_extension_list_from_keyfile (filename,
      FS_MEDIA_TYPE_AUDIO, &error);
  g_free (filename);
  fail_unless (error == NULL);
  fail_if (extensions == NULL);

  comparison = fs_rtp_header_extension_new (1, FS_DIRECTION_BOTH,
      "http://example.com/rtp-hdrext1");
  fail_unless (fs_rtp_header_extension_are_equal (extensions->data,
          comparison));
  fs_rtp_header_extension_destroy (comparison);

  comparison = fs_rtp_header_extension_new (2, FS_DIRECTION_RECV,
      "http://example.com/rtp-hdrext2");
  fail_unless (fs_rtp_header_extension_are_equal (extensions->next->data,
          comparison));
  fs_rtp_header_extension_destroy (comparison);

  fail_unless (extensions->next->next == NULL);

  fs_rtp_header_extension_list_destroy (extensions);



  filename = get_fullpath ("base/test1.conf");
  extensions = fs_rtp_header_extension_list_from_keyfile (filename,
      FS_MEDIA_TYPE_VIDEO, &error);
  g_free (filename);
  fail_unless (error == NULL);
  fail_if (extensions == NULL);

  comparison = fs_rtp_header_extension_new (1, FS_DIRECTION_BOTH,
      "http://example.com/rtp-hdrext1");
  fail_unless (fs_rtp_header_extension_are_equal (extensions->data,
          comparison));
  fs_rtp_header_extension_destroy (comparison);

  fail_unless (extensions->next == NULL);
  fs_rtp_header_extension_list_destroy (extensions);
}
GST_END_TEST;

static Suite *
fscodec_suite (void)
{
  Suite *s = suite_create ("fscodec");
  TCase *tc_chain = tcase_create ("fscodec");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_fscodec_new);
  tcase_add_test (tc_chain, test_fscodec_are_equal);
  tcase_add_test (tc_chain, test_fscodec_are_equal_opt_params);
  tcase_add_test (tc_chain, test_fscodec_are_equal_feedback_params);
  tcase_add_test (tc_chain, test_fscodec_copy);
  tcase_add_test (tc_chain, test_fscodec_null);
  tcase_add_test (tc_chain, test_fscodec_keyfile);
  tcase_add_test (tc_chain, test_fscodec_rtp_hdrext);
  tcase_add_test (tc_chain, test_fscodec_rtp_hdrext_keyfile);

  return s;
}


GST_CHECK_MAIN (fscodec);

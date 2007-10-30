/* Farsigh2 unit tests for FsCodec
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
#include <gst/farsight/fs-codec.h>


GST_START_TEST (test_fscodec_new)
{
  FsCodec *codec = NULL;

  codec = fs_codec_new (1, "aa", FS_MEDIA_TYPE_AV, 650);

  fail_if (codec == NULL, "Allocation failed");

  fail_unless (codec->id == 1, "Codec is incorrect");
  fail_unless (!strcmp (codec->encoding_name, "aa"),
      "Codec encoding name incorrect");;
  fail_unless (codec->media_type == FS_MEDIA_TYPE_AV,
      "Codec media type incorrect");
  fail_unless (codec->clock_rate == 650, "Codec clock rate incorrect");

  fs_codec_destroy (codec);
}
GST_END_TEST;


GST_START_TEST (test_fscodec_compare)
{
  FsCodec *codec1 = fs_codec_new (1, "aa", FS_MEDIA_TYPE_AV, 650);
  FsCodec *codec2 = fs_codec_new (1, "aa", FS_MEDIA_TYPE_AV, 650);

  fail_unless (fs_codec_compare (codec1, codec2) == TRUE,
      "Identical codecs not recognized");

  fs_codec_destroy (codec2);

  codec2 = fs_codec_new (2, "aa", FS_MEDIA_TYPE_AV, 650);
  fail_unless (fs_codec_compare (codec1, codec2) == FALSE,
      "Different codec ids not recognized");
  fs_codec_destroy (codec2);

  codec2 = fs_codec_new (1, "aaa", FS_MEDIA_TYPE_AV, 650);
  fail_unless (fs_codec_compare (codec1, codec2) == FALSE,
      "Different codec types not recognized");
  fs_codec_destroy (codec2);

  codec2 = fs_codec_new (1, "aa", FS_MEDIA_TYPE_VIDEO, 650);
  fail_unless (fs_codec_compare (codec1, codec2) == FALSE,
      "Different media types not recognized");
  fs_codec_destroy (codec2);

  codec2 = fs_codec_new (1, "aa", FS_MEDIA_TYPE_AV, 651);
  fail_unless (fs_codec_compare (codec1, codec2) == FALSE,
      "Different clock rates not recognized");
  fs_codec_destroy (codec2);
}
GST_END_TEST;

static FsCodec *
init_codec_with_three_params (void)
{
  FsCodec *codec = fs_codec_new (1, "aa", FS_MEDIA_TYPE_AV, 650);
  FsCodecParameter *p1 = NULL;

  p1 = g_new0 (FsCodecParameter, 1);
  p1->name = g_strdup ("aa1");
  p1->value = g_strdup ("bb1");
  codec->optional_params = g_list_append (codec->optional_params, p1);

  p1 = g_new0 (FsCodecParameter, 1);
  p1->name = g_strdup ("aa2");
  p1->value = g_strdup ("bb2");
  codec->optional_params = g_list_append (codec->optional_params, p1);

  p1 = g_new0 (FsCodecParameter, 1);
  p1->name = g_strdup ("aa3");
  p1->value = g_strdup ("bb3");
  codec->optional_params = g_list_append (codec->optional_params, p1);

  return codec;
}

GST_START_TEST (test_fscodec_compare_opt_params)
{
  FsCodec *codec1;
  FsCodec *codec2;
  FsCodecParameter *p1 = NULL;

  codec1 = init_codec_with_three_params ();
  codec2 = init_codec_with_three_params ();

  fail_unless (fs_codec_compare (codec1, codec2) == TRUE,
      "Identical codecs (with params) not recognized");

  codec1->optional_params = g_list_remove (codec1->optional_params,
      g_list_first (codec1->optional_params)->data);

  p1 = g_new0 (FsCodecParameter, 1);
  p1->name = g_strdup ("aa1");
  p1->value = g_strdup ("bb1");
  codec1->optional_params = g_list_append (codec1->optional_params, p1);

  fail_unless (fs_codec_compare (codec1, codec2) == TRUE,
      "Identical codecs (with params in different order 1) not recognized");

 codec1->optional_params = g_list_remove (codec1->optional_params,
      g_list_first (codec1->optional_params)->data);

  p1 = g_new0 (FsCodecParameter, 1);
  p1->name = g_strdup ("aa2");
  p1->value = g_strdup ("bb2");
  codec1->optional_params = g_list_append (codec1->optional_params, p1);

  fail_unless (fs_codec_compare (codec1, codec2) == TRUE,
      "Identical codecs (with params in different order 2) not recognized");

  fs_codec_destroy (codec1);

  codec1 = init_codec_with_three_params ();

  codec1->optional_params = g_list_remove (codec1->optional_params,
      g_list_first (codec1->optional_params)->data);

 fail_unless (fs_codec_compare (codec1, codec2) == FALSE,
     "Did not detect removal of first parameter of first codec");
 fail_unless (fs_codec_compare (codec2, codec1) == FALSE,
     "Did not detect removal of first parameter of second codec");

 fs_codec_destroy (codec1);

 codec1 = init_codec_with_three_params ();
 codec1->optional_params = g_list_remove (codec1->optional_params,
     g_list_last (codec1->optional_params)->data);

 fail_unless (fs_codec_compare (codec1, codec2) == FALSE,
     "Did not detect removal of last parameter of first codec");
 fail_unless (fs_codec_compare (codec2, codec1) == FALSE,
     "Did not detect removal of last parameter of second codec");

 fs_codec_destroy (codec1);


}
GST_END_TEST;

static Suite *
fscodec_suite (void)
{
  Suite *s = suite_create ("fscodec");
  TCase *tc_chain = tcase_create ("fscodec");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_fscodec_new);
  tcase_add_test (tc_chain, test_fscodec_compare);
  tcase_add_test (tc_chain, test_fscodec_compare_opt_params);

  return s;
}


GST_CHECK_MAIN (fscodec);

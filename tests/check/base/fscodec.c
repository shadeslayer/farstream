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


GST_START_TEST (test_fscodec_init)
{
  FsCodec codec;

  fs_codec_init (&codec, 1, "aa", FS_MEDIA_TYPE_AV, 650);

  fail_unless (codec.id == 1, "Codec is incorrect");
  fail_unless (!strcmp (codec.encoding_name, "aa"),
      "Codec encoding name incorrect");;
  fail_unless (codec.media_type == FS_MEDIA_TYPE_AV,
      "Codec media type incorrect");
  fail_unless (codec.clock_rate == 650, "Codec clock rate incorrect");

}

GST_END_TEST;


GST_START_TEST (test_fscodec_compare)
{
  FsCodec codec1;
  FsCodec codec2;

}

GST_END_TEST;

static Suite *
fscodec_suite (void)
{
  Suite *s = suite_create ("fscodec");
  TCase *tc_chain = tcase_create ("fscodec");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_fscodec_init);
  tcase_add_test (tc_chain, test_fscodec_compare);

  return s;
}


GST_CHECK_MAIN (fscodec);

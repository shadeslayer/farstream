/* Farstream unit tests for the fsrtcpfilter
 *
 * Copyright (C) 2008 Collabora, Nokia
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

#include <gst/rtp/gstrtcpbuffer.h>

static GstBuffer *
make_buffer (GstCaps *caps, gboolean have_sr, gint rr_count,
    gboolean have_sdes, gboolean have_bye)
{
  GstRTCPPacket packet;
  GstBuffer *buf = gst_rtcp_buffer_new (1024);
  gint i;

  gst_buffer_set_caps (buf, caps);
  if (have_sr)
  {
    gst_rtcp_buffer_add_packet (buf, GST_RTCP_TYPE_SR, &packet);
    gst_rtcp_packet_sr_set_sender_info (&packet, 132132, 12, 12, 12, 12);
  }

  if (rr_count >= 0 || !have_sr)
  {
    gst_rtcp_buffer_add_packet (buf, GST_RTCP_TYPE_RR, &packet);
    gst_rtcp_packet_rr_set_ssrc (&packet, 132132);
    for (i = 0; i < rr_count; i++)
      gst_rtcp_packet_add_rb (&packet, 123124+i, 12, 12, 21, 31, 41, 12);
  }

  if (have_sdes)
  {
    gst_rtcp_buffer_add_packet (buf, GST_RTCP_TYPE_SDES, &packet);
    gst_rtcp_packet_sdes_add_item (&packet, 123121);
    gst_rtcp_packet_sdes_add_entry (&packet, GST_RTCP_SDES_EMAIL,
        10, (guint8 *) "aa@aaa.com");
    gst_rtcp_packet_sdes_add_entry (&packet, GST_RTCP_SDES_CNAME,
        10, (guint8 *) "aa@bbb.com");
    gst_rtcp_packet_sdes_add_entry (&packet, GST_RTCP_SDES_PHONE,
        10, (guint8 *) "11-21-2-11");
  }

  if (have_bye)
  {
    gst_rtcp_buffer_add_packet (buf, GST_RTCP_TYPE_BYE, &packet);
    gst_rtcp_packet_bye_add_ssrc (&packet, 132123);
    gst_rtcp_packet_bye_set_reason (&packet, "allo");
  }
  gst_rtcp_buffer_end (buf);

  return buf;
}

GST_START_TEST (test_rtcpfilter)
{
  GList *in_buffers = NULL;
  GList *out_buffers = NULL;
  GstBuffer *buf = NULL;
  GstCaps *caps = gst_caps_new_simple ("application/x-rtcp", NULL);
  gint i;

 for (i = 0; i < 3; i++)
  {
    buf = make_buffer (caps, FALSE, i, FALSE, FALSE);
    in_buffers = g_list_append (in_buffers, gst_buffer_ref (buf));
    out_buffers = g_list_append (out_buffers, buf);

    buf = make_buffer (caps, FALSE, i, TRUE, FALSE);
    in_buffers = g_list_append (in_buffers, gst_buffer_ref (buf));
    out_buffers = g_list_append (out_buffers, buf);

    buf = make_buffer (caps, FALSE, i, TRUE, TRUE);
    in_buffers = g_list_append (in_buffers, gst_buffer_ref (buf));
    out_buffers = g_list_append (out_buffers, buf);
  }

  for (i = -1; i < 3; i++)
  {

    in_buffers = g_list_append (in_buffers,
        make_buffer (caps, TRUE, i, FALSE, FALSE));
    out_buffers = g_list_append (out_buffers,
        make_buffer (caps, FALSE, i, FALSE, FALSE));

    in_buffers = g_list_append (in_buffers,
        make_buffer (caps, TRUE, i, TRUE, FALSE));
    out_buffers = g_list_append (out_buffers,
        make_buffer (caps, FALSE, i, TRUE, FALSE));


    in_buffers = g_list_append (in_buffers,
        make_buffer (caps, TRUE, i, TRUE, TRUE));
    out_buffers = g_list_append (out_buffers,
        make_buffer (caps, FALSE, i, TRUE, TRUE));
  }



  gst_check_element_push_buffer_list ("fsrtcpfilter", in_buffers, out_buffers,
      GST_FLOW_OK);

  gst_caps_unref (caps);
}
GST_END_TEST;

static Suite *
rtcpfilter_suite (void)
{
  Suite *s = suite_create ("rtcpfilter");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);

  tc_chain = tcase_create ("rtcpfilter");
  tcase_add_test (tc_chain, test_rtcpfilter);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (rtcpfilter);


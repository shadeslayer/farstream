/*
 * Farsight2 - Farsight RTP Rate Control
 *
 * Copyright 2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2010 Nokia Corp.
 *
 * fs-rtp-tfrc.c - Rate control for Farsight RTP sessions
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
#include "config.h"
#endif

#include "fs-rtp-tfrc.h"

#include "fs-rtp-packet-modder.h"

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>


GST_DEBUG_CATEGORY_STATIC (fsrtpconference_tfrc);
#define GST_CAT_DEFAULT fsrtpconference_tfrc

G_DEFINE_TYPE (FsRtpTfrc, fs_rtp_tfrc, GST_TYPE_OBJECT);

static void fs_rtp_tfrc_dispose (GObject *object);
static void fs_rtp_tfrc_finalize (GObject *object);

static void fs_rtp_tfrc_update_sender_timer_locked (
  FsRtpTfrc *self,
  struct TrackedSource *src,
  guint now);


static void
fs_rtp_tfrc_class_init (FsRtpTfrcClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = fs_rtp_tfrc_dispose;
  gobject_class->finalize = fs_rtp_tfrc_finalize;
}


static void
free_source (struct TrackedSource *src)
{
  g_object_unref (src->rtpsource);

  if (src->sender_id)
    gst_clock_id_unschedule (src->sender_id);

  if (src->sender)
    tfrc_sender_free (src->sender);
  if (src->receiver)
    tfrc_receiver_free (src->receiver);

  g_slice_free (struct TrackedSource, src);
}

static void
fs_rtp_tfrc_init (FsRtpTfrc *self)
{
  GST_DEBUG_CATEGORY_INIT (fsrtpconference_tfrc,
      "fsrtpconference_tfrc", 0,
      "Farsight RTP Conference Element Rate Control logic");

  /* member init */

  self->tfrc_sources = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) free_source);
}


static void
fs_rtp_tfrc_dispose (GObject *object)
{
  FsRtpTfrc *self = FS_RTP_TFRC (object);

  if (self->in_rtp_probe_id)
    g_signal_handler_disconnect (self->in_rtp_pad, self->in_rtp_probe_id);
  self->in_rtp_probe_id = 0;
  if (self->in_rtcp_probe_id)
    g_signal_handler_disconnect (self->in_rtcp_pad, self->in_rtcp_probe_id);
  self->in_rtcp_probe_id = 0;

  if (self->tfrc_sources)
    g_hash_table_unref (self->tfrc_sources);
  self->tfrc_sources = NULL;

  if (self->packet_modder)
    g_object_unref (self->packet_modder);

  gst_object_unref (self->systemclock);
  self->systemclock = NULL;
}

static void
fs_rtp_tfrc_finalize (GObject *object)
{
}

static void
rtpsession_on_ssrc_validated (GObject *rtpsession, GObject *source,
    FsRtpTfrc *self)
{
  struct TrackedSource *src = NULL;
  guint32 ssrc;

  g_object_get (source, "ssrc", &ssrc, NULL);

  GST_OBJECT_LOCK (self);
  src = g_hash_table_lookup (self->tfrc_sources, GUINT_TO_POINTER (ssrc));

  if (src)
    goto out;

  src = g_slice_new0 (struct TrackedSource);

  src->self = self;
  src->ssrc = ssrc;
  src->rtpsource = g_object_ref (source);

  g_hash_table_insert (self->tfrc_sources, GUINT_TO_POINTER (ssrc), src);

out:
  GST_OBJECT_UNLOCK (self);
}

static guint
fs_rtp_tfrc_get_now (FsRtpTfrc *self)
{
  return GST_TIME_AS_MSECONDS (gst_clock_get_time (self->systemclock));
}

static gboolean
rtpsession_sending_rtcp (GObject *rtpsession, GstBuffer *buffer,
    gboolean is_early, FsRtpTfrc *self)
{

  /* Return TRUE if something was added */
  return FALSE;
}

static gboolean
incoming_rtp_probe (GstPad *pad, GstBuffer *buffer, FsRtpTfrc *self)
{
  return TRUE;
}
static gboolean
no_feedback_timer_expired (GstClock *clock, GstClockTime time, GstClockID id,
  gpointer user_data)
{
  struct TrackedSource *src = user_data;
  guint now = GST_TIME_AS_MSECONDS (time);

  if (time == GST_CLOCK_TIME_NONE)
    return FALSE;

  GST_OBJECT_LOCK (src->self);

  fs_rtp_tfrc_update_sender_timer_locked (src->self, src, now);

  GST_OBJECT_UNLOCK (src->self);

  return FALSE;
}

static void
fs_rtp_tfrc_update_sender_timer_locked (FsRtpTfrc *self,
    struct TrackedSource *src, guint now)
{
  guint expiry;
  GstClockReturn cret;

  if (src->sender_id)
    gst_clock_id_unschedule (src->sender_id);
  src->sender_id = NULL;

  for (;;)
  {
    expiry = tfrc_sender_get_no_feedback_timer_expiry (src->sender);

    if (expiry <= now)
      tfrc_sender_no_feedback_timer_expired (src->sender, now);
    else
      break;
  }


  src->sender_id = gst_clock_new_single_shot_id (self->systemclock,
      expiry * GST_MSECOND);

  cret = gst_clock_id_wait_async (src->sender_id, no_feedback_timer_expired,
      src);
  if (cret != GST_CLOCK_OK)
  {
    GST_ERROR ("Could not schedule feedback time for %u (now %u) error: %d",
        expiry, now, cret);
  }
}

static gboolean
incoming_rtcp_probe (GstPad *pad, GstBuffer *buffer, FsRtpTfrc *self)
{
  GstRTCPPacket packet;


  if (!gst_rtcp_buffer_validate (buffer))
    goto out;

  if (!gst_rtcp_buffer_get_first_packet (buffer, &packet))
    goto out;

  do {
    if (gst_rtcp_packet_get_type (&packet) == GST_RTCP_TYPE_RTPFB &&
        gst_rtcp_packet_fb_get_type (&packet) == 2 &&
        gst_rtcp_packet_get_length (&packet) == 6)
    {
      /* We have a TFRC packet */
      guint32 media_source_ssrc;
      guint32 ts;
      guint32 delay;
      guint32 x_recv;
      guint32 loss_event_rate;
      guint8 *buf = GST_BUFFER_DATA (packet.buffer) + packet.offset;
      struct TrackedSource *src;
      guint now, rtt;

      media_source_ssrc = gst_rtcp_packet_fb_get_media_ssrc (&packet);

      buf += 4 * 3; /* skip the header, ssrc of sender and media sender */

      ts = GST_READ_UINT32_BE (buf);
      buf += 4;
      delay = GST_READ_UINT32_BE (buf);
      buf += 4;
      x_recv = GST_READ_UINT32_BE (buf);
      buf += 4;
      loss_event_rate = GST_READ_UINT32_BE (buf);

      GST_LOG ("Got RTCP TFRC packet last_sent_ts: %u delay: %u x_recv: %u"
          " loss_event_rate: %u", ts, delay, x_recv, loss_event_rate);

      GST_OBJECT_LOCK (self);

      src = g_hash_table_lookup (self->tfrc_sources,
          GUINT_TO_POINTER (media_source_ssrc));
      if (!src)
      {
        GST_WARNING ("Could not find tracked source for ssrc %X",
            media_source_ssrc);
        goto done;
      }

      now = fs_rtp_tfrc_get_now (self);

      if (ts >= now || now - ts < delay)
      {
        GST_DEBUG ("Ignoring packet because ts >= now || now - ts < delay"
            "(ts: %u now: %u delay:%u", ts, now, delay);
        goto done;
      }

      rtt = now - ts - delay;

      src->rtt = rtt;

      GST_LOG ("rtt: %s", rtt);

      if (!src->sender)
      {
        src->sender = tfrc_sender_new (1460, now);
        tfrc_sender_on_first_rtt (src->sender, now);
      }

      tfrc_sender_on_feedback_packet (src->sender, now, rtt, x_recv,
          loss_event_rate, TRUE /* is data limited */);

      fs_rtp_tfrc_update_sender_timer_locked (self, src, now);

    done:
      GST_OBJECT_UNLOCK (self);


    }
  } while (gst_rtcp_packet_move_to_next (&packet));

out:
  return TRUE;
}

static GstMiniObject *
fs_rtp_tfrc_outgoing_packets (FsRtpPacketModder *modder,
    GstMiniObject *buffer_or_list, gpointer user_data)
{
  FsRtpTfrc *self = FS_RTP_TFRC (user_data);
  GstBufferList *list;
  GstBufferListIterator *it;
  gchar data[7];

  GST_OBJECT_LOCK (self);

  if (self->extension_type == EXTENSION_NONE ||
    self->last_src == NULL ||
    self->last_src->rtt == 0)
  {
    GST_OBJECT_UNLOCK (self);
    return buffer_or_list;
  }

  if (GST_IS_BUFFER (buffer_or_list))
  {
    GstBuffer *buf = GST_BUFFER (buffer_or_list);

    list = gst_rtp_buffer_list_from_buffer (buf);
    gst_buffer_unref (buf);
  }
  else
  {
    list = GST_BUFFER_LIST (buffer_or_list);
  }

  list = gst_buffer_list_make_writable (list);

  GST_WRITE_UINT24_BE (data, self->last_src->rtt);
  GST_WRITE_UINT32_BE (data+3, fs_rtp_tfrc_get_now (self));

  it = gst_buffer_list_iterate (list);

  while (gst_buffer_list_iterator_next_group (it))
  {
    gst_buffer_list_iterator_next (it);

    if (self->extension_type == EXTENSION_ONE_BYTE)
    {
      if (!gst_rtp_buffer_list_add_extension_onebyte_header (it,
              self->extension_id, data, 7))
        GST_WARNING_OBJECT (self,
            "Could not add extension to RTP header in list %p", list);
    }
    else if (self->extension_type == EXTENSION_TWO_BYTES)
    {
      if (!gst_rtp_buffer_list_add_extension_twobytes_header (it, 0,
              self->extension_id, data, 7))
        GST_WARNING_OBJECT (self,
            "Could not add extension to RTP header in list %p", list);
    }
  }

  gst_buffer_list_iterator_free (it);

  GST_OBJECT_UNLOCK (self);

  return GST_MINI_OBJECT (list);
}

/* TODO:
 * - Insert element to insert TFRC header into RTP packets
 * - Hook up to incoming RTP packets to check for TFRC headers
 * - Hook up to incoming RTCP packets
 * - Hook up to outgoing RTCP packets to add extra TFRC package
 * - Set the bitrate when required
 */

FsRtpTfrc *
fs_rtp_tfrc_new (GObject *rtpsession, GstPad *inrtp, GstPad *inrtcp,
    GstElement **send_filter)
{
  FsRtpTfrc *self;

  g_return_val_if_fail (rtpsession, NULL);

  self = g_object_new (FS_TYPE_RTP_TFRC, NULL);

  self->systemclock = gst_system_clock_obtain ();

  self->rtpsession = rtpsession;
  self->in_rtp_pad = inrtp;
  self->in_rtcp_pad = inrtcp;

  self->in_rtp_probe_id = gst_pad_add_buffer_probe (inrtp,
      G_CALLBACK (incoming_rtp_probe), self);
  self->in_rtcp_probe_id = gst_pad_add_buffer_probe (inrtcp,
      G_CALLBACK (incoming_rtcp_probe), self);

  g_signal_connect_object (rtpsession, "on-ssrc-validated",
      G_CALLBACK (rtpsession_on_ssrc_validated), self, 0);


  g_signal_connect_object (rtpsession, "on-ssrc-validated",
      G_CALLBACK (rtpsession_on_ssrc_validated), self, 0);
  g_signal_connect_object (rtpsession, "on-sending-rtcp",
      G_CALLBACK (rtpsession_sending_rtcp), self, 0);

  self->packet_modder = GST_ELEMENT (fs_rtp_packet_modder_new (
        fs_rtp_tfrc_outgoing_packets, self));
  g_object_ref (self->packet_modder);

  if (send_filter)
    *send_filter = self->packet_modder;

  return self;
}


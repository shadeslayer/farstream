/*
 * Farsight2 - Farsight RTP TFRC support
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

#include <string.h>

#include "fs-rtp-packet-modder.h"
#include "gst/farsight/fs-rtp.h"
#include "fs-rtp-codec-negotiation.h"

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>


GST_DEBUG_CATEGORY_STATIC (fsrtpconference_tfrc);
#define GST_CAT_DEFAULT fsrtpconference_tfrc

G_DEFINE_TYPE (FsRtpTfrc, fs_rtp_tfrc, GST_TYPE_OBJECT);

/* props */
enum
{
  PROP_0,
  PROP_BITRATE
};

static void fs_rtp_tfrc_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_rtp_tfrc_dispose (GObject *object);

static void fs_rtp_tfrc_update_sender_timer_locked (
  FsRtpTfrc *self,
  struct TrackedSource *src,
  guint now);

static gboolean feedback_timer_expired (GstClock *clock, GstClockTime time,
    GstClockID id, gpointer user_data);


static void
fs_rtp_tfrc_class_init (FsRtpTfrcClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->get_property = fs_rtp_tfrc_get_property;
  gobject_class->dispose = fs_rtp_tfrc_dispose;

  g_object_class_install_property (gobject_class,
      PROP_BITRATE,
      g_param_spec_uint ("bitrate",
          "The bitrate at which data should be sent",
          "The bitrate that the session should try to send at in bits/sec",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}


static void
free_source (struct TrackedSource *src)
{
  if (src->sender_id)
    gst_clock_id_unschedule (src->sender_id);

  if (src->receiver_id)
    gst_clock_id_unschedule (src->receiver_id);

  if (src->rtpsource)
    g_object_unref (src->rtpsource);

  if (src->sender)
    tfrc_sender_free (src->sender);
  if (src->receiver)
    tfrc_receiver_free (src->receiver);

  if (src->idl)
    tfrc_is_data_limited_free (src->idl);

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

void
fs_rtp_tfrc_destroy (FsRtpTfrc *self)
{
  GST_OBJECT_LOCK (self);

  if (self->in_rtp_probe_id)
    g_signal_handler_disconnect (self->in_rtp_pad, self->in_rtp_probe_id);
  self->in_rtp_probe_id = 0;
  if (self->in_rtcp_probe_id)
    g_signal_handler_disconnect (self->in_rtcp_pad, self->in_rtcp_probe_id);
  self->in_rtcp_probe_id = 0;

  g_hash_table_destroy (g_hash_table_ref (self->tfrc_sources));

  GST_OBJECT_UNLOCK (self);
}

static void
fs_rtp_tfrc_dispose (GObject *object)
{
  FsRtpTfrc *self = FS_RTP_TFRC (object);

  GST_OBJECT_LOCK (self);

  if (self->tfrc_sources)
    g_hash_table_destroy (self->tfrc_sources);
  self->tfrc_sources = NULL;
  self->last_src = NULL;

  if (self->packet_modder)
    g_object_unref (self->packet_modder);

  gst_object_unref (self->systemclock);
  self->systemclock = NULL;

  GST_OBJECT_UNLOCK (self);

  if (G_OBJECT_CLASS (fs_rtp_tfrc_parent_class)->dispose)
    G_OBJECT_CLASS (fs_rtp_tfrc_parent_class)->dispose (object);
}

static void
fs_rtp_tfrc_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  FsRtpTfrc *self = FS_RTP_TFRC (object);

  switch (prop_id)
  {
    case PROP_BITRATE:
      GST_OBJECT_LOCK (self);
      if (self->last_src && self->last_src->sender)
        g_value_set_uint (value,
            tfrc_sender_get_send_rate (self->last_src->sender) * 8);
      else
        g_value_set_uint (value, 0);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static struct TrackedSource *
fs_rtp_tfrc_get_remote_ssrc_locked (FsRtpTfrc *self, guint ssrc,
  GObject *rtpsource)
{
  struct TrackedSource *src;

  src = g_hash_table_lookup (self->tfrc_sources, GUINT_TO_POINTER (ssrc));

  if (src)
  {
    if (G_UNLIKELY (rtpsource && !src->rtpsource))
      src->rtpsource = g_object_ref (rtpsource);

    return src;
  }

  src = g_slice_new0 (struct TrackedSource);

  src->self = self;
  src->ssrc = ssrc;
  if (rtpsource)
    src->rtpsource = g_object_ref (rtpsource);

  g_hash_table_insert (self->tfrc_sources, GUINT_TO_POINTER (ssrc), src);

  return src;
}

static void
rtpsession_on_ssrc_validated (GObject *rtpsession, GObject *rtpsource,
    FsRtpTfrc *self)
{
  guint32 ssrc;

  g_object_get (rtpsource, "ssrc", &ssrc, NULL);

  GST_DEBUG ("ssrc validate: %X", ssrc);

  GST_OBJECT_LOCK (self);
  fs_rtp_tfrc_get_remote_ssrc_locked (self, ssrc, rtpsource);
  GST_OBJECT_UNLOCK (self);
}

struct TimerData
{
  FsRtpTfrc *self;
  guint ssrc;
};

static struct TimerData *
build_timer_data (FsRtpTfrc *self, guint ssrc)
{
  struct TimerData *td = g_slice_new0 (struct TimerData);

  td->self = g_object_ref (self);
  td->ssrc = ssrc;

  return td;
}

static void
free_timer_data (gpointer data)
{
  struct TimerData *td = data;
  g_object_unref (td->self);
  g_slice_free (struct TimerData, td);
}

static void
fs_rtp_tfrc_receiver_timer_func (FsRtpTfrc *self, struct TrackedSource *src,
    guint now)
{
  guint expiry;
  GstClockReturn cret;

  if (src->receiver_id)
    gst_clock_id_unschedule (src->receiver_id);
  src->receiver_id = NULL;

  expiry = tfrc_receiver_get_feedback_timer_expiry (src->receiver);

  if (expiry <= now)
  {
    if (tfrc_receiver_feedback_timer_expired (src->receiver, now))
      g_signal_emit_by_name (self->rtpsession, "send-rtcp", 0);

    expiry = tfrc_receiver_get_feedback_timer_expiry (src->receiver);
  }

  src->receiver_id = gst_clock_new_single_shot_id (self->systemclock,
      expiry * GST_MSECOND);

  cret = gst_clock_id_wait_async_full (src->receiver_id, feedback_timer_expired,
      build_timer_data (self, src->ssrc), free_timer_data);
  if (cret != GST_CLOCK_OK)
    GST_ERROR ("Could not schedule feedback time for %u (now %u) error: %d",
        expiry, now, cret);
}

static gboolean
feedback_timer_expired (GstClock *clock, GstClockTime time, GstClockID id,
  gpointer user_data)
{
  struct TimerData *td = user_data;
  struct TrackedSource *src;
  guint now = GST_TIME_AS_MSECONDS (time);

  if (time == GST_CLOCK_TIME_NONE)
    return FALSE;

  GST_OBJECT_LOCK (td->self);

  src = g_hash_table_lookup (td->self->tfrc_sources,
      GUINT_TO_POINTER (td->ssrc));

  if (src)
    fs_rtp_tfrc_receiver_timer_func (td->self, src, now);

  GST_OBJECT_UNLOCK (td->self);

  return FALSE;
}


static guint
fs_rtp_tfrc_get_now (FsRtpTfrc *self)
{
  return GST_TIME_AS_MSECONDS (gst_clock_get_time (self->systemclock));
}

struct SendingRtcpData {
  FsRtpTfrc *self;
  GstBuffer *buffer;
  gboolean ret;
  guint32 ssrc;
  gboolean have_ssrc;
};

static void
tfrc_sources_process (gpointer key, gpointer value, gpointer user_data)
{
  struct SendingRtcpData *data = user_data;
  struct TrackedSource *src = value;
  GstRTCPPacket packet;
  guint8 *pdata;
  guint32 now;

  if (!src->receiver)
    return;

  if (!gst_rtcp_buffer_add_packet (data->buffer, GST_RTCP_TYPE_RTPFB, &packet))
    return;

  if (!gst_rtcp_packet_fb_set_fci_length (&packet, 4))
  {
    gst_rtcp_packet_remove (&packet);
    return;
  }

  if (!data->have_ssrc)
    g_object_get (data->self->rtpsession, "internal-ssrc", &data->ssrc, NULL);
  data->have_ssrc = TRUE;

  /* draft-ietf-avt-tfrc-profile-10 defines the type as 2 */
  gst_rtcp_packet_fb_set_type (&packet, 2);
  gst_rtcp_packet_fb_set_sender_ssrc (&packet, data->ssrc);
  gst_rtcp_packet_fb_set_media_ssrc (&packet, src->ssrc);
  pdata = gst_rtcp_packet_fb_get_fci (&packet);

  now = fs_rtp_tfrc_get_now (data->self);
  GST_WRITE_UINT32_BE (pdata, src->last_ts);
  GST_WRITE_UINT32_BE (pdata + 4, now - src->last_now);
  GST_WRITE_UINT32_BE (pdata + 8,
      tfrc_receiver_get_receive_rate (src->receiver));
  GST_WRITE_UINT32_BE (pdata + 12,
      tfrc_receiver_get_loss_event_rate (src->receiver) * G_MAXUINT);

  GST_LOG ("Sending RTCP report last_ts: %d delay: %d, x_recv: %d, rate: %f",
      src->last_ts, now - src->last_now,
      tfrc_receiver_get_receive_rate (src->receiver),
      tfrc_receiver_get_loss_event_rate (src->receiver));


  data->ret = TRUE;
}

static gboolean
rtpsession_sending_rtcp (GObject *rtpsession, GstBuffer *buffer,
    gboolean is_early, FsRtpTfrc *self)
{
  struct SendingRtcpData data;

  data.self = self;
  data.ret = FALSE;
  data.buffer = buffer;
  data.have_ssrc = FALSE;


  GST_OBJECT_LOCK (self);
  g_hash_table_foreach (self->tfrc_sources, tfrc_sources_process, &data);
  GST_OBJECT_UNLOCK (self);

  /* Return TRUE if something was added */
  return data.ret;
}

static gboolean
incoming_rtp_probe (GstPad *pad, GstBuffer *buffer, FsRtpTfrc *self)
{
  guint32 ssrc;
  guint8 *data;
  guint size;
  gboolean got_header = FALSE;
  struct TrackedSource *src;
  guint32 rtt, ts, seq;
  gboolean send_rtcp = FALSE;
  guint now;

  GST_OBJECT_LOCK (self);

  if (self->extension_type == EXTENSION_NONE)
    goto out;
  else if (self->extension_type == EXTENSION_ONE_BYTE)
    got_header = gst_rtp_buffer_get_extension_onebyte_header (buffer,
        self->extension_id, 0, (gpointer *) &data, &size);
  else if (self->extension_type == EXTENSION_TWO_BYTES)
    got_header = gst_rtp_buffer_get_extension_twobytes_header (buffer,
        NULL, self->extension_id, 0, (gpointer *) &data, &size);

  if (!got_header)
    goto out;
  if (size != 7)
    goto out;

  ssrc = gst_rtp_buffer_get_ssrc (buffer);
  seq = gst_rtp_buffer_get_seq (buffer);

  src = fs_rtp_tfrc_get_remote_ssrc_locked (self, ssrc, NULL);

  if (src->rtpsource == NULL)
  {
    GST_WARNING_OBJECT (self, "Got packet from unconfirmed source %X ?", ssrc);
    goto out;
  }

  now =  fs_rtp_tfrc_get_now (self);

  if (!src->receiver)
    src->receiver = tfrc_receiver_new (now);

  if (seq < src->last_seq)
    src->seq_cycles += 1 << 16;
  src->last_seq = seq;
  seq += src->seq_cycles;

  rtt = GST_READ_UINT24_BE (data);
  ts = GST_READ_UINT32_BE (data + 3);
  send_rtcp = tfrc_receiver_got_packet (src->receiver, ts, now, seq, rtt,
      GST_BUFFER_SIZE (buffer));

  GST_LOG ("Got RTP packet x_recv: %d, rate: %f",
      tfrc_receiver_get_receive_rate (src->receiver),
      tfrc_receiver_get_loss_event_rate (src->receiver));

  if (src->last_rtt == 0 && rtt)
    fs_rtp_tfrc_receiver_timer_func (self, src, now);

  src->last_ts = ts;
  src->last_now = now;
  src->last_rtt = rtt;

out:
  GST_OBJECT_UNLOCK (self);

  if (send_rtcp)
    g_signal_emit_by_name (src->self->rtpsession, "send-rtcp", 0);

  return TRUE;
}

static gboolean
no_feedback_timer_expired (GstClock *clock, GstClockTime time, GstClockID id,
  gpointer user_data)
{
  struct TimerData *td = user_data;
  struct TrackedSource *src;
  guint now = GST_TIME_AS_MSECONDS (time);
  guint old_rate = 0;
  gboolean notify = FALSE;

  if (time == GST_CLOCK_TIME_NONE)
    return FALSE;

  GST_OBJECT_LOCK (td->self);

  src = g_hash_table_lookup (td->self->tfrc_sources,
      GUINT_TO_POINTER (td->ssrc));

  if (!src)
    goto out;

  old_rate = tfrc_sender_get_send_rate (src->sender);

  fs_rtp_tfrc_update_sender_timer_locked (td->self, src, now);
  if (src->idl)
    tfrc_is_data_limited_set_rate (src->idl,
        tfrc_sender_get_send_rate (src->sender), now);

  //g_debug ("RATE: %u", tfrc_sender_get_send_rate (src->sender));

  if (old_rate != tfrc_sender_get_send_rate (src->sender))
    notify = TRUE;

out:

  GST_OBJECT_UNLOCK (td->self);

  if (notify)
    g_object_notify (G_OBJECT (td->self), "bitrate");

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

  expiry = tfrc_sender_get_no_feedback_timer_expiry (src->sender);

  if (expiry <= now)
  {
    tfrc_sender_no_feedback_timer_expired (src->sender, now);
    expiry = tfrc_sender_get_no_feedback_timer_expiry (src->sender);
  }


  src->sender_id = gst_clock_new_single_shot_id (self->systemclock,
      expiry * GST_MSECOND);

  cret = gst_clock_id_wait_async_full (src->sender_id,
      no_feedback_timer_expired, build_timer_data (self, src->ssrc),
      free_timer_data);
  if (cret != GST_CLOCK_OK)
    GST_ERROR ("Could not schedule feedback time for %u (now %u) error: %d",
        expiry, now, cret);
}

static gboolean
incoming_rtcp_probe (GstPad *pad, GstBuffer *buffer, FsRtpTfrc *self)
{
  GstRTCPPacket packet;
  gboolean notify = FALSE;

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
      guint32 media_ssrc;
      guint32 sender_ssrc;
      guint32 ts;
      guint32 delay;
      guint32 x_recv;
      gdouble loss_event_rate;
      guint8 *buf = GST_BUFFER_DATA (packet.buffer) + packet.offset;
      struct TrackedSource *src;
      guint now, rtt;
      guint32 local_ssrc;
      gboolean is_data_limited;
      guint old_send_rate = 0;

      media_ssrc = gst_rtcp_packet_fb_get_media_ssrc (&packet);

      g_object_get (self->rtpsession, "internal-ssrc", &local_ssrc, NULL);

      if (media_ssrc != local_ssrc)
        continue;

      sender_ssrc = gst_rtcp_packet_fb_get_sender_ssrc (&packet);

      buf += 4 * 3; /* skip the header, ssrc of sender and media sender */

      ts = GST_READ_UINT32_BE (buf);
      buf += 4;
      delay = GST_READ_UINT32_BE (buf);
      buf += 4;
      x_recv = GST_READ_UINT32_BE (buf);
      buf += 4;
      loss_event_rate = (gdouble) GST_READ_UINT32_BE (buf) / (gdouble) G_MAXUINT;

      GST_LOG ("Got RTCP TFRC packet last_sent_ts: %u delay: %u x_recv: %u"
          " loss_event_rate: %f", ts, delay, x_recv, loss_event_rate);

      GST_OBJECT_LOCK (self);

      src = fs_rtp_tfrc_get_remote_ssrc_locked (self, sender_ssrc,
          NULL);

      now = fs_rtp_tfrc_get_now (self);

      if (ts >= now || now - ts < delay)
      {
        GST_DEBUG ("Ignoring packet because ts >= now || now - ts < delay"
            "(ts: %u now: %u delay:%u", ts, now, delay);
        goto done;
      }

      rtt = now - ts - delay;

      if (rtt == 0)
        rtt = 1;

      if (rtt > 50 * 1000)
      {
        GST_WARNING ("Impossible RTT %u ms, ignoring", rtt);
        goto done;
      }

      src->rtt = rtt;

      GST_LOG ("rtt: %u = now %u - ts %u - delay %u", rtt, now, ts, delay);

      if (!src->sender)
      {
        src->sender = tfrc_sender_new (1460, now);
        tfrc_sender_on_first_rtt (src->sender, now);
      }

      if (self->last_src)
        old_send_rate = tfrc_sender_get_send_rate (self->last_src->sender);

      if (!src->idl)
        src->idl = tfrc_is_data_limited_new (now);
      is_data_limited =
          tfrc_is_data_limited_received_feedback (src->idl, now, ts, rtt);

      tfrc_sender_on_feedback_packet (src->sender, now, rtt, x_recv,
          loss_event_rate, is_data_limited);

      tfrc_is_data_limited_set_rate (src->idl,
          tfrc_sender_get_send_rate (src->sender), now);

      //g_debug ("RATE: %u", tfrc_sender_get_send_rate (src->sender));

      fs_rtp_tfrc_update_sender_timer_locked (self, src, now);

      self->last_src = src;

      if (old_send_rate != tfrc_sender_get_send_rate (src->sender))
        notify = TRUE;

    done:
      GST_OBJECT_UNLOCK (self);


    }
  } while (gst_rtcp_packet_move_to_next (&packet));

  if (notify)
    g_object_notify (G_OBJECT (self), "bitrate");

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
  guint now;

  GST_OBJECT_LOCK (self);

  if (self->extension_type == EXTENSION_NONE)
  {
    GST_OBJECT_UNLOCK (self);
    return buffer_or_list;
  }

  now = fs_rtp_tfrc_get_now (self);

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

  if (self->last_src)
    GST_WRITE_UINT24_BE (data, self->last_src->rtt);
  else
    GST_WRITE_UINT24_BE (data, 0);
  GST_WRITE_UINT32_BE (data+3, now);

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

  if (self->last_src && self->last_src->idl)
  {
    it = gst_buffer_list_iterate (list);
    while (gst_buffer_list_iterator_next_group (it))
    {
      guint size = 0;
      GstBuffer *buf;

      while ((buf = gst_buffer_list_iterator_next (it)))
        size += GST_BUFFER_SIZE (buf);

      tfrc_is_data_limited_sent_segment (self->last_src->idl, now, size);
    }
    gst_buffer_list_iterator_free (it);
  }

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
fs_rtp_tfrc_new (GObject *rtpsession,
    GstPad *inrtp,
    GstPad *inrtcp,
    GstElement **send_filter)
{
  FsRtpTfrc *self;

  g_return_val_if_fail (rtpsession, NULL);

  self = g_object_new (FS_TYPE_RTP_TFRC, NULL);

  self->extension_type = EXTENSION_ONE_BYTE;
  self->extension_id = 4;

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

gboolean
validate_ca_for_tfrc (CodecAssociation *ca, gpointer user_data)
{
  return codec_association_is_valid_for_sending (ca, TRUE) &&
      fs_codec_get_feedback_parameter (ca->codec, "tfrc", NULL, NULL);
}

void
fs_rtp_tfrc_filter_codecs (FsRtpTfrc *self,
    GList **codec_associations,
    GList **header_extensions)
{
  gboolean has_header_ext = FALSE;
  gboolean has_codec_rtcpfb = FALSE;
  GList *item;

  has_codec_rtcpfb = !!lookup_codec_association_custom (*codec_associations,
      validate_ca_for_tfrc, NULL);

  for (item = *header_extensions; item;)
  {
    FsRtpHeaderExtension *hdrext = item->data;
    GList *next = item->next;

    if (!strcmp (hdrext->uri, "urn:ietf:params:rtp-hdtext:rtt-sendts"))
    {
      if (has_header_ext || !has_codec_rtcpfb)
      {
        *header_extensions = g_list_remove_link (*header_extensions, item);
      }
      else if (hdrext->direction == FS_DIRECTION_BOTH)
      {
        has_header_ext = TRUE;
      }
    }
    item = next;
  }

  if (!has_codec_rtcpfb || has_header_ext)
    return;

  for (item = *codec_associations; item; item = item->next)
  {
    CodecAssociation *ca = item->data;
    GList *item2;

    for (item2 = ca->codec->ABI.ABI.feedback_params; item2;)
    {
      GList *next2 = item2->next;
      FsFeedbackParameter *p = item2->data;

      if (!g_ascii_strcasecmp (p->type, "tfrc"))
      {
        ca->codec->ABI.ABI.feedback_params = g_list_delete_link (
          ca->codec->ABI.ABI.feedback_params, item2);
      }


      item2 = next2;
    }
  }

}

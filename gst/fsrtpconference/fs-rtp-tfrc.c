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


static struct TrackedSource *
tracked_src_new (FsRtpTfrc *self)
{
  struct TrackedSource *src;

  src = g_slice_new0 (struct TrackedSource);
  src->self = self;

  return src;
}

static void
tracked_src_free (struct TrackedSource *src)
{
  if (src->sender_id)
  {
    gst_clock_id_unschedule (src->sender_id);
    gst_clock_id_unref (src->sender_id);
  }

  if (src->receiver_id)
  {
    gst_clock_id_unschedule (src->receiver_id);
    gst_clock_id_unref (src->receiver_id);
  }

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
      g_direct_equal, NULL, (GDestroyNotify) tracked_src_free);

  self->last_sent_ts = GST_CLOCK_TIME_NONE;
  self->byte_reservoir = 1500; /* About one packet */
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

  if (self->initial_src)
    tracked_src_free (self->initial_src);
  self->initial_src = NULL;

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
    {
      guint byterate;

      GST_OBJECT_LOCK (self);
      if (self->last_src && self->last_src->sender)
        byterate = tfrc_sender_get_send_rate (self->last_src->sender);
      else
        byterate = tfrc_sender_get_send_rate (NULL);
      GST_OBJECT_UNLOCK (self);

      if (G_LIKELY (byterate < G_MAXUINT / 8))
        g_value_set_uint (value, byterate * 8);
      else
        g_value_set_uint (value, G_MAXUINT);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint
fs_rtp_tfrc_get_now (FsRtpTfrc *self)
{
  return GST_TIME_AS_MSECONDS (gst_clock_get_time (self->systemclock));
}


static struct TrackedSource *
fs_rtp_tfrc_get_remote_ssrc_locked (FsRtpTfrc *self, guint ssrc,
  GObject *rtpsource)
{
  struct TrackedSource *src;

  src = g_hash_table_lookup (self->tfrc_sources, GUINT_TO_POINTER (ssrc));

  if (G_LIKELY (src))
  {
    if (G_UNLIKELY (rtpsource && !src->rtpsource))
      src->rtpsource = g_object_ref (rtpsource);

    return src;
  }

  if (self->initial_src)
  {
    src = self->initial_src;
    self->initial_src = NULL;
    src->ssrc = ssrc;
    if (rtpsource && !src->rtpsource)
      src->rtpsource = g_object_ref (rtpsource);
    g_hash_table_insert (self->tfrc_sources, GUINT_TO_POINTER (ssrc), src);
    return src;
  }

  src = tracked_src_new (self);
  src->ssrc = ssrc;
  if (rtpsource)
    src->rtpsource = g_object_ref (rtpsource);

  if (!self->last_src)
    self->last_src = src;

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
fs_rtp_tfrc_set_receiver_timer_locked (FsRtpTfrc *self,
    struct TrackedSource *src, guint now)
{
  guint expiry = tfrc_receiver_get_feedback_timer_expiry (src->receiver);
  GstClockReturn cret;

  if (src->receiver_id)
  {
    if (src->next_feedback_timer <= expiry)
      return;

    gst_clock_id_unschedule (src->receiver_id);
    gst_clock_id_unref (src->receiver_id);
    src->receiver_id = NULL;
  }
  src->next_feedback_timer = expiry;

  src->receiver_id = gst_clock_new_single_shot_id (self->systemclock,
      expiry * GST_MSECOND);

  cret = gst_clock_id_wait_async_full (src->receiver_id, feedback_timer_expired,
      build_timer_data (self, src->ssrc), free_timer_data);
  if (cret != GST_CLOCK_OK)
    GST_ERROR ("Could not schedule feedback time for %u (now %u) error: %d",
        expiry, now, cret);
}

static void
fs_rtp_tfrc_receiver_timer_func_locked (FsRtpTfrc *self,
    struct TrackedSource *src, guint now)
{
  guint expiry;

  if (src->receiver_id)
  {
    gst_clock_id_unschedule (src->receiver_id);
    gst_clock_id_unref (src->receiver_id);
    src->receiver_id = NULL;
  }

  expiry = tfrc_receiver_get_feedback_timer_expiry (src->receiver);

  if (expiry <= now &&
      tfrc_receiver_feedback_timer_expired (src->receiver, now))
  {
    src->send_feedback = TRUE;
    g_signal_emit_by_name (self->rtpsession, "send-rtcp", (guint64) 0);
  }
  else
  {
    fs_rtp_tfrc_set_receiver_timer_locked (self, src, now);
  }
}

static gboolean
feedback_timer_expired (GstClock *clock, GstClockTime time, GstClockID id,
  gpointer user_data)
{
  struct TimerData *td = user_data;
  struct TrackedSource *src;
  guint now;

  if (time == GST_CLOCK_TIME_NONE)
    return FALSE;

  GST_OBJECT_LOCK (td->self);

  src = g_hash_table_lookup (td->self->tfrc_sources,
      GUINT_TO_POINTER (td->ssrc));

  now = fs_rtp_tfrc_get_now (td->self);

  if (G_LIKELY (src))
  {
    if (src->receiver_id && src->receiver_id != id)
    {
      g_warning ("Receiver ID confusion");
      gst_clock_id_unschedule (src->receiver_id);
      gst_clock_id_unref (src->receiver_id);
      src->receiver_id = NULL;
    }

    fs_rtp_tfrc_receiver_timer_func_locked (td->self, src, now);
  }

  GST_OBJECT_UNLOCK (td->self);

  return FALSE;
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
  gdouble loss_event_rate;
  guint receive_rate;

  if (!src->receiver)
    return;

  if (src->got_nohdr_pkt)
    return;

  if (!src->send_feedback)
    goto done;

  if (!gst_rtcp_buffer_add_packet (data->buffer, GST_RTCP_TYPE_RTPFB, &packet))
    goto done;

  if (!gst_rtcp_packet_fb_set_fci_length (&packet, 4))
  {
    gst_rtcp_packet_remove (&packet);
    goto done;
  }

  now = fs_rtp_tfrc_get_now (data->self);
  if (!tfrc_receiver_send_feedback (src->receiver, now, &loss_event_rate,
          &receive_rate))
  {
    gst_rtcp_packet_remove (&packet);
    goto done;
  }

  if (!data->have_ssrc)
    g_object_get (data->self->rtpsession, "internal-ssrc", &data->ssrc, NULL);
  data->have_ssrc = TRUE;

  /* draft-ietf-avt-tfrc-profile-10 defines the type as 2 */
  gst_rtcp_packet_fb_set_type (&packet, 2);
  gst_rtcp_packet_fb_set_sender_ssrc (&packet, data->ssrc);
  gst_rtcp_packet_fb_set_media_ssrc (&packet, src->ssrc);
  pdata = gst_rtcp_packet_fb_get_fci (&packet);

  GST_WRITE_UINT32_BE (pdata, src->last_ts);
  GST_WRITE_UINT32_BE (pdata + 4, now - src->last_now);
  GST_WRITE_UINT32_BE (pdata + 8, receive_rate);
  GST_WRITE_UINT32_BE (pdata + 12, loss_event_rate * G_MAXUINT);

  GST_LOG ("Sending RTCP report last_ts: %d delay: %d, x_recv: %d, rate: %f",
      src->last_ts, now - src->last_now, receive_rate, loss_event_rate);

  src->send_feedback = FALSE;

  data->ret = TRUE;

done:
  fs_rtp_tfrc_set_receiver_timer_locked (data->self, src, now);
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
  struct TrackedSource *src = NULL;
  guint32 rtt, ts, seq;
  gboolean send_rtcp = FALSE;
  guint now;
  guint8 pt;
  gint seq_delta;

  GST_OBJECT_LOCK (self);

  ssrc = gst_rtp_buffer_get_ssrc (buffer);

  pt = gst_rtp_buffer_get_payload_type (buffer);

  if (pt > 128 || !self->pts[pt])
    goto out_no_header;

  if (self->extension_type == EXTENSION_NONE)
    goto out_no_header;
  else if (self->extension_type == EXTENSION_ONE_BYTE)
    got_header = gst_rtp_buffer_get_extension_onebyte_header (buffer,
        self->extension_id, 0, (gpointer *) &data, &size);
  else if (self->extension_type == EXTENSION_TWO_BYTES)
    got_header = gst_rtp_buffer_get_extension_twobytes_header (buffer,
        NULL, self->extension_id, 0, (gpointer *) &data, &size);

  seq = gst_rtp_buffer_get_seq (buffer);

  src = fs_rtp_tfrc_get_remote_ssrc_locked (self, ssrc, NULL);

  if (src->rtpsource == NULL)
  {
    GST_WARNING_OBJECT (self, "Got packet from unconfirmed source %X ?", ssrc);
    goto out;
  }

  if (!got_header || size != 7)
    goto out_no_header;

  src->got_nohdr_pkt = FALSE;

  now =  fs_rtp_tfrc_get_now (self);

  if (!src->receiver)
    src->receiver = tfrc_receiver_new (now);

  seq_delta = seq - src->last_seq;

  if (seq < src->last_seq && seq_delta < -3000)
    src->seq_cycles += 1 << 16;
  src->last_seq = seq;
  seq += src->seq_cycles;

  rtt = GST_READ_UINT24_BE (data);
  ts = GST_READ_UINT32_BE (data + 3);
  send_rtcp = tfrc_receiver_got_packet (src->receiver, ts, now, seq, rtt,
      GST_BUFFER_SIZE (buffer));

  GST_LOG ("Got RTP packet");

  if (rtt &&  src->last_rtt == 0)
    fs_rtp_tfrc_receiver_timer_func_locked (self, src, now);

  src->last_ts = ts;
  src->last_now = now;
  src->last_rtt = rtt;

out:

  if (send_rtcp)
  {
    src->send_feedback = TRUE;
    GST_OBJECT_UNLOCK (self);
    g_signal_emit_by_name (src->self->rtpsession, "send-rtcp", (guint64) 0);
  }
  else
  {
    GST_OBJECT_UNLOCK (self);
  }

  return TRUE;

out_no_header:
  if (src)
    src->got_nohdr_pkt = TRUE;
  goto out;
}

static gboolean
no_feedback_timer_expired (GstClock *clock, GstClockTime time, GstClockID id,
  gpointer user_data)
{
  struct TimerData *td = user_data;
  struct TrackedSource *src;
  guint now;
  guint old_rate = 0;
  gboolean notify = FALSE;

  if (time == GST_CLOCK_TIME_NONE)
    return FALSE;

  GST_OBJECT_LOCK (td->self);

  src = g_hash_table_lookup (td->self->tfrc_sources,
      GUINT_TO_POINTER (td->ssrc));

  if (!src)
    goto out;

  now = fs_rtp_tfrc_get_now (td->self);

  old_rate = tfrc_sender_get_send_rate (src->sender);

  fs_rtp_tfrc_update_sender_timer_locked (td->self, src, now);

  if (old_rate != tfrc_sender_get_send_rate (src->sender))
  {
    GST_DEBUG ("Send rate changed: %u -> %u", old_rate,
        tfrc_sender_get_send_rate (src->sender));
    notify = TRUE;
  }

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
  {
    gst_clock_id_unschedule (src->sender_id);
    gst_clock_id_unref (src->sender_id);
    src->sender_id = NULL;
  }

  if (src->sender == NULL)
    return;

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

static void
tracked_src_add_sender (struct TrackedSource *src, guint now)
{
  src->sender = tfrc_sender_new (1460, now);
  src->idl = tfrc_is_data_limited_new (now);
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

      if (ts > now || now - ts < delay)
      {
        GST_WARNING ("Ignoring packet because ts > now || now - ts < delay"
            "(ts: %u now: %u delay:%u", ts, now, delay);
        goto done;
      }

      /* Make sure we only use the RTT from the most recent packets from
       * the remote side, ignore anything that got delayed in between.
       */
      if (ts < src->max_ts &&
          (src->max_ts < (G_MAXUINT * 9/10) ||
              ts > (G_MAXUINT / 10)))
        goto done;

      rtt = now - ts - delay;

      if (rtt == 0)
        rtt = 1;

      if (rtt > 10 * 1000)
      {
        GST_WARNING ("Impossible RTT %u ms, ignoring", rtt);
        goto done;
      }

      GST_LOG ("rtt: %u = now %u - ts %u - delay %u", rtt, now, ts, delay);

      src->max_ts = ts;

      if (G_UNLIKELY (!src->sender))
        tracked_src_add_sender (src, now);

      if (G_UNLIKELY (tfrc_sender_get_averaged_rtt (src->sender) == 0))
        tfrc_sender_on_first_rtt (src->sender, now);

      if (self->last_src && self->last_src->sender)
        old_send_rate = tfrc_sender_get_send_rate (self->last_src->sender);

      is_data_limited =
          tfrc_is_data_limited_received_feedback (src->idl, now, ts,
              tfrc_sender_get_averaged_rtt (src->sender));

      tfrc_sender_on_feedback_packet (src->sender, now, rtt, x_recv,
          loss_event_rate, is_data_limited);

      fs_rtp_tfrc_update_sender_timer_locked (self, src, now);

      self->last_src = src;

      if (old_send_rate != tfrc_sender_get_send_rate (src->sender))
      {
        GST_DEBUG ("Send rate changed: %u -> %u", old_send_rate,
            tfrc_sender_get_send_rate (src->sender));
        notify = TRUE;
      }

    done:
      GST_OBJECT_UNLOCK (self);


    }
  } while (gst_rtcp_packet_move_to_next (&packet));

  if (notify)
    g_object_notify (G_OBJECT (self), "bitrate");

out:
  return TRUE;
}

static GstClockTime
fs_rtp_tfrc_get_sync_time (FsRtpPacketModder *modder,
    GstBuffer *buffer, gpointer user_data)
{
  FsRtpTfrc *self = FS_RTP_TFRC (user_data);
  GstClockTime sync_time = GST_BUFFER_TIMESTAMP (buffer);
  gint bytes_for_one_rtt = 0;
  guint size = 0;
  guint send_rate;

  GST_OBJECT_LOCK (self);

  if (self->extension_type == EXTENSION_NONE)
  {
    GST_OBJECT_UNLOCK (self);
    return GST_CLOCK_TIME_NONE;
  }

  if (self->last_src && self->last_src->sender)
  {
    send_rate = tfrc_sender_get_send_rate (self->last_src->sender);
    bytes_for_one_rtt = send_rate *
        tfrc_sender_get_averaged_rtt (self->last_src->sender) / 1000;
  }
  else
  {
    send_rate = tfrc_sender_get_send_rate (NULL);
    bytes_for_one_rtt = 0;
  }

  size = GST_BUFFER_SIZE (buffer) + 10;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
  {
    if (GST_CLOCK_TIME_IS_VALID (self->last_sent_ts) &&
        self->last_sent_ts < GST_BUFFER_TIMESTAMP (buffer))
      self->byte_reservoir +=
        gst_util_uint64_scale (
            (GST_BUFFER_TIMESTAMP (buffer) - self->last_sent_ts),
            send_rate,
            GST_SECOND);
    self->last_sent_ts = GST_BUFFER_TIMESTAMP (buffer);

    if (bytes_for_one_rtt &&
        self->byte_reservoir > bytes_for_one_rtt)
      self->byte_reservoir = bytes_for_one_rtt;
  }

  self->byte_reservoir -= size;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer) &&
      self->byte_reservoir < 0)
  {
    GstClockTimeDiff diff = 0;

    diff = gst_util_uint64_scale_int (GST_SECOND,
        -self->byte_reservoir, send_rate);
    g_assert (diff > 0);


    GST_LOG ("Delaying packet by %"GST_TIME_FORMAT
        " = 1sec * bytes %d / rate %u",
        GST_TIME_ARGS (diff), self->byte_reservoir,
        send_rate);

    GST_BUFFER_TIMESTAMP (buffer) += diff;
  }

  GST_OBJECT_UNLOCK (self);


  return sync_time;
}


static GstBuffer *
fs_rtp_tfrc_outgoing_packets (FsRtpPacketModder *modder,
    GstBuffer *buffer, GstClockTime buffer_ts, gpointer user_data)
{
  FsRtpTfrc *self = FS_RTP_TFRC (user_data);
  gchar data[7];
  guint now;
  GstBuffer *newbuf;
  gboolean is_data_limited;

  if (!GST_CLOCK_TIME_IS_VALID (buffer_ts))
    return buffer;

  GST_OBJECT_LOCK (self);

  if (self->extension_type == EXTENSION_NONE)
  {
    GST_OBJECT_UNLOCK (self);
    return buffer;
  }

  now = fs_rtp_tfrc_get_now (self);

  if (G_UNLIKELY (self->last_src == NULL))
    self->initial_src = self->last_src = tracked_src_new (self);

  if (G_UNLIKELY (self->last_src->sender == NULL))
  {
    tracked_src_add_sender (self->last_src, now);
    fs_rtp_tfrc_update_sender_timer_locked (self, self->last_src, now);
  }

  GST_WRITE_UINT24_BE (data,
      tfrc_sender_get_averaged_rtt (self->last_src->sender));
  GST_WRITE_UINT32_BE (data+3, now);

  is_data_limited = (GST_BUFFER_TIMESTAMP (buffer) == buffer_ts);

  newbuf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buffer) + 16);
  gst_buffer_copy_metadata (newbuf, buffer, GST_BUFFER_COPY_ALL);

  memcpy (GST_BUFFER_DATA (newbuf), GST_BUFFER_DATA (buffer),
      gst_rtp_buffer_get_header_len (buffer));

  if (self->extension_type == EXTENSION_ONE_BYTE)
  {
    if (!gst_rtp_buffer_add_extension_onebyte_header (newbuf,
            self->extension_id, data, 7))
      GST_WARNING_OBJECT (self,
          "Could not add extension to RTP header buf %p", newbuf);
  }
  else if (self->extension_type == EXTENSION_TWO_BYTES)
  {
    if (!gst_rtp_buffer_add_extension_twobytes_header (newbuf, 0,
            self->extension_id, data, 7))
      GST_WARNING_OBJECT (self,
          "Could not add extension to RTP header in list %p", newbuf);
  }

  /* FIXME:
   * This will break if any padding is applied
   */

  GST_BUFFER_SIZE (newbuf) = gst_rtp_buffer_get_header_len (newbuf) +
    gst_rtp_buffer_get_payload_len (buffer);

  memcpy (gst_rtp_buffer_get_payload (newbuf),
      gst_rtp_buffer_get_payload (buffer),
      gst_rtp_buffer_get_payload_len (buffer));


  if (g_hash_table_size (self->tfrc_sources))
  {
    GHashTableIter ht_iter;
    struct TrackedSource *src;

    g_hash_table_iter_init (&ht_iter, self->tfrc_sources);

    while (g_hash_table_iter_next (&ht_iter, NULL,
            (gpointer *) &src))
    {
      if (src->sender)
      {
        if (!is_data_limited)
          tfrc_is_data_limited_not_limited_now (src->idl, now);
        tfrc_sender_sending_packet (src->sender, GST_BUFFER_SIZE (newbuf));
      }
    }
  }
  if (self->initial_src)
  {
    if (!is_data_limited)
      tfrc_is_data_limited_not_limited_now (self->initial_src->idl, now);
    tfrc_sender_sending_packet (self->initial_src->sender,
        GST_BUFFER_SIZE (newbuf));
  }


  GST_OBJECT_UNLOCK (self);

  gst_buffer_unref (buffer);

  return newbuf;
}


FsRtpTfrc *
fs_rtp_tfrc_new (GObject *rtpsession,
    GstPad *inrtp,
    GstPad *inrtcp,
    GstElement **send_filter)
{
  FsRtpTfrc *self;

  g_return_val_if_fail (rtpsession, NULL);

  self = g_object_new (FS_TYPE_RTP_TFRC, NULL);

  self->extension_type = EXTENSION_NONE;
  self->extension_id = 0;
  memset (self->pts, 0, 128);

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
  g_signal_connect_object (rtpsession, "on-sending-rtcp",
      G_CALLBACK (rtpsession_sending_rtcp), self, 0);

  self->packet_modder = GST_ELEMENT (fs_rtp_packet_modder_new (
        fs_rtp_tfrc_outgoing_packets, fs_rtp_tfrc_get_sync_time, self));
  g_object_ref (self->packet_modder);

  if (send_filter)
    *send_filter = self->packet_modder;

  return self;
}

gboolean
validate_ca_for_tfrc (CodecAssociation *ca, gpointer user_data)
{
  return codec_association_is_valid_for_sending (ca, TRUE) &&
      fs_codec_get_feedback_parameter (ca->codec, "tfrc", "",  "");
}

void
fs_rtp_tfrc_filter_codecs (GList **codec_associations,
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
        fs_rtp_header_extension_destroy (item->data);
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
        fs_codec_remove_feedback_parameter (ca->codec, item2);

      item2 = next2;
    }
  }

}

void
fs_rtp_tfrc_codecs_updated (FsRtpTfrc *self,
    GList *codec_associations,
    GList *header_extensions)
{
  GList *item;
  FsRtpHeaderExtension *hdrext;

  GST_OBJECT_LOCK (self);

  memset (self->pts, 0, 128);
  for (item = codec_associations; item; item = item->next)
  {
    CodecAssociation *ca = item->data;

    if (fs_codec_get_feedback_parameter (ca->codec, "tfrc", NULL, NULL));
    self->pts[ca->codec->id] = TRUE;
  }

  for (item = header_extensions; item; item = item->next)
  {
    hdrext = item->data;
    if (!strcmp (hdrext->uri, "urn:ietf:params:rtp-hdtext:rtt-sendts") &&
        hdrext->direction == FS_DIRECTION_BOTH)
      break;
  }

  if (!item)
  {
    self->extension_type = EXTENSION_NONE;
    GST_OBJECT_UNLOCK (self);
    return;
  }

  if (hdrext->id > 15)
    self->extension_type = EXTENSION_TWO_BYTES;
  else
    self->extension_type = EXTENSION_ONE_BYTE;

  self->extension_id = hdrext->id;

  GST_OBJECT_UNLOCK (self);
}


gboolean
fs_rtp_tfrc_is_enabled (FsRtpTfrc *self, guint pt)
{
  gboolean is_enabled;

  g_return_val_if_fail (pt < 128, FALSE);


  GST_OBJECT_LOCK (self);
  is_enabled = (self->extension_type != EXTENSION_NONE) &&
      self->pts[pt];
  GST_OBJECT_UNLOCK (self);

  return is_enabled;
}

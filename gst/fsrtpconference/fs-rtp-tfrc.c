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

GST_DEBUG_CATEGORY_STATIC (fsrtpconference_tfrc);
#define GST_CAT_DEFAULT fsrtpconference_tfrc

G_DEFINE_TYPE (FsRtpTfrc, fs_rtp_tfrc, GST_TYPE_OBJECT);

static void fs_rtp_tfrc_dispose (GObject *object);
static void fs_rtp_tfrc_finalize (GObject *object);


static void
fs_rtp_tfrc_class_init (FsRtpTfrcClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = fs_rtp_tfrc_dispose;
  gobject_class->finalize = fs_rtp_tfrc_finalize;
}


static void
fs_rtp_tfrc_init (FsRtpTfrc *self)
{
  GST_DEBUG_CATEGORY_INIT (fsrtpconference_tfrc,
      "fsrtpconference_tfrc", 0,
      "Farsight RTP Conference Element Rate Control logic");
  /* member init */
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
}

static void
fs_rtp_tfrc_finalize (GObject *object)
{
}

static void
rtpsession_on_ssrc_validated (GObject *rtpsession, GObject *source,
    FsRtpTfrc *self)
{
  return;
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
incoming_rtcp_probe (GstPad *pad, GstBuffer *buffer, FsRtpTfrc *self)
{
  return TRUE;
}

/* TODO:
 * - Insert element to insert TFRC header into RTP packets
 * - Hook up to incoming RTP packets to check for TFRC headers
 * - Hook up to incoming RTCP packets
 * - Hook up to outgoing RTCP packets to add extra TFRC package
 * - Set the bitrate when required
 */

FsRtpTfrc *
fs_rtp_tfrc_new (GObject *rtpsession, GstPad *inrtp, GstPad *inrtcp)
{
  FsRtpTfrc *self;

  g_return_val_if_fail (rtpsession, NULL);

  self = g_object_new (FS_TYPE_RTP_TFRC, NULL);

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


  return self;
}


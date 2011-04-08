/*
 * Farsight2 - Farsight RTP Packet modder
 *
 * Copyright 2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2010 Nokia Corp.
 *
 * fs-rtp-packet-modder.h - Filter to modify RTP packets
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


#ifndef __FS_RTP_PACKET_MODDER_H__
#define __FS_RTP_PACKET_MODDER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define FS_TYPE_RTP_PACKET_MODDER \
  (fs_rtp_packet_modder_get_type ())
#define FS_RTP_PACKET_MODDER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),FS_TYPE_RTP_PACKET_MODDER, \
      FsRtpPacketModder))
#define FS_RTP_PACKET_MODDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),FS_TYPE_RTP_PACKET_MODDER, \
      FsRtpPacketModderClass))
#define FS_IS_RTP_PACKET_MODDER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),FS_TYPE_RTP_PACKET_MODDER))
#define FS_IS_RTP_PACKET_MODDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),FS_TYPE_RTP_PACKET_MODDER))

typedef struct _FsRtpPacketModder          FsRtpPacketModder;
typedef struct _FsRtpPacketModderClass     FsRtpPacketModderClass;


typedef GstBuffer *(*FsRtpPacketModderFunc) (FsRtpPacketModder *modder,
    GstBuffer *buffer, gpointer user_data);

/**
 * FsRtpPacketModder:
 *
 * Opaque #FsRtpPacketModder data structure.
 */
struct _FsRtpPacketModder {
  GstElement      element;

  GstPad *srcpad;
  GstPad *sinkpad;

  FsRtpPacketModderFunc func;
  gpointer user_data;

  /* for sync */
  GstSegment segment;
  GstClockID clock_id;
  gboolean unscheduled;
  /* the latency of the upstream peer, we have to take this into account when
   * synchronizing the buffers. */
  GstClockTime peer_latency;
};

struct _FsRtpPacketModderClass {
  GstElementClass parent_class;
};

GType   fs_rtp_packet_modder_get_type        (void);

FsRtpPacketModder *fs_rtp_packet_modder_new (FsRtpPacketModderFunc func,
    gpointer user_data);


G_END_DECLS

#endif /* __FS_RTP_PACKET_MODDER_H__ */

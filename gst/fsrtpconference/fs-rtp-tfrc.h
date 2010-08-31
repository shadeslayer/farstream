/*
 * Farsight2 - Farsight RTP TFRC Support
 *
 * Copyright 2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2010 Nokia Corp.
 *
 * fs-rtp-tfrc.h - Rate control for Farsight RTP sessions
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


#ifndef __FS_RTP_TFRC_H__
#define __FS_RTP_TFRC_H__

#include <gst/gst.h>

#include "tfrc.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_TFRC \
  (fs_rtp_tfrc_get_type ())
#define FS_RTP_TFRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_TFRC, FsRtpTfrc))
#define FS_RTP_TFRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_TFRC, FsRtpTfrcClass))
#define FS_IS_RTP_TFRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_TFRC))
#define FS_IS_RTP_TFRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_TFRC))
#define FS_RTP_TFRC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_TFRC, FsRtpTfrcClass))
#define FS_RTP_TFRC_CAST(obj) ((FsRtpTfrc *) (obj))

typedef struct _FsRtpTfrc FsRtpTfrc;
typedef struct _FsRtpTfrcClass FsRtpTfrcClass;

typedef enum {
  EXTENSION_NONE,
  EXTENSION_ONE_BYTE,
  EXTENSION_TWO_BYTES
} ExtensionType;


struct TrackedSource {
  FsRtpTfrc *self;

  guint32 ssrc;
  GObject *rtpsource;
  gboolean has_google_tfrc;
  gboolean has_standard_tfrc;

  TfrcSender *sender;
  GstClockID sender_id;
  guint sender_expiry;
  guint32 rtt;

  TfrcReceiver *receiver;
  GstClockID receiver_id;
  guint32 seq_cycles;
  guint32 last_seq;
  guint32 last_ts;
  guint32 last_now;
  guint32 last_rtt;
};

/**
 * FsRtpTfrc:
 *
 */
struct _FsRtpTfrc
{
  GstObject parent;

  GstClock *systemclock;

  GObject *rtpsession;

  GstPad *in_rtp_pad;
  GstPad *in_rtcp_pad;

  gulong in_rtp_probe_id;
  gulong in_rtcp_probe_id;

  GstElement *packet_modder;

  GHashTable *tfrc_sources;
  struct TrackedSource *last_src;

  ExtensionType extension_type;
  guint extension_id;
};

struct _FsRtpTfrcClass
{
  GstObjectClass parent_class;
};


GType fs_rtp_tfrc_get_type (void);

FsRtpTfrc *fs_rtp_tfrc_new (GObject *rtpsession, GstPad *inrtp,
    GstPad *inrtcp, GstElement **send_filter);


G_END_DECLS

#endif /* __FS_RTP_TFRC_H__ */

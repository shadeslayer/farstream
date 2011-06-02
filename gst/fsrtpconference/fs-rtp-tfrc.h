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

  TfrcSender *sender;
  GstClockID sender_id;
  TfrcIsDataLimited *idl;
  guint64 send_ts_base;
  guint64 send_ts_cycles;
  guint32 fb_last_ts;
  guint64 fb_ts_cycles;

  TfrcReceiver *receiver;
  GstClockID receiver_id;
  guint32 seq_cycles;
  guint32 last_seq;
  guint64 ts_cycles;
  guint32 last_ts;
  guint64 last_now;
  guint32 last_rtt;
  gboolean send_feedback;

  guint64 next_feedback_timer;

  gboolean got_nohdr_pkt;
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
  struct TrackedSource *initial_src;
  struct TrackedSource *last_src;

  /* Sender stuff */
  gint byte_reservoir;
  GstClockTime last_sent_ts;

  ExtensionType extension_type;
  guint extension_id;

  gboolean pts[128];
};

struct _FsRtpTfrcClass
{
  GstObjectClass parent_class;
};


GType fs_rtp_tfrc_get_type (void);

FsRtpTfrc *fs_rtp_tfrc_new (GObject *rtpsession,
    GstPad *inrtp,
    GstPad *inrtcp,
    GstElement **send_filter);

void fs_rtp_tfrc_destroy (FsRtpTfrc *self);

void fs_rtp_tfrc_filter_codecs (GList **codec_associations,
    GList **header_extensions);

void fs_rtp_tfrc_codecs_updated (FsRtpTfrc *self,
    GList *codec_associations,
    GList *header_extensions);

gboolean fs_rtp_tfrc_is_enabled (FsRtpTfrc *self, guint pt);

G_END_DECLS

#endif /* __FS_RTP_TFRC_H__ */

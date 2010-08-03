/*
 * Farsight2 - Farsight RTP Rate Control
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

typedef struct _FsRtpTfrc FsRtpRateControl;
typedef struct _FsRtpTfrcClass FsRtpRateControlClass;
typedef struct _FsRtpTfrcPrivate FsRtpRateControlPrivate;

struct _FsRtpTfrcClass
{
  GstObjectClass parent_class;
};

/**
 * FsRtpTfrc:
 *
 */
struct _FsRtpTfrc
{
  GstObject parent;

  FsRtpTfrcPrivate *priv;
};

#define FS_RTP_TFRC_LOCK(tfrc)  \
  GST_OBJECT_LOCK(tfrc)
#define FS_RTP_TFRC_UNLOCK(tfrc) \
  GST_OBJECT_UNLOCK(tfrc)
#define FS_RTP_TFRC_GET_LOCK(tfrc) \
  GST_OBJECT_GET_LOCK(tfrc)


GType fs_rtp_tfrc_get_type (void);


G_END_DECLS

#endif /* __FS_RTP_TFRC_H__ */

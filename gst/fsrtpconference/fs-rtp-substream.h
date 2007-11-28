/*
 * Farsight2 - Farsight RTP Sub Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-substream.h - A Farsight RTP Substream gobject
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#ifndef __FS_RTP_SUBSTREAM_H__
#define __FS_RTP_SUBSTREAM_H__

#include <gst/gst.h>

#include "fs-rtp-conference.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_SUB_STREAM \
  (fs_rtp_sub_stream_get_type())
#define FS_RTP_SUB_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_SUB_STREAM, FsRtpSubStream))
#define FS_RTP_SUB_STREAM_CLASS(klass) \
 (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_SUB_STREAM, FsRtpSubStreamClass))
#define FS_IS_RTP_SUB_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_SUB_STREAM))
#define FS_IS_RTP_SUB_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_SUB_STREAM))
#define FS_RTP_SUB_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_SUB_STREAM,   \
    FsRtpSubStreamClass))
#define FS_RTP_SUB_STREAM_CAST(obj) ((FsRtpSubStream*) (obj))

typedef struct _FsRtpSubStream FsRtpSubStream;
typedef struct _FsRtpSubStreamClass FsRtpSubStreamClass;
typedef struct _FsRtpSubStreamPrivate FsRtpSubStreamPrivate;

struct _FsRtpSubStreamClass
{
  GObjectClass parent_class;

};

/**
 * FsRtpSubStream:
 *
 */
struct _FsRtpSubStream
{
  GObject parent;
  FsRtpSubStreamPrivate *priv;
};

GType fs_rtp_sub_stream_get_type (void);

FsRtpSubStream *fs_rtp_substream_new ( FsRtpConference *conference, GstPad *pad,
  guint32 ssrc, guint pt, GError **error);


gboolean fs_rtp_sub_stream_try_add_codecbin (FsRtpSubStream *substream);


G_END_DECLS

#endif /* __FS_RTP_SUBSTREAM_H__ */

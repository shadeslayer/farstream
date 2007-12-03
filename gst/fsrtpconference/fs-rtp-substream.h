/*
 * Farsight2 - Farsight RTP Sub Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-substream.h - A Farsight RTP Substream gobject
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


#ifndef __FS_RTP_SUBSTREAM_H__
#define __FS_RTP_SUBSTREAM_H__

#include <gst/gst.h>

#include "fs-rtp-conference.h"
#include "fs-rtp-session.h"

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

FsRtpSubStream *fs_rtp_substream_new (FsRtpConference *conference,
    FsRtpSession *session,
    GstPad *pad,
    guint32 ssrc,
    guint pt,
    GError **error);


gboolean fs_rtp_sub_stream_add_codecbin (FsRtpSubStream *substream,
    GError **error);

void fs_rtp_sub_stream_stop (FsRtpSubStream *substream);

void fs_rtp_sub_stream_block (FsRtpSubStream *substream,
    GstPadBlockCallback callback,
    gpointer user_data);


GstPad *fs_rtp_sub_stream_get_output_ghostpad (FsRtpSubStream *substream,
    GError **error);


void fs_rtp_sub_stream_invalidate_codec_locked (FsRtpSubStream *substream,
    gint pt,
    const FsCodec *codec);


G_END_DECLS

#endif /* __FS_RTP_SUBSTREAM_H__ */

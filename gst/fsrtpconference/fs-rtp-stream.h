/*
 * Farsight2 - Farsight RTP Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-stream.h - A Farsight RTP Stream
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

#ifndef __FS_RTP_STREAM_H__
#define __FS_RTP_STREAM_H__

#include <gst/farsight/fs-stream.h>
#include <gst/farsight/fs-stream-transmitter.h>

#include "fs-rtp-participant.h"
#include "fs-rtp-session.h"
#include "fs-rtp-substream.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_STREAM \
  (fs_rtp_stream_get_type())
#define FS_RTP_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_STREAM, FsRtpStream))
#define FS_RTP_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_STREAM, FsRtpStreamClass))
#define FS_IS_RTP_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_STREAM))
#define FS_IS_RTP_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_STREAM))
#define FS_RTP_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_STREAM, FsRtpStreamClass))
#define FS_RTP_STREAM_CAST(obj) ((FsRtpStream*) (obj))

typedef struct _FsRtpStream FsRtpStream;
typedef struct _FsRtpStreamClass FsRtpStreamClass;
typedef struct _FsRtpStreamPrivate FsRtpStreamPrivate;

struct _FsRtpStreamClass
{
  FsStreamClass parent_class;

};

/**
 * FsRtpStream:
 *
 */
struct _FsRtpStream
{
  FsStream parent;
  FsRtpStreamPrivate *priv;
};

GType fs_rtp_stream_get_type (void);

FsRtpStream *fs_rtp_stream_new (FsRtpSession *session,
                                FsRtpParticipant *participant,
                                FsStreamDirection direction,
                                FsStreamTransmitter *stream_transmitter,
                                GError **error);

void fs_rtp_stream_add_substream (FsRtpStream *stream,
  FsRtpSubStream *substream);

G_END_DECLS

#endif /* __FS_RTP_STREAM_H__ */

/*
 * Farsight2 - Farsight Raw Stream
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007,2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Mike Ruprecht <mike.ruprecht@collabora.co.uk>
 *
 * fs-raw-stream.h - A Farsight Raw Stream
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

#ifndef __FS_RAW_STREAM_H__
#define __FS_RAW_STREAM_H__

#include <gst/farsight/fs-stream.h>
#include <gst/farsight/fs-stream-transmitter.h>

#include "fs-raw-participant.h"
#include "fs-raw-session.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RAW_STREAM \
  (fs_raw_stream_get_type ())
#define FS_RAW_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RAW_STREAM, FsRawStream))
#define FS_RAW_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RAW_STREAM, FsRawStreamClass))
#define FS_IS_RAW_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RAW_STREAM))
#define FS_IS_RAW_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RAW_STREAM))
#define FS_RAW_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RAW_STREAM, FsRawStreamClass))
#define FS_RAW_STREAM_CAST(obj) ((FsRawStream*) (obj))

typedef struct _FsRawStream FsRawStream;
typedef struct _FsRawStreamClass FsRawStreamClass;
typedef struct _FsRawStreamPrivate FsRawStreamPrivate;


struct _FsRawStreamClass
{
  FsStreamClass parent_class;

};

/**
 * FsRawStream:
 *
 */
struct _FsRawStream
{
  FsStream parent;

  /*< private >*/
  FsRawStreamPrivate *priv;
};

GType fs_raw_stream_get_type (void);

FsRawStream *fs_raw_stream_new (FsRawSession *session,
    FsRawParticipant *participant,
    FsStreamDirection direction,
    FsRawConference *conference,
    FsStreamTransmitter *stream_transmitter,
    GstPad *transmitter_pad,
    GError **error);


G_END_DECLS

#endif /* __FS_RAW_STREAM_H__ */

/*
 * Farsight2 - Farsight MSN Stream
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-stream.h - A Farsight MSN Stream
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

#ifndef __FS_MSN_STREAM_H__
#define __FS_MSN_STREAM_H__

#include <gst/farsight/fs-stream.h>

#include "fs-msn-participant.h"
#include "fs-msn-session.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_MSN_STREAM \
  (fs_msn_stream_get_type ())
#define FS_MSN_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_MSN_STREAM, FsMsnStream))
#define FS_MSN_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_MSN_STREAM, FsMsnStreamClass))
#define FS_IS_MSN_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_MSN_STREAM))
#define FS_IS_MSN_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_MSN_STREAM))
#define FS_MSN_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_MSN_STREAM, FsMsnStreamClass))
#define FS_MSN_STREAM_CAST(obj) ((FsMsnStream*) (obj))

typedef struct _FsMsnStream FsMsnStream;
typedef struct _FsMsnStreamClass FsMsnStreamClass;
typedef struct _FsMsnStreamPrivate FsMsnStreamPrivate;


struct _FsMsnStreamClass
{
  FsStreamClass parent_class;

};

/**
 * FsMsnStream:
 *
 */
struct _FsMsnStream
{
  FsStream parent;

  /*< private >*/
  FsMsnStreamPrivate *priv;
};

GType fs_msn_stream_get_type (void);

FsMsnStream *fs_msn_stream_new (FsMsnSession *session,
    FsMsnParticipant *participant,
    FsStreamDirection direction,
    FsMsnConference *conference);

void fs_msn_stream_set_tos_locked (FsMsnStream *self, gint tos);


G_END_DECLS

#endif /* __FS_MSN_STREAM_H__ */

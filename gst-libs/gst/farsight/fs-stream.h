/*
 * Farsight2 - Farsight Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-stream.h - A Farsight Stream (base implementation)
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

#ifndef __FS_STREAM_H__
#define __FS_STREAM_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_STREAM \
  (fs_stream_get_type())
#define FS_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_STREAM, FsStream))
#define FS_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_STREAM, FsStreamClass))
#define FS_IS_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_STREAM))
#define FS_IS_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_STREAM))
#define FS_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_STREAM, FsStreamClass))

typedef struct _FsStream FsStream;
typedef struct _FsStreamClass FsStreamClass;
typedef struct _FsStreamPrivate FsStreamPrivate;

struct _FsStreamClass
{
  GObjectClass parent_class;

  /*virtual functions */

  /*< private >*/
  gpointer _padding[8];
};

/**
 * FsStream:
 *
 */
struct _FsStream
{
  GObject parent;

  /*< private >*/
  gpointer _padding[8];
  FsStreamPrivate *priv;
};

GType fs_stream_get_type (void);

G_END_DECLS

#endif /* __FS_STREAM_H__ */

/*
 * Farstream - Farstream Shared Memory Stream Transmitter
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-shm-stream-transmitter.h - A Farstream Shared Memory stream transmitter
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

#ifndef __FS_SHM_STREAM_TRANSMITTER_H__
#define __FS_SHM_STREAM_TRANSMITTER_H__

#include <glib.h>
#include <glib-object.h>

#include <gst/farstream/fs-stream-transmitter.h>
#include <gst/farstream/fs-plugin.h>
#include "fs-shm-transmitter.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_SHM_STREAM_TRANSMITTER \
  (fs_shm_stream_transmitter_get_type ())
#define FS_SHM_STREAM_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_SHM_STREAM_TRANSMITTER, \
                              FsShmStreamTransmitter))
#define FS_SHM_STREAM_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_SHM_STREAM_TRANSMITTER, \
                           FsShmStreamTransmitterClass))
#define FS_IS_SHM_STREAM_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_SHM_STREAM_TRANSMITTER))
#define FS_IS_SHM_STREAM_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_SHM_STREAM_TRANSMITTER))
#define FS_SHM_STREAM_TRANSMITTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_SHM_STREAM_TRANSMITTER, \
                              FsShmStreamTransmitterClass))
#define FS_SHM_STREAM_TRANSMITTER_CAST(obj) ((FsShmStreamTransmitter *) (obj))

typedef struct _FsShmStreamTransmitter FsShmStreamTransmitter;
typedef struct _FsShmStreamTransmitterClass FsShmStreamTransmitterClass;
typedef struct _FsShmStreamTransmitterPrivate FsShmStreamTransmitterPrivate;

/**
 * FsShmStreamTransmitterClass:
 * @parent_class: Our parent
 *
 * The Shared Memory stream transmitter class
 */

struct _FsShmStreamTransmitterClass
{
  FsStreamTransmitterClass parent_class;

  /*virtual functions */
  /*< private >*/
};

/**
 * FsShmStreamTransmitter:
 * @parent: Parent object
 *
 * All members are private, access them using methods and properties
 */
struct _FsShmStreamTransmitter
{
  FsStreamTransmitter parent;

  /*< private >*/
  FsShmStreamTransmitterPrivate *priv;
};

GType fs_shm_stream_transmitter_register_type (FsPlugin *module);

GType fs_shm_stream_transmitter_get_type (void);

FsShmStreamTransmitter *
fs_shm_stream_transmitter_newv (FsShmTransmitter *transmitter,
  guint n_parameters, GParameter *parameters, GError **error);

G_END_DECLS

#endif /* __FS_SHM_STREAM_TRANSMITTER_H__ */

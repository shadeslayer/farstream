/*
 * Farsight2 - Farsight Shared Memory Transmitter
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-shm-transmitter.h - A Farsight Shared Memory transmitter
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

#ifndef __FS_SHM_TRANSMITTER_H__
#define __FS_SHM_TRANSMITTER_H__

#include <gst/farsight/fs-transmitter.h>

#include <gst/gst.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_SHM_TRANSMITTER \
  (fs_shm_transmitter_get_type ())
#define FS_SHM_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_SHM_TRANSMITTER, \
    FsShmTransmitter))
#define FS_SHM_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_SHM_TRANSMITTER, \
    FsShmTransmitterClass))
#define FS_IS_SHM_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_SHM_TRANSMITTER))
#define FS_IS_SHM_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_SHM_TRANSMITTER))
#define FS_SHM_TRANSMITTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_SHM_TRANSMITTER, \
    FsShmTransmitterClass))
#define FS_SHM_TRANSMITTER_CAST(obj) ((FsShmTransmitter *) (obj))

typedef struct _FsShmTransmitter FsShmTransmitter;
typedef struct _FsShmTransmitterClass FsShmTransmitterClass;
typedef struct _FsShmTransmitterPrivate FsShmTransmitterPrivate;

/**
 * FsShmTransmitterClass:
 * @parent_class: Our parent
 *
 * The Shared Memory transmitter class
 */

struct _FsShmTransmitterClass
{
  FsTransmitterClass parent_class;
};

/**
 * FsShmTransmitter:
 * @parent: Parent object
 *
 * All members are private, access them using methods and properties
 */
struct _FsShmTransmitter
{
  FsTransmitter parent;

  /* The number of components (READONLY) */
  gint components;

  /*< private >*/
  FsShmTransmitterPrivate *priv;
};

GType fs_shm_transmitter_get_type (void);

typedef struct _ShmSrc ShmSrc;
typedef struct _ShmSink ShmSink;

typedef void (*got_buffer) (GstBuffer *buffer, guint component, gpointer data);
typedef void (*ready) (guint component, gchar *path, gpointer data);
typedef void (*connection) (guint component, gint id, gpointer data);

ShmSrc *fs_shm_transmitter_get_shm_src (FsShmTransmitter *self,
    guint component,
    const gchar *path,
    got_buffer got_buffer_func,
    connection disconnected_func,
    gpointer cb_data,
    GError **error);

gboolean fs_shm_transmitter_check_shm_src (FsShmTransmitter *self,
    ShmSrc *shm,
    const gchar *path);

ShmSink *fs_shm_transmitter_get_shm_sink (FsShmTransmitter *self,
    guint component,
    const gchar *path,
    ready ready_func,
    connection connected_fubnc,
    gpointer cb_data,
    GError **error);

gboolean fs_shm_transmitter_check_shm_sink (FsShmTransmitter *self,
    ShmSink *shm,
    const gchar *path);

void fs_shm_transmitter_sink_set_sending (FsShmTransmitter *self,
    ShmSink *shm, gboolean sending);



G_END_DECLS

#endif /* __FS_SHM_TRANSMITTER_H__ */

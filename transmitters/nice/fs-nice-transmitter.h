/*
 * Farsight2 - Farsight libnice Transmitter
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-nice-transmitter.h - A Farsight libnice transmitter
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

#ifndef __FS_NICE_TRANSMITTER_H__
#define __FS_NICE_TRANSMITTER_H__

#include <gst/farsight/fs-transmitter.h>

#include <gst/gst.h>
#include <agent.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_NICE_TRANSMITTER \
  (fs_nice_transmitter_get_type ())
#define FS_NICE_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_NICE_TRANSMITTER, \
    FsNiceTransmitter))
#define FS_NICE_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_NICE_TRANSMITTER, \
    FsNiceTransmitterClass))
#define FS_IS_NICE_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_NICE_TRANSMITTER))
#define FS_IS_NICE_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_NICE_TRANSMITTER))
#define FS_NICE_TRANSMITTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_NICE_TRANSMITTER, \
    FsNiceTransmitterClass))
#define FS_NICE_TRANSMITTER_CAST(obj) ((FsNiceTransmitter *) (obj))

typedef struct _FsNiceTransmitter FsNiceTransmitter;
typedef struct _FsNiceTransmitterClass FsNiceTransmitterClass;
typedef struct _FsNiceTransmitterPrivate FsNiceTransmitterPrivate;

/**
 * FsNiceTransmitterClass:
 * @parent_class: Our parent
 *
 * The Nice UDP transmitter class
 */

struct _FsNiceTransmitterClass
{
  FsTransmitterClass parent_class;
};

/**
 * FsNiceTransmitter:
 *
 * All members are private, access them using methods and properties
 */
struct _FsNiceTransmitter
{
  FsTransmitter parent;

  /* The number of components (READONLY)*/
  gint components;

  /* The agent, don't modify the pointer */
  NiceAgent *agent;

  /*< private >*/
  FsNiceTransmitterPrivate *priv;
};


GType fs_nice_transmitter_get_type (void);

struct _NiceGstStream;
typedef struct _NiceGstStream NiceGstStream;

NiceGstStream *fs_nice_transmitter_add_gst_stream (FsNiceTransmitter *self,
    guint stream_id,
    GError **error);

void fs_nice_transmitter_free_gst_stream (FsNiceTransmitter *self,
    NiceGstStream *ns);



G_END_DECLS

#endif /* __FS_NICE_TRANSMITTER_H__ */

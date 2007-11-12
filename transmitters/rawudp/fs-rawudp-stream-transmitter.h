/*
 * Farsight2 - Farsight RAW UDP with STUN Stream Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-stream-transmitter.h - A Farsight UDP stream transmitter with STUN
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

#ifndef __FS_RAWUDP_STREAM_TRANSMITTER_H__
#define __FS_RAWUDP_STREAM_TRANSMITTER_H__

#include <glib.h>
#include <glib-object.h>

#include <gst/farsight/fs-stream-transmitter.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RAWUDP_STREAM_TRANSMITTER \
  (fs_rawudp_stream_transmitter_get_type())
#define FS_RAWUDP_STREAM_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RAWUDP_STREAM_TRANSMITTER, \
                              FsRawUdpStreamTransmitter))
#define FS_RAWUDP_STREAM_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RAWUDP_STREAM_TRANSMITTER, \
                           FsRawUdpStreamTransmitterClass))
#define FS_IS_RAWUDP_STREAM_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RAWUDP_STREAM_TRANSMITTER))
#define FS_IS_RAWUDP_STREAM_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RAWUDP_STREAM_TRANSMITTER))
#define FS_RAWUDP_STREAM_TRANSMITTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RAWUDP_STREAM_TRANSMITTER, \
                              FsRawUdpStreamTransmitterClass))
#define FS_RAWUDP_STREAM_TRANSMITTER_CAST(obj) ((FsRawUdpStreamTransmitter *) (obj))

typedef struct _FsRawUdpStreamTransmitter FsRawUdpStreamTransmitter;
typedef struct _FsRawUdpStreamTransmitterClass FsRawUdpStreamTransmitterClass;
typedef struct _FsRawUdpStreamTransmitterPrivate FsRawUdpStreamTransmitterPrivate;

/**
 * FsRawUdpStreamTransmitterClass:
 * @parent_class: Our parent
 *
 * The Raw UDP stream transmitter class
 */

struct _FsRawUdpStreamTransmitterClass
{
  FsStreamTransmitter parent_class;

  /*virtual functions */
  /*< private >*/
  gpointer _padding[8];
};

/**
 * FsRawUdpStreamTransmitter:
 *
 * All members are private, access them using methods and properties
 */
struct _FsRawUdpStreamTransmitter
{
  FsStreamTransmitterClass parent;

  /*< private >*/
  FsRawUdpStreamTransmitterPrivate *priv;
  gpointer _padding[8];
};

GType fs_rawudp_stream_transmitter_get_type (void);

FsRawUdpStreamTransmitter *
fs_rawudp_stream_transmitter_newv (guint n_parameters, GParameter *parameters,
  GError **error);

G_END_DECLS

#endif /* __FS_RAWUDP_STREAM_TRANSMITTER_H__ */

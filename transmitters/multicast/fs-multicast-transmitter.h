/*
 * Farsight2 - Farsight Multicast UDP Transmitter
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-multicast-transmitter.h - A Farsight Multicast UDP transmitter
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

#ifndef __FS_MULTICAST_TRANSMITTER_H__
#define __FS_MULTICAST_TRANSMITTER_H__

#include <gst/farsight/fs-transmitter.h>

#include <gst/gst.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_MULTICAST_TRANSMITTER \
  (fs_multicast_transmitter_get_type ())
#define FS_MULTICAST_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_MULTICAST_TRANSMITTER, \
    FsMulticastTransmitter))
#define FS_MULTICAST_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_MULTICAST_TRANSMITTER, \
    FsMulticastTransmitterClass))
#define FS_IS_MULTICAST_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_MULTICAST_TRANSMITTER))
#define FS_IS_MULTICAST_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_MULTICAST_TRANSMITTER))
#define FS_MULTICAST_TRANSMITTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_MULTICAST_TRANSMITTER, \
    FsMulticastTransmitterClass))
#define FS_MULTICAST_TRANSMITTER_CAST(obj) ((FsMulticastTransmitter *) (obj))

typedef struct _FsMulticastTransmitter FsMulticastTransmitter;
typedef struct _FsMulticastTransmitterClass FsMulticastTransmitterClass;
typedef struct _FsMulticastTransmitterPrivate FsMulticastTransmitterPrivate;

/**
 * FsMulticastTransmitterClass:
 * @parent_class: Our parent
 *
 * The Multicast UDP transmitter class
 */

struct _FsMulticastTransmitterClass
{
  FsTransmitterClass parent_class;
};

/**
 * FsMulticastTransmitter:
 *
 * All members are private, access them using methods and properties
 */
struct _FsMulticastTransmitter
{
  FsTransmitter parent;

  /* The number of components (READONLY)*/
  gint components;

  /*< private >*/
  FsMulticastTransmitterPrivate *priv;
};

/* Private declarations */
typedef struct _UdpSock UdpSock;

GType fs_multicast_transmitter_get_type (void);

UdpSock *fs_multicast_transmitter_get_udpsock (FsMulticastTransmitter *trans,
    guint component_id,
    const gchar *local_ip,
    const gchar *multicast_ip,
    guint16 port,
    guint8 ttl,
    gboolean recv,
    GError **error);

void fs_multicast_transmitter_put_udpsock (FsMulticastTransmitter *trans,
    UdpSock *udpsock);

void fs_multicast_transmitter_udpsock_inc_sending (UdpSock *udpsock);
void fs_multicast_transmitter_udpsock_dec_sending (UdpSock *udpsock);


G_END_DECLS

#endif /* __FS_MULTICAST_TRANSMITTER_H__ */

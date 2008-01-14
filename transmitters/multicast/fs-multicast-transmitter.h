/*
 * Farsight2 - Farsight Multicast UDP Transmitter
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-rawudp-transmitter.h - A Farsight Multicast UDP transmitter
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

#include <arpa/inet.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_MULTICAST_TRANSMITTER \
  (fs_multicast_transmitter_get_type())
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

/* Private declaration */
typedef struct _UdpPort UdpPort;

GType fs_multicast_transmitter_get_type (void);



UdpPort *fs_multicast_transmitter_get_udpport (FsMulticastTransmitter *trans,
  guint component_id, const gchar *requested_ip, guint requested_port,
  GError **error);

void fs_multicast_transmitter_put_udpport (FsMulticastTransmitter *trans,
  UdpPort *udpport);

void fs_multicast_transmitter_udpport_add_dest (UdpPort *udpport,
  const gchar *ip, gint port);
void fs_multicast_transmitter_udpport_remove_dest (UdpPort *udpport,
  const gchar *ip, gint port);

gboolean fs_multicast_transmitter_udpport_sendto (UdpPort *udpport,
  gchar *msg, size_t len, const struct sockaddr *to, socklen_t tolen,
  GError **error);

gulong fs_multicast_transmitter_udpport_connect_recv (UdpPort *udpport,
  GCallback callback, gpointer user_data);
void fs_multicast_transmitter_udpport_disconnect_recv (UdpPort *udpport,
  gulong id);

gboolean fs_multicast_transmitter_udpport_is_pad (UdpPort *udpport,
    GstPad *pad);

gboolean fs_multicast_transmitter_udpport_get_port (UdpPort *udpport);



G_END_DECLS

#endif /* __FS_MULTICAST_TRANSMITTER_H__ */

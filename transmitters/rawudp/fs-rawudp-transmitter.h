/*
 * Farsight2 - Farsight RAW UDP with STUN Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rawudp-transmitter.h - A Farsight UDP transmitter with STUN
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

#ifndef __FS_RAWUDP_TRANSMITTER_H__
#define __FS_RAWUDP_TRANSMITTER_H__

#include <gst/farsight/fs-transmitter.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RAWUDP_TRANSMITTER \
  (fs_rawudp_transmitter_get_type())
#define FS_RAWUDP_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RAWUDP_TRANSMITTER, \
    FsRawUdpTransmitter))
#define FS_RAWUDP_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RAWUDP_TRANSMITTER, \
    FsRawUdpTransmitterClass))
#define FS_IS_RAWUDP_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RAWUDP_TRANSMITTER))
#define FS_IS_RAWUDP_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RAWUDP_TRANSMITTER))
#define FS_RAWUDP_TRANSMITTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RAWUDP_TRANSMITTER, \
    FsRawUdpTransmitterClass))
#define FS_RAWUDP_TRANSMITTER_CAST(obj) ((FsRawUdpTransmitter *) (obj))

typedef struct _FsRawUdpTransmitter FsRawUdpTransmitter;
typedef struct _FsRawUdpTransmitterClass FsRawUdpTransmitterClass;
typedef struct _FsRawUdpTransmitterPrivate FsRawUdpTransmitterPrivate;

/**
 * FsRawUdpTransmitterClass:
 * @parent_class: Our parent
 *
 * The Raw UDP transmitter class
 */

struct _FsRawUdpTransmitterClass
{
  FsTransmitterClass parent_class;

  /*< private >*/
  gpointer _padding[8];
};

/**
 * FsRawUdpTransmitter:
 *
 * All members are private, access them using methods and properties
 */
struct _FsRawUdpTransmitter
{
  FsTransmitter parent;

  /*< private >*/
  FsRawUdpTransmitterPrivate *priv;
  gpointer _padding[8];
};

/* Private declaration */
typedef struct _UdpStream UdpStream;

GType fs_rawudp_transmitter_get_type (void);



UdpStream *fs_rawudp_transmitter_get_udpstream (FsRawUdpTransmitter *trans,
  const gchar *requested_ip, guint requested_port,
  const gchar *requested_rtcp_ip, guint requested_rtcp_port,
  GError **error);

void fs_rawudp_transmitter_put_udpstream (FsRawUdpTransmitter *trans,
  UdpStream *udpstream);

void fs_rawudp_transmitter_udpstream_add_dest (UdpStream *udpstream,
  const gchar *ip, gint port, gboolean is_rtcp);
void fs_rawudp_transmitter_udpstream_remove_dest (UdpStream *udpstream,
  const gchar *ip, gint port, gboolean is_rtcp);



G_END_DECLS

#endif /* __FS_RAWUDP_TRANSMITTER_H__ */

/*
 * Farsight2 - Farsight UPnP IGD abstraction
 *
 * Copyright 2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2008 Nokia Corp.
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

#ifndef __FS_UPNP_SIMPLE_IGD_H__
#define __FS_UPNP_SIMPLE_IGD_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_UPNP_SIMPLE_IGD       \
  (fs_upnp_simple_igd_get_type ())
#define FS_UPNP_SIMPLE_IGD(obj)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_UPNP_SIMPLE_IGD, \
      FsUpnpSimpleIgd))
#define FS_UPNP_SIMPLE_IGD_CLASS(klass)                       \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_UPNP_SIMPLE_IGD,  \
      FsUpnpSimpleIgdClass))
#define FS_IS_UPNP_SIMPLE_IGD(obj)                            \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_UPNP_SIMPLE_IGD))
#define FS_IS_UPNP_SIMPLE_IGD_CLASS(klass)                    \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_UPNP_SIMPLE_IGD))
#define FS_UPNP_SIMPLE_IGD_GET_CLASS(obj)                     \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_UPNP_SIMPLE_IGD, \
      FsUpnpSimpleIgdClass))
#define FS_UPNP_SIMPLE_IGD_CAST(obj)                          \
  ((FsUpnpSimpleIgd *) (obj))

typedef struct _FsUpnpSimpleIgd FsUpnpSimpleIgd;
typedef struct _FsUpnpSimpleIgdClass FsUpnpSimpleIgdClass;
typedef struct _FsUpnpSimpleIgdPrivate FsUpnpSimpleIgdPrivate;

/**
 * FsUpnpSimpleIgdClass:
 * @parent_class: Our parent
 *
 * The Raw UDP component transmitter class
 */

struct _FsUpnpSimpleIgdClass
{
  GObjectClass parent_class;

  /*virtual functions */
  /*< private >*/
};

/**
 * FsUpnpSimpleIgd:
 *
 * All members are private, access them using methods and properties
 */
struct _FsUpnpSimpleIgd
{
  GObject parent;

  /*< private >*/
  FsUpnpSimpleIgdPrivate *priv;
};

GType fs_upnp_simple_igd_get_type (void);

FsUpnpSimpleIgd *
fs_upnp_simple_igd_new (GMainContext *context);

void
fs_upnp_simple_igd_add_port (FsUpnpSimpleIgd *self,
    const gchar *protocol,
    guint16 external_port,
    const gchar *local_ip,
    guint16 local_port,
    guint32 lease_duration,
    const gchar *description);

void
fs_upnp_simple_igd_remove_port (FsUpnpSimpleIgd *self,
    const gchar *protocol,
    guint external_port);

G_END_DECLS

#endif /* __FS_UPNP_SIMPLE_IGD_H__ */

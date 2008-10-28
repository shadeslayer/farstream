/*
 * Farsight2 - Farsight RAW UDP with STUN Component Transmitter
 *
 * Copyright 2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2008 Nokia Corp.
 *
 * fs-rawudp-component.h - A Farsight UDP component transmitter with STUN
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

#ifndef __FS_RAWUDP_COMPONENT_H__
#define __FS_RAWUDP_COMPONENT_H__

#include <glib.h>
#include <glib-object.h>

#include <gst/farsight/fs-stream-transmitter.h>
#include <gst/farsight/fs-plugin.h>
#include "fs-rawudp-transmitter.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RAWUDP_COMPONENT       \
  (fs_rawudp_component_get_type ())
#define FS_RAWUDP_COMPONENT(obj)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RAWUDP_COMPONENT, \
      FsRawUdpComponent))
#define FS_RAWUDP_COMPONENT_CLASS(klass)                       \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RAWUDP_COMPONENT,  \
      FsRawUdpComponentClass))
#define FS_IS_RAWUDP_COMPONENT(obj)                            \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RAWUDP_COMPONENT))
#define FS_IS_RAWUDP_COMPONENT_CLASS(klass)                    \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RAWUDP_COMPONENT))
#define FS_RAWUDP_COMPONENT_GET_CLASS(obj)                     \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RAWUDP_COMPONENT, \
      FsRawUdpComponentClass))
#define FS_RAWUDP_COMPONENT_CAST(obj)                          \
  ((FsRawUdpComponent *) (obj))

typedef struct _FsRawUdpComponent FsRawUdpComponent;
typedef struct _FsRawUdpComponentClass FsRawUdpComponentClass;
typedef struct _FsRawUdpComponentPrivate FsRawUdpComponentPrivate;

/**
 * FsRawUdpComponentClass:
 * @parent_class: Our parent
 *
 * The Raw UDP component transmitter class
 */

struct _FsRawUdpComponentClass
{
  GObjectClass parent_class;

  /*virtual functions */
  /*< private >*/
};

/**
 * FsRawUdpComponent:
 *
 * All members are private, access them using methods and properties
 */
struct _FsRawUdpComponent
{
  GObject parent;

  /*< private >*/
  FsRawUdpComponentPrivate *priv;
};

GType fs_rawudp_component_register_type (FsPlugin *module);

GType fs_rawudp_component_get_type (void);

FsRawUdpComponent *
fs_rawudp_component_new (
    guint component,
    FsRawUdpTransmitter *trans,
    gboolean associate_on_source,
    const gchar *ip,
    guint port,
    const gchar *stun_ip,
    guint stun_port,
    guint stun_timeout,
    gboolean upnp_mapping,
    gboolean upnp_discovery,
    guint upnp_mapping_timeout,
    guint upnp_discovery_timeout,
    gpointer upnp_igd,
    guint *used_port,
    GError **error);

gboolean
fs_rawudp_component_set_remote_candidate (FsRawUdpComponent *self,
    FsCandidate *candidate,
    GError **error);

gboolean
fs_rawudp_component_gather_local_candidates (FsRawUdpComponent *self,
    GError **error);

void
fs_rawudp_component_stop (FsRawUdpComponent *self);

G_END_DECLS

#endif /* __FS_RAWUDP_COMPONENT_H__ */

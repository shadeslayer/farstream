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


#include "fs-upnp-simple-igd.h"

#include <libgupnp/gupnp-control-point.h>


struct _FsUpnpSimpleIgdPrivate
{
  GMainContext *context;
};

/* signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
};


// static guint signals[LAST_SIGNAL] = { 0 };


#define FS_UPNP_SIMPLE_IGD_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_UPNP_SIMPLE_IGD,             \
   FsUpnpSimpleIgdPrivate))

G_DEFINE_TYPE (FsUpnpSimpleIgd, fs_upnp_simple_igd, G_TYPE_OBJECT);


static void fs_upnp_simple_igd_dispose (GObject *object);
static void fs_upnp_simple_igd_finalize (GObject *object);
static void fs_upnp_simple_igd_constructed (GObject *object);

static void fs_upnp_simple_igd_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static void fs_upnp_simple_igd_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);


static void
fs_upnp_simple_igd_class_init (FsUpnpSimpleIgdClass *klass)
{
 GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = fs_upnp_simple_igd_constructed;
  gobject_class->dispose = fs_upnp_simple_igd_dispose;
  gobject_class->finalize = fs_upnp_simple_igd_finalize;
  gobject_class->set_property = fs_upnp_simple_igd_set_property;
  gobject_class->get_property = fs_upnp_simple_igd_get_property;
}

static void
fs_upnp_simple_igd_init (FsUpnpSimpleIgd *self)
{
  self->priv = FS_UPNP_SIMPLE_IGD_GET_PRIVATE (self);
}

static void
fs_upnp_simple_igd_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (fs_upnp_simple_igd_parent_class)->constructed)
    G_OBJECT_CLASS (fs_upnp_simple_igd_parent_class)->constructed (object);
}

static void
fs_upnp_simple_igd_dispose (GObject *object)
{
  G_OBJECT_CLASS (fs_upnp_simple_igd_parent_class)->dispose (object);
}

static void
fs_upnp_simple_igd_finalize (GObject *object)
{
  FsUpnpSimpleIgd *self = FS_UPNP_SIMPLE_IGD_CAST (object);

  g_main_context_unref (self->priv->context);

  G_OBJECT_CLASS (fs_upnp_simple_igd_parent_class)->finalize (object);
}

static void
fs_upnp_simple_igd_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
}

static void
fs_upnp_simple_igd_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
}




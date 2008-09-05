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


#include "fs-upnp-simple-igd-thread.h"


struct _FsUpnpSimpleIgdThreadPrivate
{
  GThread *thread;
  GMainLoop *loop;
  GMainContext *context;
  GMutex *mutex;
};




#define FS_UPNP_SIMPLE_IGD_THREAD_GET_PRIVATE(o)                        \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_UPNP_SIMPLE_IGD_THREAD,    \
   FsUpnpSimpleIgdThreadPrivate))


G_DEFINE_TYPE (FsUpnpSimpleIgdThread, fs_upnp_simple_igd_thread,
    FS_TYPE_UPNP_SIMPLE_IGD);

static void fs_upnp_simple_igd_thread_constructed (GObject *object);
static void fs_upnp_simple_igd_thread_finalize (GObject *object);


static void
fs_upnp_simple_igd_thread_class_init (FsUpnpSimpleIgdThreadClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsUpnpSimpleIgdThreadPrivate));

  gobject_class->constructed = fs_upnp_simple_igd_thread_constructed;
  gobject_class->finalize = fs_upnp_simple_igd_thread_finalize;
}


static void
fs_upnp_simple_igd_thread_init (FsUpnpSimpleIgdThread *self)
{
  self->priv = FS_UPNP_SIMPLE_IGD_THREAD_GET_PRIVATE (self);

  self->priv->mutex = g_mutex_new ();
  self->priv->context = g_main_context_new ();
}

static void
fs_upnp_simple_igd_thread_finalize (GObject *object)
{
  FsUpnpSimpleIgdThread *self = FS_UPNP_SIMPLE_IGD_THREAD_CAST (object);

  g_main_context_unref (self->priv->context);
  g_mutex_free (self->priv->mutex);

  G_OBJECT_CLASS (fs_upnp_simple_igd_thread_parent_class)->finalize (object);
}

static void
fs_upnp_simple_igd_thread_constructed (GObject *object)
{
  //FsUpnpSimpleIgdThread *self = FS_UPNP_SIMPLE_IGD_THREAD_CAST (object);

  if (G_OBJECT_CLASS (fs_upnp_simple_igd_thread_parent_class)->constructed)
    G_OBJECT_CLASS (fs_upnp_simple_igd_thread_parent_class)->constructed (object);
}

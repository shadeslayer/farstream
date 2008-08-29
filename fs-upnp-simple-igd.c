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
  GMainLoop *loop;
  GMainContext *main_context;

  /* These are to be used only for the main thread */

  GUPnPContext *gupnp_context;
  GUPnPControlPoint *cp;

  GPtrArray *service_proxies;

  /* Everything below is protected by the mutex */

  GMutex *mutex;
  GThread *thread;
  guint request_timeout;
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
  PROP_REQUEST_TIMEOUT
};


// static guint signals[LAST_SIGNAL] = { 0 };


#define FS_UPNP_SIMPLE_IGD_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_UPNP_SIMPLE_IGD,             \
   FsUpnpSimpleIgdPrivate))



#define FS_UPNP_SIMPLE_IGD_LOCK(self) \
  g_mutex_lock ((self)->priv->mutex)
#define FS_UPNP_SIMPLE_IGD_UNLOCK(self) \
  g_mutex_unlock ((self)->priv->mutex)


G_DEFINE_TYPE (FsUpnpSimpleIgd, fs_upnp_simple_igd, G_TYPE_OBJECT);


static void fs_upnp_simple_igd_dispose (GObject *object);
static void fs_upnp_simple_igd_finalize (GObject *object);
static void fs_upnp_simple_igd_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static void fs_upnp_simple_igd_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);

static void
fs_upnp_simple_igd_class_init (FsUpnpSimpleIgdClass *klass)
{
 GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = fs_upnp_simple_igd_dispose;
  gobject_class->finalize = fs_upnp_simple_igd_finalize;
  gobject_class->set_property = fs_upnp_simple_igd_set_property;
  gobject_class->get_property = fs_upnp_simple_igd_get_property;

  g_object_class_install_property (gobject_class,
      PROP_REQUEST_TIMEOUT,
      g_param_spec_int ("request-timeout",
          "The timeout after which a request is considered to have failed",
          "After this timeout, the request is considered to have failed and"
          "is dropped.",
          0, G_MAXUINT, 5,
          G_PARAM_READWRITE));
}

static void
fs_upnp_simple_igd_init (FsUpnpSimpleIgd *self)
{
  self->priv = FS_UPNP_SIMPLE_IGD_GET_PRIVATE (self);

  self->priv->mutex = g_mutex_new ();
  self->priv->request_timeout = 5;

  self->priv->service_proxies = g_ptr_array_new ();
}

static void
fs_upnp_simple_igd_dispose (GObject *object)
{
  FsUpnpSimpleIgd *self = FS_UPNP_SIMPLE_IGD_CAST (object);

  g_return_if_fail (self->priv->thread == NULL);

  while (g_ptr_array_index(self->priv->service_proxies, 0))
    g_object_unref ( G_OBJECT (
            g_ptr_array_remove_index_fast (self->priv->service_proxies, 0)));

  if (self->priv->cp)
    g_object_unref (self->priv->cp);
  self->priv->cp = NULL;

  if (self->priv->gupnp_context)
    g_object_unref (self->priv->gupnp_context);
  self->priv->gupnp_context = NULL;

  G_OBJECT_CLASS (fs_upnp_simple_igd_parent_class)->dispose (object);
}

static void
fs_upnp_simple_igd_finalize (GObject *object)
{
  FsUpnpSimpleIgd *self = FS_UPNP_SIMPLE_IGD_CAST (object);

  g_main_context_unref (self->priv->main_context);

  g_ptr_array_free (self->priv->service_proxies, TRUE);

  g_mutex_free (self->priv->mutex);

  G_OBJECT_CLASS (fs_upnp_simple_igd_parent_class)->finalize (object);
}

static void
fs_upnp_simple_igd_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  FsUpnpSimpleIgd *self = FS_UPNP_SIMPLE_IGD_CAST (object);

  switch (prop_id) {
    case PROP_REQUEST_TIMEOUT:
      FS_UPNP_SIMPLE_IGD_LOCK (self);
      g_value_set_uint (value, self->priv->request_timeout);
      FS_UPNP_SIMPLE_IGD_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
fs_upnp_simple_igd_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  FsUpnpSimpleIgd *self = FS_UPNP_SIMPLE_IGD_CAST (object);

  switch (prop_id) {
    case PROP_REQUEST_TIMEOUT:
      FS_UPNP_SIMPLE_IGD_LOCK (self);
      self->priv->request_timeout = g_value_get_uint (value);
      FS_UPNP_SIMPLE_IGD_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_cp_service_avail (GUPnPControlPoint *cp,
    GUPnPServiceProxy *proxy,
    FsUpnpSimpleIgd *self)
{
}


static void
_cp_service_unavail (GUPnPControlPoint *cp,
    GUPnPServiceProxy *proxy,
    FsUpnpSimpleIgd *self)
{
}


static gboolean
fs_upnp_simple_igd_build (FsUpnpSimpleIgd *self, GError **error)
{
  self->priv->gupnp_context = gupnp_context_new (self->priv->main_context,
      NULL, 0, error);
  if (!self->priv->gupnp_context)
    return FALSE;

  self->priv->cp = gupnp_control_point_new (self->priv->gupnp_context,
      "urn:schemas-upnp-org:service:WANIPConnection:1");
  g_return_val_if_fail (self->priv->cp, FALSE);

  g_signal_connect (self->priv->cp, "service-proxy-available",
      G_CALLBACK (_cp_service_avail), self);
  g_signal_connect (self->priv->cp, "service-proxy-unavailable",
      G_CALLBACK (_cp_service_unavail), self);

  gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (self->priv->cp),
      TRUE);

  return TRUE;
}

FsUpnpSimpleIgd *
fs_upnp_simple_igd_new (GMainContext *main_context)
{
  FsUpnpSimpleIgd *self = g_object_new (FS_TYPE_UPNP_SIMPLE_IGD, NULL);

  self->priv->main_context = g_main_context_ref (main_context);

  fs_upnp_simple_igd_build (self, NULL);

  return self;
}

static gpointer
fs_upnp_simple_igd_loop_func (gpointer data)
{
  FsUpnpSimpleIgd *self = data;

  FS_UPNP_SIMPLE_IGD_LOCK (self);
  self->priv->loop = g_main_loop_new (self->priv->main_context, FALSE);
  FS_UPNP_SIMPLE_IGD_UNLOCK (self);

  g_main_loop_run (self->priv->loop);

  FS_UPNP_SIMPLE_IGD_LOCK (self);
  g_main_loop_unref (self->priv->loop);
  self->priv->loop = NULL;
  FS_UPNP_SIMPLE_IGD_UNLOCK (self);

  return NULL;
}

FsUpnpSimpleIgd *
fs_upnp_simple_igd_new_with_thread ()
{
  FsUpnpSimpleIgd *self = g_object_new (FS_TYPE_UPNP_SIMPLE_IGD, NULL);

  self->priv->main_context = g_main_context_new ();

  fs_upnp_simple_igd_build (self, NULL);

  self->priv->thread = g_thread_create (fs_upnp_simple_igd_loop_func,
      self, TRUE, NULL);

  if (!self->priv->thread)
  {
    g_object_unref (self);
    self = NULL;
  }

  return self;
}


void
fs_upnp_simple_igd_stop (FsUpnpSimpleIgd *self)
{
  GSource *source;
  GMainLoop *loop;

  if (!self->priv->thread)
    return;

  source = g_idle_source_new ();


  FS_UPNP_SIMPLE_IGD_LOCK (self);
  loop = g_main_loop_ref (self->priv->loop);
  FS_UPNP_SIMPLE_IGD_UNLOCK (self);

  g_source_set_callback (source,
      (GSourceFunc) g_main_loop_quit,
      loop,
      NULL);

  g_source_attach (source, self->priv->main_context);

  g_thread_join (self->priv->thread);

  g_main_loop_unref (loop);
}

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

#include <string.h>

#include <libgupnp/gupnp-control-point.h>


struct _FsUpnpSimpleIgdPrivate
{
  GMainContext *main_context;

  GUPnPContext *gupnp_context;
  GUPnPControlPoint *cp;

  GPtrArray *service_proxies;

  GPtrArray *mappings;

  gulong avail_handler;
  gulong unavail_handler;

  guint request_timeout;

  gboolean gathering;
};

struct Proxy {
  FsUpnpSimpleIgd *parent;
  GUPnPServiceProxy *proxy;
  GPtrArray *actions;

  gchar *external_ip;
};

struct Action {
  struct Proxy *parent;
  GUPnPServiceProxyAction *action;
  GSource *timeout_source;
};

struct Mapping {
  gchar *protocol;
  guint external_port;
  gchar *local_ip;
  guint16 local_port;
  guint32 lease_duration;
  gchar *description;
};


/* signals */
enum
{
  SIGNAL_NEW_EXTERNAL_IP,
  SIGNAL_ERROR,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_REQUEST_TIMEOUT
};


static guint signals[LAST_SIGNAL] = { 0 };


#define FS_UPNP_SIMPLE_IGD_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_UPNP_SIMPLE_IGD,             \
   FsUpnpSimpleIgdPrivate))


G_DEFINE_TYPE (FsUpnpSimpleIgd, fs_upnp_simple_igd, G_TYPE_OBJECT);


static void fs_upnp_simple_igd_dispose (GObject *object);
static void fs_upnp_simple_igd_finalize (GObject *object);
static void fs_upnp_simple_igd_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static void fs_upnp_simple_igd_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);

static void fs_upnp_simple_igd_gather_proxy (FsUpnpSimpleIgd *self,
    struct Proxy *prox);

static void cleanup_proxy (struct Proxy *prox);

static void
fs_upnp_simple_igd_class_init (FsUpnpSimpleIgdClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsUpnpSimpleIgdPrivate));

  gobject_class->dispose = fs_upnp_simple_igd_dispose;
  gobject_class->finalize = fs_upnp_simple_igd_finalize;
  gobject_class->set_property = fs_upnp_simple_igd_set_property;
  gobject_class->get_property = fs_upnp_simple_igd_get_property;

  g_object_class_install_property (gobject_class,
      PROP_REQUEST_TIMEOUT,
      g_param_spec_uint ("request-timeout",
          "The timeout after which a request is considered to have failed",
          "After this timeout, the request is considered to have failed and"
          "is dropped.",
          0, G_MAXUINT, 5,
          G_PARAM_READWRITE));

  /**
   * FsUpnpSimpleIgd::new-external-ip
   * @self: #FsUpnpSimpleIgd that emitted the signal
   * @ip: The string representing the new external IP
   *
   * This signal means that a new external IP has been found on an IGD.
   * It is only emitted if fs_upnp_simple_igd_gather() has been set to %TRUE.
   *
   */
  signals[SIGNAL_NEW_EXTERNAL_IP] = g_signal_new ("new-external-ip",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__STRING,
      G_TYPE_NONE, 1, G_TYPE_STRING);

  /**
   * FsUpnpSimpleIgd::error
   * @self: #FsUpnpSimpleIgd that emitted the signal
   * @error: a #GError
   *
   * This means that an asynchronous error has happened.
   *
   */
  signals[SIGNAL_ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__POINTER,
      G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void
fs_upnp_simple_igd_init (FsUpnpSimpleIgd *self)
{
  self->priv = FS_UPNP_SIMPLE_IGD_GET_PRIVATE (self);

  self->priv->request_timeout = 5;

  self->priv->service_proxies = g_ptr_array_new ();
}

static void
fs_upnp_simple_igd_dispose (GObject *object)
{
  FsUpnpSimpleIgd *self = FS_UPNP_SIMPLE_IGD_CAST (object);

  if (self->priv->avail_handler)
    g_signal_handler_disconnect (self->priv->cp, self->priv->avail_handler);
  self->priv->avail_handler = 0;

  if (self->priv->unavail_handler)
    g_signal_handler_disconnect (self->priv->cp, self->priv->unavail_handler);
  self->priv->unavail_handler = 0;

  while (self->priv->service_proxies->len)
  {
    cleanup_proxy (
        g_ptr_array_index (self->priv->service_proxies, 0));
    g_ptr_array_remove_index_fast (self->priv->service_proxies, 0);
  }

  if (self->priv->cp)
    g_object_unref (self->priv->cp);
  self->priv->cp = NULL;

  if (self->priv->gupnp_context)
    g_object_unref (self->priv->gupnp_context);
  self->priv->gupnp_context = NULL;

  G_OBJECT_CLASS (fs_upnp_simple_igd_parent_class)->dispose (object);
}

static void
cleanup_proxy (struct Proxy *prox)
{
  guint i;

  for (i=0; i < prox->actions->len; i++)
  {
    struct Action *action = g_ptr_array_index (prox->actions, i);

    if (action->timeout_source)
      g_source_destroy (action->timeout_source);

    gupnp_service_proxy_cancel_action (prox->proxy, action->action);
    g_slice_free (struct Action, action);
  }

  g_ptr_array_free (prox->actions, TRUE);

  g_object_unref (prox->proxy);
  g_slice_free (struct Proxy, prox);
}

static void
cleanup_mapping (struct Mapping *mapping)
{
  g_free (mapping->protocol);
  g_free (mapping->local_ip);
  g_free (mapping->description);
  g_slice_free (struct Mapping, mapping);
}

static void
fs_upnp_simple_igd_finalize (GObject *object)
{
  FsUpnpSimpleIgd *self = FS_UPNP_SIMPLE_IGD_CAST (object);

  g_main_context_unref (self->priv->main_context);

  g_warn_if_fail (self->priv->service_proxies->len == 0);
  g_ptr_array_free (self->priv->service_proxies, TRUE);


  g_ptr_array_foreach (self->priv->mappings, (GFunc) cleanup_mapping, NULL);
  g_ptr_array_free (self->priv->mappings, TRUE);

  G_OBJECT_CLASS (fs_upnp_simple_igd_parent_class)->finalize (object);
}

static void
fs_upnp_simple_igd_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  FsUpnpSimpleIgd *self = FS_UPNP_SIMPLE_IGD_CAST (object);

  switch (prop_id) {
    case PROP_REQUEST_TIMEOUT:
      g_value_set_uint (value, self->priv->request_timeout);
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
      self->priv->request_timeout = g_value_get_uint (value);
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
  struct Proxy *prox = g_slice_new0 (struct Proxy);

  prox->parent = self;
  prox->proxy = g_object_ref (proxy);
  prox->actions = g_ptr_array_new ();

  if (self->priv->gathering)
    fs_upnp_simple_igd_gather_proxy (self, prox);

  g_ptr_array_add(self->priv->service_proxies, prox);
}


static void
_cp_service_unavail (GUPnPControlPoint *cp,
    GUPnPServiceProxy *proxy,
    FsUpnpSimpleIgd *self)
{
  guint i;

  for (i=0; i < self->priv->service_proxies->len; i++)
  {
    struct Proxy *prox =
      g_ptr_array_index (self->priv->service_proxies, i);

    if (prox->proxy == proxy)
    {
      cleanup_proxy (prox);
      g_ptr_array_remove_index_fast (self->priv->service_proxies, i);
      break;
    }
  }
}


static gboolean
fs_upnp_simple_igd_build (FsUpnpSimpleIgd *self)
{
  self->priv->gupnp_context = gupnp_context_new (self->priv->main_context,
      NULL, 0, NULL);
  if (!self->priv->gupnp_context)
    return FALSE;

  self->priv->cp = gupnp_control_point_new (self->priv->gupnp_context,
      "urn:schemas-upnp-org:service:WANIPConnection:1");
  g_return_val_if_fail (self->priv->cp, FALSE);

  self->priv->avail_handler = g_signal_connect (self->priv->cp,
      "service-proxy-available",
      G_CALLBACK (_cp_service_avail), self);
  self->priv->unavail_handler = g_signal_connect (self->priv->cp,
      "service-proxy-unavailable",
      G_CALLBACK (_cp_service_unavail), self);

  gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (self->priv->cp),
      TRUE);

  return TRUE;
}

FsUpnpSimpleIgd *
fs_upnp_simple_igd_new (GMainContext *main_context)
{
  FsUpnpSimpleIgd *self = g_object_new (FS_TYPE_UPNP_SIMPLE_IGD, NULL);

  if (!main_context)
    main_context = g_main_context_default ();

  self->priv->main_context = g_main_context_ref (main_context);

  fs_upnp_simple_igd_build (self);

  return self;
}

void
fs_upnp_simple_igd_gather (FsUpnpSimpleIgd *self, gboolean gather)
{
  if (self->priv->gathering == gather)
    return;

  self->priv->gathering = gather;

  if (gather)
  {
    guint i;

    for (i = 0; i < self->priv->service_proxies->len; i++)
    {
      struct Proxy *prox =
          g_ptr_array_index(self->priv->service_proxies, i);
      fs_upnp_simple_igd_gather_proxy (self, prox);
    }
  }
}


static void
_service_proxy_got_external_ip_address (GUPnPServiceProxy *proxy,
    GUPnPServiceProxyAction *action,
    gpointer user_data)
{
  struct Proxy *prox = user_data;
  FsUpnpSimpleIgd *self = prox->parent;
  GError *error = NULL;
  gchar *ip = NULL;

  if (gupnp_service_proxy_end_action (proxy, action, &error,
          "NewExternalIPAddress", G_TYPE_STRING, &ip,
          NULL))
  {
    g_free (prox->external_ip);
    prox->external_ip = g_strdup (ip);

    g_signal_emit (self, signals[SIGNAL_NEW_EXTERNAL_IP], 0,
        ip);
  }
  else
  {
    g_return_if_fail (error);
    g_signal_emit (self, signals[SIGNAL_ERROR], error->domain,
        error);
  }
  g_clear_error (&error);
}

static gboolean
_service_proxy_action_timeout (gpointer user_data)
{
  struct Action *action = user_data;

  gupnp_service_proxy_cancel_action (action->parent->proxy, action->action);

  g_ptr_array_remove_fast (action->parent->actions, action);

  g_slice_free (struct Action, action);

  return FALSE;
}

static void
fs_upnp_simple_igd_gather_proxy (FsUpnpSimpleIgd *self,
    struct Proxy *prox)
{
  struct Action *action = g_slice_new0 (struct Action);

  action->parent = prox;
  action->action = gupnp_service_proxy_begin_action (prox->proxy,
      "GetExternalIPAddress",
      _service_proxy_got_external_ip_address, self,
      NULL);

  action->timeout_source =
    g_timeout_source_new_seconds (self->priv->request_timeout);
  g_source_set_callback (action->timeout_source,
      _service_proxy_action_timeout, action, NULL);
  g_source_attach (action->timeout_source, self->priv->main_context);

  g_ptr_array_add(prox->actions, action);
}


void
fs_upnp_simple_igd_add_port (FsUpnpSimpleIgd *self,
    const gchar *protocol,
    guint16 external_port,
    const gchar *local_ip,
    guint16 local_port,
    guint32 lease_duration,
    const gchar *description)
{
  struct Mapping *mapping = g_slice_new0 (struct Mapping);

  g_return_if_fail (protocol && local_ip);

  mapping->protocol = g_strdup (protocol);
  mapping->external_port = external_port;
  mapping->local_ip = g_strdup (local_ip);
  mapping->local_port = local_port;
  mapping->lease_duration = lease_duration;
  mapping->description = g_strdup (description);

  g_ptr_array_add (self->priv->mappings, mapping);
}


void
fs_upnp_simple_igd_remove_port (FsUpnpSimpleIgd *self,
    const gchar *protocol,
    guint external_port)
{
  guint i;
  struct Mapping *mapping;

  g_return_if_fail (protocol);

  for (i = 0; i < self->priv->mappings->len; i++)
  {
    struct Mapping *tmpmapping = g_ptr_array_index (self->priv->mappings, i);
    if (tmpmapping->external_port == external_port &&
        !strcmp (tmpmapping->protocol, protocol))
    {
      mapping = tmpmapping;
      break;
    }
  }
  g_return_if_fail (mapping);

  cleanup_mapping (mapping);
}

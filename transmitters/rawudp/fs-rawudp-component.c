/*
 * Farsight2 - Farsight RAW UDP with STUN Component Transmitter
 *
 * Copyright 2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2008 Nokia Corp.
 *
 * fs-rawudp-transmitter.c - A Farsight UDP transmitter with STUN
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rawudp-component.h"

#include <gst/farsight/fs-conference-iface.h>


#define GST_CAT_DEFAULT fs_rawudp_transmitter_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_COMPONENT,
  PROP_STUN_IP,
  PROP_STUN_PORT,
  PROP_STUN_TIMEOUT,
  PROP_SENDING,
  PROP_UDPPORT,
  PROP_TRANSMITTER
};


struct _FsRawUdpComponentPrivate
{
  gboolean disposed;

  guint component;

  UdpPort *udpport;
  FsRawUdpTransmitter *transmitter;

  gchar *stun_ip;
  guint stun_port;
  guint stun_timeout;

  GMutex *mutex;

  /* Above this line, its all set at construction time */
  /* This is protected by the mutex */

  gboolean sending;
};


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

static GType type = 0;

#define FS_RAWUDP_COMPONENT_LOCK(component) \
  g_mutex_lock ((component)->priv->mutex)
#define FS_RAWUDP_COMPONENT_UNLOCK(component) \
  g_mutex_unlock ((component)->priv->mutex)

static void
fs_rawudp_component_class_init (FsRawUdpComponentClass *klass);
static void
fs_rawudp_component_init (FsRawUdpComponent *self);
static void
fs_rawudp_component_dispose (GObject *object);
static void
fs_rawudp_component_finalize (GObject *object);
static void
fs_rawudp_component_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void
fs_rawudp_component_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);


GType
fs_rawudp_component_get_type (void)
{
  return type;
}

GType
fs_rawudp_component_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsRawUdpComponentClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_rawudp_component_class_init,
    NULL,
    NULL,
    sizeof (FsRawUdpComponent),
    0,
    (GInstanceInitFunc) fs_rawudp_component_init
  };

  type = g_type_module_register_type (G_TYPE_MODULE (module),
      G_TYPE_OBJECT, "FsRawUdpComponent", &info, 0);

  return type;
}



static void
fs_rawudp_component_class_init (FsRawUdpComponentClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_rawudp_component_set_property;
  gobject_class->get_property = fs_rawudp_component_get_property;


 g_object_class_install_property (gobject_class,
      PROP_COMPONENT,
      g_param_spec_uint ("component",
          "The component id",
          "The id of this component",
          1, G_MAXUINT, 1,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));


  g_object_class_install_property (gobject_class,
      PROP_SENDING,
      g_param_spec_boolean ("sending",
          "Whether to send from this transmitter",
          "If set to FALSE, the transmitter will stop sending to this person",
          TRUE,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_STUN_IP,
      g_param_spec_string ("stun-ip",
          "The IP address of the STUN server",
          "The IPv4 address of the STUN server as a x.x.x.x string",
          NULL,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
      PROP_STUN_PORT,
      g_param_spec_uint ("stun-port",
          "The port of the STUN server",
          "The IPv4 UDP port of the STUN server as a ",
          1, 65535, 3478,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
      PROP_STUN_TIMEOUT,
      g_param_spec_uint ("stun-timeout",
          "The timeout for the STUN reply",
          "How long to wait for for the STUN reply (in seconds) before giving up",
          1, G_MAXUINT, 30,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));


  g_object_class_install_property (gobject_class,
      PROP_TRANSMITTER,
      g_param_spec_object ("transmitter",
          "The transmitter object",
          "The rawudp transmitter object",
          FS_TYPE_RAWUDP_TRANSMITTER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));


  g_object_class_install_property (gobject_class,
      PROP_UDPPORT,
      g_param_spec_pointer ("udpport",
          "The UdpPort for this component",
          "a gpointer to the udpport",
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

  gobject_class->dispose = fs_rawudp_component_dispose;
  gobject_class->finalize = fs_rawudp_component_finalize;

  g_type_class_add_private (klass, sizeof (FsRawUdpComponentPrivate));
}




static void
fs_rawudp_component_init (FsRawUdpComponent *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      FS_TYPE_RAWUDP_COMPONENT,
      FsRawUdpComponentPrivate);

  self->priv->sending = TRUE;

  self->priv->disposed = FALSE;

  self->priv->mutex = g_mutex_new ();
}



static void
fs_rawudp_component_dispose (GObject *object)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (object);

  if (self->priv->disposed)
    /* If dispose did already run, return. */
    return;

  FS_RAWUDP_COMPONENT_LOCK (self);

  self->priv->udpport = NULL;

  g_object_unref (self->priv->transmitter);
  self->priv->transmitter = NULL;

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  FS_RAWUDP_COMPONENT_UNLOCK (self);

  parent_class->dispose (object);
}


static void
fs_rawudp_component_finalize (GObject *object)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (object);

  g_free (self->priv->stun_ip);

  g_mutex_free (self->priv->mutex);

  parent_class->finalize (object);
}


static void
fs_rawudp_component_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (object);

  switch (prop_id)
  {
    case PROP_SENDING:
      FS_RAWUDP_COMPONENT_LOCK (self);
      g_value_set_boolean (value, self->priv->sending);
      FS_RAWUDP_COMPONENT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rawudp_component_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (object);

  switch (prop_id)
  {
    case PROP_COMPONENT:
      self->priv->component = g_value_get_uint (value);
      break;
    case PROP_SENDING:
      FS_RAWUDP_COMPONENT_LOCK (self);
      self->priv->sending = g_value_get_boolean (value);
      FS_RAWUDP_COMPONENT_UNLOCK (self);
      break;
    case PROP_STUN_IP:
      g_free (self->priv->stun_ip);
      self->priv->stun_ip = g_value_dup_string (value);
      break;
    case PROP_STUN_PORT:
      self->priv->stun_port = g_value_get_uint (value);
      break;
    case PROP_STUN_TIMEOUT:
      self->priv->stun_timeout = g_value_get_uint (value);
      break;
    case PROP_UDPPORT:
      self->priv->udpport = g_value_get_pointer (value);
      break;
    case PROP_TRANSMITTER:
      self->priv->transmitter = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


FsRawUdpComponent *
fs_rawudp_component_new (
    guint component,
    FsRawUdpTransmitter *trans,
    const gchar *stun_ip,
    guint stun_port,
    guint stun_timeout,
    UdpPort *udpport,
    GError **error)
{
  FsRawUdpComponent *self = NULL;

  self = g_object_new (FS_TYPE_RAWUDP_COMPONENT,
      "component", component,
      "transmitter", trans,
      "stun-ip", stun_ip,
      "stun-port", stun_port,
      "stun-timeout", stun_timeout,
      "udpport", udpport,
      NULL);

  if (!self)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not build RawUdp component %u", component);
    return NULL;
  }

  return self;
}

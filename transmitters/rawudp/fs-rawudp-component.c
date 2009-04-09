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

#include "fs-rawudp-marshal.h"

#include <stun/usages/bind.h>
#include <stun/stunagent.h>
#include <stun/usages/timer.h>
#include <nice/address.h>

#include <gst/farsight/fs-conference-iface.h>
#include <gst/farsight/fs-interfaces.h>

#include <gst/netbuffer/gstnetbuffer.h>

#ifdef HAVE_GUPNP
#include <libgupnp-igd/gupnp-simple-igd-thread.h>
#endif

#include <string.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef G_OS_WIN32
# include <winsock2.h>
#else /*G_OS_WIN32*/
# include <netdb.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
#endif /*G_OS_WIN32*/

#define GST_CAT_DEFAULT fs_rawudp_transmitter_debug

#define DEFAULT_UPNP_MAPPING_TIMEOUT (600)
#define DEFAULT_UPNP_DISCOVERY_TIMEOUT (10)

/* Signals */
enum
{
  NEW_LOCAL_CANDIDATE,
  LOCAL_CANDIDATES_PREPARED,
  NEW_ACTIVE_CANDIDATE_PAIR,
  KNOWN_SOURCE_PACKET_RECEIVED,
  ERROR_SIGNAL,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_COMPONENT,
  PROP_IP,
  PROP_PORT,
  PROP_STUN_IP,
  PROP_STUN_PORT,
  PROP_STUN_TIMEOUT,
  PROP_SENDING,
  PROP_TRANSMITTER,
  PROP_FORCED_CANDIDATE,
  PROP_ASSOCIATE_ON_SOURCE,
#ifdef HAVE_GUPNP
  PROP_UPNP_MAPPING,
  PROP_UPNP_DISCOVERY,
  PROP_UPNP_MAPPING_TIMEOUT,
  PROP_UPNP_DISCOVERY_TIMEOUT,
  PROP_UPNP_IGD
#endif
};


struct _FsRawUdpComponentPrivate
{
  gboolean disposed;

  guint component;

  GError *construction_error;

  FsRawUdpTransmitter *transmitter;

  gchar *ip;
  guint port;

  gchar *stun_ip;
  guint stun_port;
  guint stun_timeout;

  GMutex *mutex;

  StunAgent stun_agent;
  StunMessage stun_message;
  guchar stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
  struct sockaddr_storage stun_sockaddr;
  gboolean stun_server_changed;

  gboolean associate_on_source;

#ifdef HAVE_GUPNP
  gboolean upnp_discovery;
  gboolean upnp_mapping;
  guint upnp_mapping_timeout;
  guint upnp_discovery_timeout;

  GUPnPSimpleIgdThread *upnp_igd;
#endif

  /* Above this line, its all set at construction time */
  /* Below, they are protected by the mutex */

  UdpPort *udpport;

  FsCandidate *remote_candidate;
  GstNetAddress remote_address;

  FsCandidate *local_active_candidate;
  FsCandidate *local_forced_candidate;

  gboolean gathered;

  gulong stun_recv_id;

  gulong buffer_recv_id;

  GstClockID stun_timeout_id;
  GThread *stun_timeout_thread;
  gboolean stun_stop;

  gboolean sending;

  gboolean remote_is_unique;

#ifdef HAVE_GUPNP
  GSource *upnp_discovery_timeout_src;
  FsCandidate *local_upnp_candidate;

  gulong upnp_signal_id;
#endif
};


static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

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
fs_rawudp_constructed (GObject *object);
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


static gboolean
fs_rawudp_component_emit_local_candidates (FsRawUdpComponent *self,
    GError **eror);
static void
fs_rawudp_component_emit_error (FsRawUdpComponent *self,
    gint error_no,
    gchar *error_msg,
    gchar *debug_msg);
static void
fs_rawudp_component_maybe_new_active_candidate_pair (FsRawUdpComponent *self);
static void
fs_rawudp_component_emit_candidate (FsRawUdpComponent *self,
    FsCandidate *candidate);

static gboolean
stun_recv_cb (GstPad *pad, GstBuffer *buffer,
    gpointer user_data);
static gpointer
stun_timeout_func (gpointer user_data);
static gboolean
buffer_recv_cb (GstPad *pad, GstBuffer *buffer, gpointer user_data);

static void
remote_is_unique_cb (gboolean unique, const GstNetAddress *address,
    gpointer user_data);

static gboolean
fs_rawudp_component_start_stun (FsRawUdpComponent *self, GError **error);
static void
fs_rawudp_component_stop_stun_locked (FsRawUdpComponent *self);

#ifdef HAVE_GUPNP
static void
fs_rawudp_component_stop_upnp_discovery_locked (FsRawUdpComponent *self);
#endif

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

  /* Required because the GST type registration is not thread safe */

  g_type_class_ref (GST_TYPE_NETBUFFER);

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
  gobject_class->constructed = fs_rawudp_constructed;
  gobject_class->dispose = fs_rawudp_component_dispose;
  gobject_class->finalize = fs_rawudp_component_finalize;

  g_object_class_install_property (gobject_class,
      PROP_COMPONENT,
      g_param_spec_uint ("component",
          "The component id",
          "The id of this component",
          1, G_MAXUINT, 1,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));


  g_object_class_install_property (gobject_class,
      PROP_SENDING,
      g_param_spec_boolean ("sending",
          "Whether to send from this transmitter",
          "If set to FALSE, the transmitter will stop sending to this person",
          TRUE,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_IP,
      g_param_spec_string ("ip",
          "The local IP of this component",
          "The IPv4 address as a x.x.x.x string",
          NULL,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
      PROP_PORT,
      g_param_spec_uint ("port",
          "The local port requested for this component",
          "The IPv4 UDP port",
          1, 65535, 7078,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));


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
          1,  MAX_STUN_TIMEOUT, DEFAULT_STUN_TIMEOUT,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));


  g_object_class_install_property (gobject_class,
      PROP_TRANSMITTER,
      g_param_spec_object ("transmitter",
          "The transmitter object",
          "The rawudp transmitter object",
          FS_TYPE_RAWUDP_TRANSMITTER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));


  g_object_class_install_property (gobject_class,
      PROP_FORCED_CANDIDATE,
      g_param_spec_boxed ("forced-candidate",
          "A Forced candidate",
          "This candidate is built from a user preference",
          FS_TYPE_CANDIDATE,
          G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
      PROP_ASSOCIATE_ON_SOURCE,
      g_param_spec_boolean ("associate-on-source",
          "Associate incoming data based on the source address",
          "Whether to associate incoming data stream based on the"
          " source address",
          TRUE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

#ifdef HAVE_GUPNP
    g_object_class_install_property (gobject_class,
      PROP_UPNP_MAPPING,
      g_param_spec_boolean ("upnp-mapping",
          "Try to map ports using UPnP",
          "Tries to map ports using UPnP if enabled",
          TRUE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_UPNP_DISCOVERY,
      g_param_spec_boolean ("upnp-discovery",
          "Try to use UPnP to find the external IP address",
          "Tries to discovery the external IP with UPnP if stun fails",
          TRUE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_UPNP_MAPPING_TIMEOUT,
      g_param_spec_uint ("upnp-mapping-timeout",
          "Timeout after which UPnP mappings expire",
          "The UPnP port mappings expire after this period if the app has"
          " crashed (in seconds)",
          0, G_MAXUINT32, DEFAULT_UPNP_MAPPING_TIMEOUT,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_UPNP_DISCOVERY_TIMEOUT,
      g_param_spec_uint ("upnp-discovery-timeout",
          "Timeout after which UPnP discovery fails",
          "After this period, UPnP discovery is considered to have failed"
          " and the local IP is returned",
          0, G_MAXUINT32, DEFAULT_UPNP_DISCOVERY_TIMEOUT,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_UPNP_IGD,
      g_param_spec_object ("upnp-igd",
          "The GUPnPSimpleIgdThread object",
          "This is the GUPnP IGD abstraction object",
          GUPNP_TYPE_SIMPLE_IGD_THREAD,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
#endif

   /**
   * FsRawUdpComponent::new-local-candidate:
   * @self: #FsStream that emitted the signal
   * @local_candidate: #FsCandidate of the local candidate
   *
   * This signal is emitted when a new local candidate is discovered.
   */
  signals[NEW_LOCAL_CANDIDATE] = g_signal_new
    ("new-local-candidate",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__BOXED,
      G_TYPE_NONE, 1, FS_TYPE_CANDIDATE);

 /**
   * FsRawUdpComponent::local-candidates-prepared:
   * @self: #FsStream that emitted the signal
   *
   * This signal is emitted when all local candidates have been
   * prepared for this component.
   */
  signals[LOCAL_CANDIDATES_PREPARED] = g_signal_new
    ("local-candidates-prepared",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  /**
   * FsiRawUdpComponent::new-active-candidate-pair:
   * @self: #FsStream that emitted the signal
   * @local_candidate: #FsCandidate of the local candidate being used
   * @remote_candidate: #FsCandidate of the remote candidate being used
   *
   * This signal is emitted when there is a new active chandidate pair that has
   * been established.
   *
   */
  signals[NEW_ACTIVE_CANDIDATE_PAIR] = g_signal_new
    ("new-active-candidate-pair",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL,
        NULL,
        _fs_rawudp_marshal_VOID__BOXED_BOXED,
        G_TYPE_NONE, 2, FS_TYPE_CANDIDATE, FS_TYPE_CANDIDATE);

 /**
   * FsRawUdpComponent::known-source-packet-received:
   * @self: #FsRawUdpComponent that emitted the signal
   * @component: The ID of this component
   * @buffer: the #GstBuffer coming from the known source
   *
   * This signal is emitted when a buffer coming from a confirmed known source
   * is received.
   *
   */
  signals[KNOWN_SOURCE_PACKET_RECEIVED] = g_signal_new
    ("known-source-packet-received",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_rawudp_marshal_VOID__UINT_POINTER,
      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

  /**
   * FsRawUdpComponent::error:
   * @self: #FsStreamTransmitter that emitted the signal
   * @errorno: The number of the error
   * @error_msg: Error message to be displayed to user
   * @debug_msg: Debugging error message
   *
   * This signal is emitted in any error condition
   *
   */
  signals[ERROR_SIGNAL] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_rawudp_marshal_VOID__ENUM_STRING_STRING,
      G_TYPE_NONE, 3, FS_TYPE_ERROR, G_TYPE_STRING, G_TYPE_STRING);


  g_type_class_add_private (klass, sizeof (FsRawUdpComponentPrivate));
}




static void
fs_rawudp_component_init (FsRawUdpComponent *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      FS_TYPE_RAWUDP_COMPONENT,
      FsRawUdpComponentPrivate);

  self->priv->disposed = FALSE;

  self->priv->sending = TRUE;
  self->priv->port = 7078;

  self->priv->associate_on_source = TRUE;

  stun_agent_init (&self->priv->stun_agent,
      STUN_ALL_KNOWN_ATTRIBUTES, STUN_COMPATIBILITY_RFC3489, 0);

#ifdef HAVE_GUPNP
  self->priv->upnp_mapping = TRUE;
  self->priv->upnp_discovery = TRUE;
  self->priv->upnp_discovery_timeout = DEFAULT_UPNP_DISCOVERY_TIMEOUT;
  self->priv->upnp_mapping_timeout = DEFAULT_UPNP_MAPPING_TIMEOUT;
#endif

  self->priv->mutex = g_mutex_new ();
}

static void
fs_rawudp_constructed (GObject *object)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (object);

  if (!self->priv->transmitter)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_INVALID_ARGUMENTS,
        "You need a transmitter to build this object");
    return;
  }

  self->priv->udpport =
    fs_rawudp_transmitter_get_udpport (self->priv->transmitter,
        self->priv->component,
        self->priv->ip,
        self->priv->port,
        &self->priv->construction_error);
  if (!self->priv->udpport)
  {
    if (!self->priv->construction_error)
      self->priv->construction_error = g_error_new (FS_ERROR, FS_ERROR_INTERNAL,
          "Unkown error when trying to open udp port");
    return;
  }

  if (self->priv->associate_on_source)
    self->priv->buffer_recv_id =
      fs_rawudp_transmitter_udpport_connect_recv (
          self->priv->udpport,
          G_CALLBACK (buffer_recv_cb), self);

  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
}

static void
fs_rawudp_component_dispose (GObject *object)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (object);
  FsRawUdpTransmitter *ts = NULL;

  if (self->priv->disposed)
    /* If dispose did already run, return. */
    return;

  if (self->priv->udpport)
  {
    GST_ERROR ("You must call fs_stream_transmitter_stop() before dropping"
        " the last reference to a stream transmitter");
    fs_rawudp_component_stop (self);
  }

#ifdef HAVE_GUPNP
  if (self->priv->upnp_igd)
  {
    g_object_unref (self->priv->upnp_igd);
    self->priv->upnp_igd = NULL;
  }
#endif

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  FS_RAWUDP_COMPONENT_LOCK (self);
  ts = self->priv->transmitter;
  self->priv->transmitter = NULL;
  FS_RAWUDP_COMPONENT_UNLOCK (self);

  g_object_unref (ts);

  parent_class->dispose (object);
}

void
fs_rawudp_component_stop (FsRawUdpComponent *self)
{
  UdpPort *udpport = NULL;

  FS_RAWUDP_COMPONENT_LOCK (self);
  if (self->priv->stun_timeout_thread != NULL)
  {
    fs_rawudp_component_stop_stun_locked (self);
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    g_thread_join (self->priv->stun_timeout_thread);
    FS_RAWUDP_COMPONENT_LOCK (self);

    self->priv->stun_timeout_thread = NULL;
  }

  udpport = self->priv->udpport;
  self->priv->udpport = NULL;

  if (udpport)
  {
#ifdef HAVE_GUPNP

    fs_rawudp_component_stop_upnp_discovery_locked (self);

    if (self->priv->upnp_igd  &&
        (self->priv->upnp_mapping || self->priv->upnp_discovery))
    {
      gupnp_simple_igd_remove_port (GUPNP_SIMPLE_IGD (self->priv->upnp_igd),
          "UDP", fs_rawudp_transmitter_udpport_get_port (udpport));
    }
#endif

    if (self->priv->buffer_recv_id)
    {
      fs_rawudp_transmitter_udpport_disconnect_recv (
          udpport,
          self->priv->buffer_recv_id);
      self->priv->buffer_recv_id = 0;
    }

    if (self->priv->remote_candidate)
    {
      if (self->priv->sending)
        fs_rawudp_transmitter_udpport_remove_dest (udpport,
            self->priv->remote_candidate->ip,
            self->priv->remote_candidate->port);
      else
        fs_rawudp_transmitter_udpport_remove_recvonly_dest (udpport,
            self->priv->remote_candidate->ip,
            self->priv->remote_candidate->port);


      fs_rawudp_transmitter_udpport_remove_known_address (udpport,
          &self->priv->remote_address, remote_is_unique_cb, self);
    }

    FS_RAWUDP_COMPONENT_UNLOCK (self);

    fs_rawudp_transmitter_put_udpport (self->priv->transmitter, udpport);
  }
  else
    FS_RAWUDP_COMPONENT_UNLOCK (self);
}

static void
fs_rawudp_component_finalize (GObject *object)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (object);

  if (self->priv->remote_candidate)
    fs_candidate_destroy (self->priv->remote_candidate);
  if (self->priv->local_active_candidate)
    fs_candidate_destroy (self->priv->local_active_candidate);
  if (self->priv->local_forced_candidate)
    fs_candidate_destroy (self->priv->local_forced_candidate);
#ifdef HAVE_GUPNP
  if (self->priv->local_upnp_candidate)
    fs_candidate_destroy (self->priv->local_upnp_candidate);
#endif

  g_free (self->priv->ip);
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
    case PROP_FORCED_CANDIDATE:
      FS_RAWUDP_COMPONENT_LOCK (self);
      g_value_set_boxed (value, self->priv->local_forced_candidate);
      FS_RAWUDP_COMPONENT_UNLOCK (self);
      break;
    case PROP_COMPONENT:
      g_value_set_uint (value, self->priv->component);
      break;
#ifdef HAVE_GUPNP
    case PROP_UPNP_MAPPING:
      g_value_set_boolean (value, self->priv->upnp_mapping);
      break;
    case PROP_UPNP_DISCOVERY:
      g_value_set_boolean (value, self->priv->upnp_discovery);
      break;
    case PROP_UPNP_MAPPING_TIMEOUT:
      g_value_set_uint (value, self->priv->upnp_mapping_timeout);
      break;
    case PROP_UPNP_DISCOVERY_TIMEOUT:
      g_value_set_uint (value, self->priv->upnp_discovery_timeout);
      break;
#endif
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
      {
        gboolean sending, old_sending;
        FsCandidate *candidate = NULL;

        g_return_if_fail (self->priv->udpport);


        FS_RAWUDP_COMPONENT_LOCK (self);
        old_sending = self->priv->sending;
        sending = self->priv->sending = g_value_get_boolean (value);
        if (self->priv->remote_candidate)
          candidate = fs_candidate_copy (self->priv->remote_candidate);
        FS_RAWUDP_COMPONENT_UNLOCK (self);

        if (sending != old_sending && candidate)
        {
          if (sending)
          {
            fs_rawudp_transmitter_udpport_remove_recvonly_dest (
                self->priv->udpport, candidate->ip, candidate->port);
            fs_rawudp_transmitter_udpport_add_dest (self->priv->udpport,
                candidate->ip, candidate->port);
          }
          else
          {
            fs_rawudp_transmitter_udpport_remove_dest (self->priv->udpport,
                candidate->ip, candidate->port);
            fs_rawudp_transmitter_udpport_add_recvonly_dest (
                self->priv->udpport, candidate->ip, candidate->port);
          }
        }
        if (candidate)
          fs_candidate_destroy (candidate);
      }
      break;
    case PROP_IP:
      g_free (self->priv->ip);
      self->priv->ip = g_value_dup_string (value);
      break;
    case PROP_PORT:
      self->priv->port = g_value_get_uint (value);
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
    case PROP_TRANSMITTER:
      self->priv->transmitter = g_value_dup_object (value);
      break;
    case PROP_FORCED_CANDIDATE:
      FS_RAWUDP_COMPONENT_LOCK (self);
      if (self->priv->local_forced_candidate)
        GST_WARNING ("Tried to reset a forced candidate");
      else
        self->priv->local_forced_candidate = g_value_dup_boxed (value);
      FS_RAWUDP_COMPONENT_UNLOCK (self);
      break;
    case PROP_ASSOCIATE_ON_SOURCE:
      self->priv->associate_on_source = g_value_get_boolean (value);
      break;
#ifdef HAVE_GUPNP
    case PROP_UPNP_MAPPING:
      self->priv->upnp_mapping = g_value_get_boolean (value);
      break;
    case PROP_UPNP_DISCOVERY:
      self->priv->upnp_discovery = g_value_get_boolean (value);
      break;
    case PROP_UPNP_MAPPING_TIMEOUT:
      self->priv->upnp_mapping_timeout = g_value_get_uint (value);
      break;
    case PROP_UPNP_DISCOVERY_TIMEOUT:
      self->priv->upnp_discovery_timeout = g_value_get_uint (value);
      break;
    case PROP_UPNP_IGD:
      self->priv->upnp_igd = g_value_dup_object (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


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
    GError **error)
{
  FsRawUdpComponent *self = NULL;

  self = g_object_new (FS_TYPE_RAWUDP_COMPONENT,
      "component", component,
      "transmitter", trans,
      "associate-on-source", associate_on_source,
      "ip", ip,
      "port", port,
      "stun-ip", stun_ip,
      "stun-port", stun_port,
      "stun-timeout", stun_timeout,
#ifdef HAVE_GUPNP
      "upnp-mapping", upnp_mapping,
      "upnp-discovery", upnp_discovery,
      "upnp-mapping-timeout", upnp_mapping_timeout,
      "upnp-discovery-timeout", upnp_discovery_timeout,
      "upnp-igd", upnp_igd,
#endif
      NULL);

  if (!self)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not build RawUdp component %u", component);
    return NULL;
  }

  if (self->priv->construction_error)
  {
    g_propagate_error (error, self->priv->construction_error);
    g_object_unref (self);
    return NULL;
  }

  if (used_port)
    *used_port = fs_rawudp_transmitter_udpport_get_port (self->priv->udpport);

  return self;
}

static void
remote_is_unique_cb (gboolean unique, const GstNetAddress *address,
    gpointer user_data)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (user_data);

  FS_RAWUDP_COMPONENT_LOCK (self);

  if (!gst_netaddress_equal (address, &self->priv->remote_address))
  {
    GST_ERROR ("Got callback for an address that is not ours");
    goto out;
  }

  self->priv->remote_is_unique = unique;

 out:
  FS_RAWUDP_COMPONENT_UNLOCK (self);
}


gboolean
fs_rawudp_component_set_remote_candidate (FsRawUdpComponent *self,
    FsCandidate *candidate,
    GError **error)
{
  FsCandidate *old_candidate = NULL;
  gboolean sending;
  struct addrinfo hints = {0};
  struct addrinfo *res = NULL;
  int rv;

  if (candidate->component_id != self->priv->component)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Remote candidate routed to wrong component (%d->%d)",
        candidate->component_id,
        self->priv->component);
    return FALSE;
  }

  hints.ai_flags = AI_NUMERICHOST;
  rv = getaddrinfo (candidate->ip, NULL, &hints, &res);
  if (rv != 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid address passed: %s", gai_strerror (rv));
    return FALSE;
  }

  FS_RAWUDP_COMPONENT_LOCK (self);

  if (!self->priv->udpport)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Can't call set_remote_candidate after the thread has been stopped");
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    return FALSE;
  }

  if (self->priv->remote_candidate)
    fs_rawudp_transmitter_udpport_remove_known_address (self->priv->udpport,
        &self->priv->remote_address, remote_is_unique_cb, self);

  old_candidate = self->priv->remote_candidate;
  self->priv->remote_candidate = fs_candidate_copy (candidate);
  sending = self->priv->sending;

  switch (res->ai_family)
  {
    case AF_INET:
      gst_netaddress_set_ip4_address (&self->priv->remote_address,
          ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr,
          g_htons(candidate->port));
      break;
    case AF_INET6:
      gst_netaddress_set_ip6_address (&self->priv->remote_address,
          ((struct sockaddr_in6 *)res->ai_addr)->sin6_addr.s6_addr,
          g_htons(candidate->port));
      break;
  }


  self->priv->remote_is_unique =
    fs_rawudp_transmitter_udpport_add_known_address (self->priv->udpport,
        &self->priv->remote_address, remote_is_unique_cb, self);

  FS_RAWUDP_COMPONENT_UNLOCK (self);

  freeaddrinfo (res);

  if (sending)
    fs_rawudp_transmitter_udpport_add_dest (self->priv->udpport,
        candidate->ip, candidate->port);
  else
    fs_rawudp_transmitter_udpport_add_recvonly_dest (self->priv->udpport,
        candidate->ip, candidate->port);

  if (old_candidate)
  {
    if (sending)
      fs_rawudp_transmitter_udpport_remove_dest (self->priv->udpport,
          old_candidate->ip,
          old_candidate->port);
    else
      fs_rawudp_transmitter_udpport_remove_recvonly_dest (self->priv->udpport,
          candidate->ip,
          candidate->port);
    fs_candidate_destroy (old_candidate);
  }

  fs_rawudp_component_maybe_new_active_candidate_pair (self);

  return TRUE;
}

static void
fs_rawudp_component_maybe_emit_local_candidates (FsRawUdpComponent *self)
{
  GError *error = NULL;

  FS_RAWUDP_COMPONENT_LOCK (self);
  if (self->priv->local_active_candidate)
  {
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    return;
  }

  if (self->priv->stun_timeout_thread &&
      self->priv->stun_timeout_thread != g_thread_self ())
  {
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    return;
  }

#ifdef HAVE_GUPNP
  if (self->priv->local_upnp_candidate)
  {
    self->priv->local_active_candidate = self->priv->local_upnp_candidate;
    self->priv->local_upnp_candidate = NULL;
    GST_DEBUG ("C:%d Emitting UPnP discovered candidate: %s:%u",
        self->priv->component,
        self->priv->local_active_candidate->ip,
        self->priv->local_active_candidate->port);
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    fs_rawudp_component_emit_candidate (self,
        self->priv->local_active_candidate);
    return;
  }
#endif

  FS_RAWUDP_COMPONENT_UNLOCK (self);

  if (!fs_rawudp_component_emit_local_candidates (self, &error))
  {
    if (error->domain == FS_ERROR)
      fs_rawudp_component_emit_error (self, error->code,
          error->message, error->message);
    else
      fs_rawudp_component_emit_error (self, FS_ERROR_INTERNAL,
          "Error emitting local candidates", NULL);
  }
  g_clear_error (&error);

}

#ifdef HAVE_GUPNP
static void
_upnp_mapped_external_port (GUPnPSimpleIgdThread *igd, gchar *proto,
    gchar *external_ip, gchar *replaces_external_ip, guint external_port,
    gchar *local_ip, guint local_port, gchar *description, gpointer user_data)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (user_data);

  FS_RAWUDP_COMPONENT_LOCK (self);
  /* Skip it if its not our port */
  if (fs_rawudp_transmitter_udpport_get_port (self->priv->udpport) !=
      external_port)
  {
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    return;
  }

  fs_rawudp_component_stop_upnp_discovery_locked (self);

  if (self->priv->local_upnp_candidate || self->priv->local_active_candidate)
  {
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    return;
  }

  self->priv->local_upnp_candidate = fs_candidate_new ("L1",
      self->priv->component,
      FS_CANDIDATE_TYPE_HOST,
      FS_NETWORK_PROTOCOL_UDP,
      external_ip,
      external_port);

  FS_RAWUDP_COMPONENT_UNLOCK (self);

  fs_rawudp_component_maybe_emit_local_candidates (self);
}

static gboolean
_upnp_discovery_timeout (gpointer user_data)
{
  FsRawUdpComponent *self = user_data;

  FS_RAWUDP_COMPONENT_LOCK (self);
  g_source_unref (self->priv->upnp_discovery_timeout_src);
  self->priv->upnp_discovery_timeout_src = NULL;
  FS_RAWUDP_COMPONENT_UNLOCK (self);

  fs_rawudp_component_maybe_emit_local_candidates (self);

  return FALSE;
}

static void
fs_rawudp_component_stop_upnp_discovery_locked (FsRawUdpComponent *self)
{
  if (self->priv->upnp_discovery_timeout_src)
  {
    g_source_destroy (self->priv->upnp_discovery_timeout_src);
    g_source_unref (self->priv->upnp_discovery_timeout_src);
  }
  self->priv->upnp_discovery_timeout_src = NULL;

  if (self->priv->upnp_signal_id)
  {
    g_signal_handler_disconnect (self->priv->upnp_igd,
        self->priv->upnp_signal_id);
    self->priv->upnp_signal_id = 0;
  }
}

#endif

gboolean
fs_rawudp_component_gather_local_candidates (FsRawUdpComponent *self,
    GError **error)
{
  if (self->priv->gathered)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Call gather local candidates twice on the same component");
    return FALSE;
  }

  if (!self->priv->udpport)
  {   g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "You can not call gather_local_candidate() after the stream has"
        " been stopped");
    return FALSE;
  }

#ifdef HAVE_GUPNP

  if (self->priv->upnp_igd  &&
      (self->priv->upnp_mapping || self->priv->upnp_discovery))
  {
    guint port;
    GList *ips;

    port = fs_rawudp_transmitter_udpport_get_port (self->priv->udpport);

    ips = fs_interfaces_get_local_ips (FALSE);

    if (ips)
    {
      gchar *ip = g_list_first (ips)->data;
      GMainContext *ctx;

      if (self->priv->upnp_discovery)
      {
        FS_RAWUDP_COMPONENT_LOCK (self);
        self->priv->upnp_signal_id = g_signal_connect (self->priv->upnp_igd,
            "mapped-external-port",
            G_CALLBACK (_upnp_mapped_external_port), self);
        FS_RAWUDP_COMPONENT_UNLOCK (self);
      }

      gupnp_simple_igd_add_port (GUPNP_SIMPLE_IGD (self->priv->upnp_igd),
          "UDP", port, ip, port, self->priv->upnp_mapping_timeout,
          "Farsight Raw UDP transmitter");


      if (self->priv->upnp_discovery)
      {
        FS_RAWUDP_COMPONENT_LOCK (self);
        self->priv->upnp_discovery_timeout_src = g_timeout_source_new_seconds (
            self->priv->upnp_discovery_timeout);
        g_source_set_callback (self->priv->upnp_discovery_timeout_src,
            _upnp_discovery_timeout, self, NULL);
        g_object_get (self->priv->upnp_igd, "main-context", &ctx, NULL);
        g_source_attach (self->priv->upnp_discovery_timeout_src, ctx);
        FS_RAWUDP_COMPONENT_UNLOCK (self);
      }
    }

    /* free list of ips */
    g_list_foreach (ips, (GFunc) g_free, NULL);
    g_list_free (ips);

  }
#endif

  if (self->priv->stun_ip && self->priv->stun_port)
    return fs_rawudp_component_start_stun (self, error);
#ifdef HAVE_GUPNP
  else if (!self->priv->upnp_igd || !self->priv->upnp_discovery)
    return fs_rawudp_component_emit_local_candidates (self, error);
  else
    return TRUE;
#else
  else
    return fs_rawudp_component_emit_local_candidates (self, error);
#endif
}

static gboolean
fs_rawudp_component_send_stun_locked (FsRawUdpComponent *self, GError **error)
{
  return fs_rawudp_transmitter_udpport_sendto (self->priv->udpport,
      (gchar*) self->priv->stun_buffer,
      stun_message_length (&self->priv->stun_message),
      (const struct sockaddr *)&self->priv->stun_sockaddr,
      sizeof (self->priv->stun_sockaddr), error);
}

static gboolean
fs_rawudp_component_start_stun (FsRawUdpComponent *self, GError **error)
{
  NiceAddress niceaddr;
  gboolean res = TRUE;

  GST_DEBUG ("C:%d starting the STUN process with server %s:%u",
      self->priv->component, self->priv->stun_ip, self->priv->stun_port);

  FS_RAWUDP_COMPONENT_LOCK (self);
  self->priv->stun_recv_id =
    fs_rawudp_transmitter_udpport_connect_recv (
        self->priv->udpport,
        G_CALLBACK (stun_recv_cb), self);


  nice_address_init (&niceaddr);
  if (!nice_address_set_from_string (&niceaddr, self->priv->stun_ip))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid IP address %s passed for STUN", self->priv->stun_ip);
    return FALSE;
  }
  nice_address_set_port (&niceaddr, self->priv->stun_port);
  nice_address_copy_to_sockaddr (&niceaddr,
      (struct sockaddr *) &self->priv->stun_sockaddr);

  stun_usage_bind_create (
      &self->priv->stun_agent,
      &self->priv->stun_message,
      self->priv->stun_buffer,
      sizeof(self->priv->stun_buffer));

  if (self->priv->stun_timeout_thread == NULL) {
    /* only create a new thread if the old one was stopped. Otherwise we can
     * just reuse the currently running one. */
    self->priv->stun_timeout_thread =
      g_thread_create (stun_timeout_func, self, TRUE, error);
  }

  res = (self->priv->stun_timeout_thread != NULL);

  g_assert (error == NULL || res || *error);

  FS_RAWUDP_COMPONENT_UNLOCK (self);

  return res;
}

/*
 * This function MUST always be called wiuth the Component lock held
 */

static void
fs_rawudp_component_stop_stun_locked (FsRawUdpComponent *self)
{
  if (self->priv->stun_recv_id)
  {
    fs_rawudp_transmitter_udpport_disconnect_recv (
        self->priv->udpport,
        self->priv->stun_recv_id);
    self->priv->stun_recv_id = 0;
  }

  self->priv->stun_stop = TRUE;
  if (self->priv->stun_timeout_id)
    gst_clock_id_unschedule (self->priv->stun_timeout_id);
}



static gboolean
stun_recv_cb (GstPad *pad, GstBuffer *buffer,
    gpointer user_data)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (user_data);
  FsCandidate *candidate = NULL;
  StunMessage msg;
  StunValidationStatus stunv;
  StunUsageBindReturn stunr;
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  struct sockaddr_storage alt_addr;
  socklen_t alt_addr_len = sizeof(alt_addr);
  gchar addr_str[NI_MAXHOST];
  NiceAddress niceaddr;

  if (GST_BUFFER_SIZE (buffer) < 4)
    /* Packet is too small to be STUN */
    return TRUE;

  if (GST_BUFFER_DATA (buffer)[0] >> 6)
    /* Non stun packet */
    return TRUE;


  g_assert (fs_rawudp_transmitter_udpport_is_pad (self->priv->udpport, pad));

  FS_RAWUDP_COMPONENT_LOCK(self);
  stunv = stun_agent_validate (&self->priv->stun_agent, &msg,
      GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), NULL, NULL);
  FS_RAWUDP_COMPONENT_UNLOCK(self);

  /* not a valid stun message */
  if (stunv != STUN_VALIDATION_SUCCESS)
    return TRUE;

  stunr = stun_usage_bind_process (&msg,
      (struct sockaddr *) &addr, &addr_len,
      (struct sockaddr *) &alt_addr, &alt_addr_len);

  switch (stunr)
  {
    case STUN_USAGE_BIND_RETURN_INVALID:
      /* Not a valid bind reponse */
      return TRUE;
    case STUN_USAGE_BIND_RETURN_ERROR:
      /* Not a valid bind reponse */
      return FALSE;
    case STUN_USAGE_BIND_RETURN_ALTERNATE_SERVER:
      /* Change servers and reset timeouts */
      FS_RAWUDP_COMPONENT_LOCK(self);
      memcpy (&self->priv->stun_sockaddr, &alt_addr,
          MIN (sizeof(self->priv->stun_sockaddr), alt_addr_len));
      self->priv->stun_server_changed = TRUE;
      stun_usage_bind_create (
          &self->priv->stun_agent,
          &self->priv->stun_message,
          self->priv->stun_buffer,
          sizeof(self->priv->stun_buffer));
      nice_address_init (&niceaddr);
      nice_address_set_from_sockaddr (&niceaddr,
          (const struct sockaddr *) &alt_addr);
      nice_address_to_string (&niceaddr, addr_str);
      GST_DEBUG ("Stun server redirected us to alternate server %s:%d",
          addr_str, nice_address_get_port (&niceaddr));
      if (self->priv->stun_timeout_id)
        gst_clock_id_unschedule (self->priv->stun_timeout_id);
      FS_RAWUDP_COMPONENT_UNLOCK(self);
      return FALSE;
    default:
      /* For any other case, pass the packet through */
      return TRUE;
    case STUN_USAGE_BIND_RETURN_SUCCESS:
      break;
  }

  nice_address_init (&niceaddr);
  nice_address_set_from_sockaddr (&niceaddr, (const struct sockaddr *) &addr);
  nice_address_to_string (&niceaddr, addr_str);

  candidate = fs_candidate_new ("L1",
      self->priv->component,
      FS_CANDIDATE_TYPE_SRFLX,
      FS_NETWORK_PROTOCOL_UDP,
      addr_str,
      nice_address_get_port (&niceaddr));

  GST_DEBUG ("Stun server says we are %s:%u\n", addr_str,
      nice_address_get_port (&niceaddr));

  FS_RAWUDP_COMPONENT_LOCK(self);
  fs_rawudp_component_stop_stun_locked (self);
#ifdef HAVE_GUPNP
  fs_rawudp_component_stop_upnp_discovery_locked (self);
#endif

  self->priv->local_active_candidate = fs_candidate_copy (candidate);

  FS_RAWUDP_COMPONENT_UNLOCK(self);

  GST_DEBUG ("C:%d Emitting STUN discovered candidate: %s:%u",
      self->priv->component,
      candidate->ip, candidate->port);
  fs_rawudp_component_emit_candidate (self, candidate);

  fs_candidate_destroy (candidate);

  return FALSE;
}

static gpointer
stun_timeout_func (gpointer user_data)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (user_data);
  GstClock *sysclock = NULL;
  GstClockID id;
  gboolean emit = TRUE;
  GstClockTime next_stun_timeout;
  GError *error = NULL;
  guint timeout_accum_ms = 0;
  guint remainder;
  StunUsageTimerReturn timer_ret = STUN_USAGE_TIMER_RETURN_RETRANSMIT;
  StunTransactionId stunid;
  StunTimer stun_timer;

  sysclock = gst_system_clock_obtain ();
  if (sysclock == NULL)
  {
    fs_rawudp_component_emit_error (self, FS_ERROR_INTERNAL,
        "Could not obtain gst system clock", NULL);
    FS_RAWUDP_COMPONENT_LOCK(self);
    goto interrupt;
  }

  FS_RAWUDP_COMPONENT_LOCK(self);
  stun_timer_start (&stun_timer);

  while (!self->priv->stun_stop &&
      timeout_accum_ms < self->priv->stun_timeout * 1000)
  {
    if (self->priv->stun_server_changed)
    {
      stun_timer_start (&stun_timer);
      self->priv->stun_server_changed = FALSE;
      timer_ret = STUN_USAGE_TIMER_RETURN_RETRANSMIT;
    }

    if (timer_ret == STUN_USAGE_TIMER_RETURN_RETRANSMIT &&
        !fs_rawudp_component_send_stun_locked (self, &error))
    {
      FS_RAWUDP_COMPONENT_UNLOCK(self);
      fs_rawudp_component_emit_error (self, error->code, "Could not send stun",
          error->message);
      g_clear_error (&error);
      FS_RAWUDP_COMPONENT_LOCK (self);
      fs_rawudp_component_stop_stun_locked (self);
      goto interrupt;
    }

    if (self->priv->stun_stop)
      goto interrupt;

    remainder = stun_timer_remainder (&stun_timer);

    next_stun_timeout = gst_clock_get_time (sysclock) +
      remainder * GST_MSECOND;

    id = self->priv->stun_timeout_id = gst_clock_new_single_shot_id (sysclock,
        next_stun_timeout);

    GST_LOG ("C:%u Waiting for STUN reply for %u ms, next: %u ms",
        self->priv->component, remainder, timeout_accum_ms);

    FS_RAWUDP_COMPONENT_UNLOCK(self);
    gst_clock_id_wait (id, NULL);
    FS_RAWUDP_COMPONENT_LOCK(self);

    gst_clock_id_unref (id);
    self->priv->stun_timeout_id = NULL;

    timer_ret = stun_timer_refresh (&stun_timer);
    timeout_accum_ms += remainder;

    if (timer_ret == STUN_USAGE_TIMER_RETURN_TIMEOUT)
      break;
  }

 interrupt:
  if (self->priv->stun_stop)
  {
    GST_DEBUG ("C:%u STUN process interrupted", self->priv->component);
    emit = FALSE;
  }

  fs_rawudp_component_stop_stun_locked (self);

  stun_message_id (&self->priv->stun_message, stunid);
  stun_agent_forget_transaction (&self->priv->stun_agent, stunid);

  FS_RAWUDP_COMPONENT_UNLOCK(self);

  gst_object_unref (sysclock);

  if (emit)
    fs_rawudp_component_maybe_emit_local_candidates (self);

  return NULL;
}


static void
fs_rawudp_component_emit_error (FsRawUdpComponent *self,
    gint error_no,
    gchar *error_msg,
    gchar *debug_msg)
{
  g_signal_emit (self, signals[ERROR_SIGNAL], 0, error_no, error_msg,
      debug_msg);
}


static void
fs_rawudp_component_maybe_new_active_candidate_pair (FsRawUdpComponent *self)
{

  FS_RAWUDP_COMPONENT_LOCK (self);

  if (self->priv->local_active_candidate && self->priv->remote_candidate)
  {
    FsCandidate *remote = fs_candidate_copy (self->priv->remote_candidate);

    FS_RAWUDP_COMPONENT_UNLOCK (self);

    g_signal_emit (self, signals[NEW_ACTIVE_CANDIDATE_PAIR], 0,
        self->priv->local_active_candidate, remote);

    fs_candidate_destroy (remote);
  }
  else
  {
    FS_RAWUDP_COMPONENT_UNLOCK (self);
  }
}



static gboolean
fs_rawudp_component_emit_local_candidates (FsRawUdpComponent *self,
    GError **error)
{
  GList *ips = NULL;
  GList *current;
  guint port;

  FS_RAWUDP_COMPONENT_LOCK (self);
  if (self->priv->local_forced_candidate)
  {
    self->priv->local_active_candidate = fs_candidate_copy (
        self->priv->local_forced_candidate);
    FS_RAWUDP_COMPONENT_UNLOCK (self);

    GST_DEBUG ("C:%d Emitting forced candidate: %s:%u",
        self->priv->component,
        self->priv->local_active_candidate->ip,
        self->priv->local_active_candidate->port);
    fs_rawudp_component_emit_candidate (self,
        self->priv->local_active_candidate);
    return TRUE;
  }

  port = fs_rawudp_transmitter_udpport_get_port (self->priv->udpport);

  ips = fs_interfaces_get_local_ips (TRUE);

  for (current = g_list_first (ips);
       current;
       current = g_list_next (current))
  {
    self->priv->local_active_candidate = fs_candidate_new ("L1",
        self->priv->component,
        FS_CANDIDATE_TYPE_HOST,
        FS_NETWORK_PROTOCOL_UDP,
        current->data,
        port);    /* FIXME: Emit only the first candidate ?? */
    break;
  }

  /* free list of ips */
  g_list_foreach (ips, (GFunc) g_free, NULL);
  g_list_free (ips);

  if (self->priv->local_active_candidate)
  {
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    GST_DEBUG ("C:%d Emitting local interface candidate: %s:%u",
        self->priv->component,
        self->priv->local_active_candidate->ip,
        self->priv->local_active_candidate->port);
    fs_rawudp_component_emit_candidate (self,
        self->priv->local_active_candidate);
  }
  else
  {
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "We have no local candidate for component %d",
        self->priv->component);
    return FALSE;
  }

  return TRUE;
}

static void
fs_rawudp_component_emit_candidate (FsRawUdpComponent *self,
    FsCandidate *candidate)
{
    g_signal_emit (self, signals[NEW_LOCAL_CANDIDATE], 0,
        candidate);
    g_signal_emit (self, signals[LOCAL_CANDIDATES_PREPARED], 0);

    fs_rawudp_component_maybe_new_active_candidate_pair (self);
}

/*
 * This is a has "have-data" signal handler, so we return %TRUE to not
 * drop the buffer
 */
static gboolean
buffer_recv_cb (GstPad *pad, GstBuffer *buffer, gpointer user_data)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (user_data);

  if (GST_IS_NETBUFFER (buffer))
  {
    GstNetBuffer *netbuffer = (GstNetBuffer*) buffer;

    FS_RAWUDP_COMPONENT_LOCK (self);
    if (self->priv->remote_is_unique &&
        gst_netaddress_equal (&self->priv->remote_address, &netbuffer->from))
    {
      FS_RAWUDP_COMPONENT_UNLOCK (self);
      g_signal_emit (self, signals[KNOWN_SOURCE_PACKET_RECEIVED], 0,
          self->priv->component, buffer);
    }
    else
    {
      FS_RAWUDP_COMPONENT_UNLOCK (self);
    }
  }
  else
  {
    GST_WARNING ("received buffer thats not a NetBuffer");
  }

  return TRUE;
}

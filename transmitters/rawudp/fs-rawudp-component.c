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

#include "stun.h"

#include <gst/farsight/fs-conference-iface.h>
#include <gst/farsight/fs-interfaces.h>

#include <gst/netbuffer/gstnetbuffer.h>

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
  PROP_FORCED_CANDIDATE
};


struct _FsRawUdpComponentPrivate
{
  gboolean disposed;

  guint component;

  GError *construction_error;

  UdpPort *udpport;
  FsRawUdpTransmitter *transmitter;

  gchar *ip;
  guint port;

  gchar *stun_ip;
  guint stun_port;
  guint stun_timeout;

  GMutex *mutex;

  gchar stun_cookie[16];

  /* Above this line, its all set at construction time */
  /* This is protected by the mutex */

  FsCandidate *remote_candidate;
  GstNetAddress remote_address;

  FsCandidate *local_active_candidate;
  FsCandidate *local_forced_candidate;
  FsCandidate *local_stun_candidate;

  gboolean gathered;

  gulong stun_recv_id;

  gulong buffer_recv_id;

  GstClockID stun_timeout_id;
  GstClockTime next_stun_timeout;
  GThread *stun_timeout_thread;

  gboolean sending;
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
      PROP_FORCED_CANDIDATE,
      g_param_spec_boxed ("forced-candidate",
          "A Forced candidate",
          "This candidate is built from a user preference",
          FS_TYPE_CANDIDATE,
          G_PARAM_WRITABLE));

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

  ((guint32*)self->priv->stun_cookie)[0] = g_random_int ();
  ((guint32*)self->priv->stun_cookie)[1] = g_random_int ();
  ((guint32*)self->priv->stun_cookie)[2] = g_random_int ();
  ((guint32*)self->priv->stun_cookie)[3] = g_random_int ();

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

  FS_RAWUDP_COMPONENT_LOCK (self);

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  if (self->priv->stun_timeout_thread != NULL)
  {
    FS_RAWUDP_COMPONENT_UNLOCK (self);
    g_thread_join (self->priv->stun_timeout_thread);
    FS_RAWUDP_COMPONENT_LOCK (self);

    self->priv->stun_timeout_thread = NULL;
  }

  FS_RAWUDP_COMPONENT_UNLOCK (self);


  if (self->priv->buffer_recv_id)
  {
    fs_rawudp_transmitter_udpport_disconnect_recv (
        self->priv->udpport,
        self->priv->buffer_recv_id);
    self->priv->buffer_recv_id = 0;
  }

  if (self->priv->remote_candidate &&
      self->priv->udpport &&
      self->priv->sending)
      fs_rawudp_transmitter_udpport_remove_dest (self->priv->udpport,
          self->priv->remote_candidate->ip,
          self->priv->remote_candidate->port);

  if (self->priv->udpport)
    fs_rawudp_transmitter_put_udpport (self->priv->transmitter,
        self->priv->udpport);

  FS_RAWUDP_COMPONENT_LOCK (self);
  self->priv->udpport = NULL;
  ts = self->priv->transmitter;
  self->priv->transmitter = NULL;
  FS_RAWUDP_COMPONENT_UNLOCK (self);

  g_object_unref (ts);

  parent_class->dispose (object);
}


static void
fs_rawudp_component_finalize (GObject *object)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (object);

  if (self->priv->remote_candidate)
    fs_candidate_destroy (self->priv->remote_candidate);
  if (self->priv->local_active_candidate)
    fs_candidate_destroy (self->priv->local_active_candidate);
  if (self->priv->local_stun_candidate)
    fs_candidate_destroy (self->priv->local_stun_candidate);
  if (self->priv->local_forced_candidate)
    fs_candidate_destroy (self->priv->local_forced_candidate);

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
        FS_RAWUDP_COMPONENT_LOCK (self);
        old_sending = self->priv->sending;
        sending = self->priv->sending = g_value_get_boolean (value);
        if (self->priv->remote_candidate)
          candidate = fs_candidate_copy (self->priv->remote_candidate);
        FS_RAWUDP_COMPONENT_UNLOCK (self);

        if (sending != old_sending && candidate)
        {
          if (sending)
            fs_rawudp_transmitter_udpport_add_dest (self->priv->udpport,
                candidate->ip, candidate->port);
          else
            fs_rawudp_transmitter_udpport_remove_dest (self->priv->udpport,
                candidate->ip, candidate->port);
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


FsRawUdpComponent *
fs_rawudp_component_new (
    guint component,
    FsRawUdpTransmitter *trans,
    const gchar *ip,
    guint port,
    const gchar *stun_ip,
    guint stun_port,
    guint stun_timeout,
    guint *used_port,
    GError **error)
{
  FsRawUdpComponent *self = NULL;

  self = g_object_new (FS_TYPE_RAWUDP_COMPONENT,
      "component", component,
      "transmitter", trans,
      "ip", ip,
      "port", port,
      "stun-ip", stun_ip,
      "stun-port", stun_port,
      "stun-timeout", stun_timeout,
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
  FS_RAWUDP_COMPONENT_UNLOCK (self);

  freeaddrinfo (res);

  if (sending)
    fs_rawudp_transmitter_udpport_add_dest (self->priv->udpport,
        candidate->ip, candidate->port);

  if (old_candidate)
  {
    fs_rawudp_transmitter_udpport_remove_dest (self->priv->udpport,
        old_candidate->ip,
        old_candidate->port);
    fs_candidate_destroy (old_candidate);
  }

  fs_rawudp_component_maybe_new_active_candidate_pair (self);

  return TRUE;
}

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

  if (self->priv->stun_ip && self->priv->stun_port)
    return fs_rawudp_component_start_stun (self, error);
  else
    return fs_rawudp_component_emit_local_candidates (self, error);
}

gboolean
fs_rawudp_component_start_stun (FsRawUdpComponent *self, GError **error)
{
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  struct sockaddr_in address;
  gchar *packed;
  guint length;
  int retval;
  StunMessage *msg;
  gboolean res = TRUE;
  GstClock *sysclock = NULL;

  sysclock = gst_system_clock_obtain ();
  if (sysclock == NULL)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Could not obtain gst system clock");
    return FALSE;
  }

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_flags = AI_NUMERICHOST;
  retval = getaddrinfo (self->priv->stun_ip, NULL, &hints, &result);
  if (retval != 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid IP address %s passed for STUN: %s",
        self->priv->stun_ip, gai_strerror (retval));
    return FALSE;
  }
  memcpy (&address, result->ai_addr, sizeof (struct sockaddr_in));
  freeaddrinfo (result);

  address.sin_family = AF_INET;
  address.sin_port = htons (self->priv->stun_port);

  FS_RAWUDP_COMPONENT_LOCK (self);
  self->priv->stun_recv_id =
    fs_rawudp_transmitter_udpport_connect_recv (
        self->priv->udpport,
        G_CALLBACK (stun_recv_cb), self);
  FS_RAWUDP_COMPONENT_UNLOCK (self);

  msg = stun_message_new (STUN_MESSAGE_BINDING_REQUEST,
      self->priv->stun_cookie, 0);
  if (!msg)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Could not create a new STUN binding request");
    return FALSE;
  }

  length = stun_message_pack (msg, &packed);

  if (!fs_rawudp_transmitter_udpport_sendto (self->priv->udpport,
          packed, length, (const struct sockaddr *)&address, sizeof (address),
          error))
  {
    g_free (packed);
    stun_message_free (msg);
    return FALSE;
  }
  g_free (packed);
  stun_message_free (msg);

  FS_RAWUDP_COMPONENT_LOCK (self);

  self->priv->next_stun_timeout = gst_clock_get_time (sysclock) +
    (self->priv->stun_timeout * GST_SECOND);

  gst_object_unref (sysclock);

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

  self->priv->next_stun_timeout = 0;
  if (self->priv->stun_timeout_id)
    gst_clock_id_unschedule (self->priv->stun_timeout_id);
}



static gboolean
stun_recv_cb (GstPad *pad, GstBuffer *buffer,
    gpointer user_data)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (user_data);
  FsCandidate *candidate = NULL;
  StunMessage *msg;
  StunAttribute **attr;

  if (GST_BUFFER_SIZE (buffer) < 4)
    /* Packet is too small to be STUN */
    return TRUE;

  if (GST_BUFFER_DATA (buffer)[0] >> 6)
    /* Non stun packet */
    return TRUE;


  g_assert (fs_rawudp_transmitter_udpport_is_pad (self->priv->udpport, pad));

  msg = stun_message_unpack (GST_BUFFER_SIZE (buffer),
      (const gchar *) GST_BUFFER_DATA (buffer));
  if (!msg)
    /* invalid message */
    return TRUE;

  if (memcmp (msg->transaction_id, self->priv->stun_cookie, 16) != 0)
  {
    /* not ours */
    stun_message_free (msg);
    return TRUE;
  }

  if (msg->type == STUN_MESSAGE_BINDING_ERROR_RESPONSE)
  {
    fs_rawudp_component_emit_error (FS_RAWUDP_COMPONENT (self),
        FS_ERROR_NETWORK, "Got an error message from the STUN server",
        "The STUN process produced an error");
    stun_message_free (msg);
    // fs_rawudp_component_stop_stun (self, component_id);
    /* Lets not stop the STUN now and wait for the timeout
     * in case the server answers with the right reply
     */
    return FALSE;
  }

  if (msg->type != STUN_MESSAGE_BINDING_RESPONSE)
  {
    stun_message_free (msg);
    return TRUE;
  }


  for (attr = msg->attributes; *attr; attr++)
  {
    if ((*attr)->type == STUN_ATTRIBUTE_MAPPED_ADDRESS)
    {
      // TODO
      gchar *id = g_strdup_printf ("L1");
      gchar *ip = g_strdup_printf ("%u.%u.%u.%u",
          ((*attr)->address.ip & 0xff000000) >> 24,
          ((*attr)->address.ip & 0x00ff0000) >> 16,
          ((*attr)->address.ip & 0x0000ff00) >>  8,
          ((*attr)->address.ip & 0x000000ff));

      candidate = fs_candidate_new (id,
          self->priv->component,
          FS_CANDIDATE_TYPE_SRFLX,
          FS_NETWORK_PROTOCOL_UDP,
          ip,
          (*attr)->address.port);
      g_free (id);
      g_free (ip);

      GST_DEBUG ("Stun server says we are %u.%u.%u.%u %u\n",
          ((*attr)->address.ip & 0xff000000) >> 24,
          ((*attr)->address.ip & 0x00ff0000) >> 16,
          ((*attr)->address.ip & 0x0000ff00) >>  8,
          ((*attr)->address.ip & 0x000000ff),(*attr)->address.port);
      break;
    }
  }

  FS_RAWUDP_COMPONENT_LOCK(self);
  fs_rawudp_component_stop_stun_locked (self);

  self->priv->local_active_candidate = fs_candidate_copy (candidate);
  FS_RAWUDP_COMPONENT_UNLOCK(self);

  fs_rawudp_component_emit_candidate (self, candidate);

  fs_candidate_destroy (candidate);

  /* It was a stun packet, lets drop it */
  stun_message_free (msg);
  return FALSE;
}

static gpointer
stun_timeout_func (gpointer user_data)
{
  FsRawUdpComponent *self = FS_RAWUDP_COMPONENT (user_data);
  GstClock *sysclock = NULL;
  GstClockID id;
  gboolean emit = TRUE;

  sysclock = gst_system_clock_obtain ();
  if (sysclock == NULL)
  {
    fs_rawudp_component_emit_error (self, FS_ERROR_INTERNAL,
        "Could not obtain gst system clock", NULL);
    emit = FALSE;
    FS_RAWUDP_COMPONENT_LOCK(self);
    goto error;
  }

  FS_RAWUDP_COMPONENT_LOCK(self);
  id = self->priv->stun_timeout_id = gst_clock_new_single_shot_id (sysclock,
      self->priv->next_stun_timeout);

  FS_RAWUDP_COMPONENT_UNLOCK(self);
  gst_clock_id_wait (id, NULL);
  FS_RAWUDP_COMPONENT_LOCK(self);

  gst_clock_id_unref (id);
  self->priv->stun_timeout_id = NULL;

  if (self->priv->next_stun_timeout == 0)
    emit = FALSE;

 error:
  fs_rawudp_component_stop_stun_locked (self);

  FS_RAWUDP_COMPONENT_UNLOCK(self);

  gst_object_unref (sysclock);

  if (emit)
  {
    GError *error = NULL;
    if (!fs_rawudp_component_emit_local_candidates (self, &error))
    {
      if (error->domain == FS_ERROR)
        fs_rawudp_component_emit_error (self, error->code,
            error->message, error->message);
      else
        fs_rawudp_component_emit_error (self, FS_ERROR_INTERNAL,
            "Error emitting local errors", NULL);
    }
    g_clear_error (&error);
  }

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

    if (gst_netaddress_equal (&self->priv->remote_address,
            &netbuffer->from))
      g_signal_emit (self, signals[KNOWN_SOURCE_PACKET_RECEIVED], 0,
          self->priv->component, buffer);
  }
  else
  {
    GST_WARNING ("received buffer thats not a NetBuffer");
  }

  return TRUE;
}

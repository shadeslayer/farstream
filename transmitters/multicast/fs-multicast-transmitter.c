/*
 * Farstream - Farstream Multicast UDP Transmitter
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-multicast-transmitter.c - A Farstream multicast UDP transmitter
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

/**
 * SECTION:fs-multicast-transmitter
 * @short_description: A transmitter for multicast UDP
 *
 * This transmitter provides multicast udp
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-multicast-transmitter.h"
#include "fs-multicast-stream-transmitter.h"

#include <farstream/fs-conference.h>
#include <farstream/fs-plugin.h>

#include <string.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef G_OS_WIN32
# include <ws2tcpip.h>
# define close closesocket
#else /*G_OS_WIN32*/
# include <netdb.h>
# include <sys/socket.h>
# include <netinet/ip.h>
# include <arpa/inet.h>
#endif /*G_OS_WIN32*/

GST_DEBUG_CATEGORY (fs_multicast_transmitter_debug);
#define GST_CAT_DEFAULT fs_multicast_transmitter_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_GST_SINK,
  PROP_GST_SRC,
  PROP_COMPONENTS,
  PROP_TYPE_OF_SERVICE,
  PROP_DO_TIMESTAMP
};

struct _FsMulticastTransmitterPrivate
{
  /* We hold references to this element */
  GstElement *gst_sink;
  GstElement *gst_src;

  /* We don't hold a reference to these elements, they are owned
     by the bins */
  /* They are tables of pointers, one per component */
  GstElement **udpsrc_funnels;
  GstElement **udpsink_tees;

  GMutex *mutex;
  GList **udpsocks;

  gint type_of_service;
  gboolean do_timestamp;

  gboolean disposed;
};

#define FS_MULTICAST_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_MULTICAST_TRANSMITTER, \
    FsMulticastTransmitterPrivate))

static void fs_multicast_transmitter_class_init (
    FsMulticastTransmitterClass *klass);
static void fs_multicast_transmitter_init (FsMulticastTransmitter *self);
static void fs_multicast_transmitter_constructed (GObject *object);
static void fs_multicast_transmitter_dispose (GObject *object);
static void fs_multicast_transmitter_finalize (GObject *object);

static void fs_multicast_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_multicast_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static FsStreamTransmitter *fs_multicast_transmitter_new_stream_transmitter (
    FsTransmitter *transmitter, FsParticipant *participant,
    guint n_parameters, GParameter *parameters, GError **error);
static GType fs_multicast_transmitter_get_stream_transmitter_type (
    FsTransmitter *transmitter);

static void fs_multicast_transmitter_set_type_of_service (
    FsMulticastTransmitter *self,
    gint tos);


static GObjectClass *parent_class = NULL;
//static guint signals[LAST_SIGNAL] = { 0 };


/*
 * Lets register the plugin
 */

static GType type = 0;

GType
fs_multicast_transmitter_get_type (void)
{
  g_assert (type);
  return type;
}

static GType
fs_multicast_transmitter_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsMulticastTransmitterClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_multicast_transmitter_class_init,
    NULL,
    NULL,
    sizeof (FsMulticastTransmitter),
    0,
    (GInstanceInitFunc) fs_multicast_transmitter_init
  };

  GST_DEBUG_CATEGORY_INIT (fs_multicast_transmitter_debug,
      "fsmulticasttransmitter", 0,
      "Farstream multicast UDP transmitter");

  fs_multicast_stream_transmitter_register_type (module);

  type = g_type_module_register_type (G_TYPE_MODULE (module),
    FS_TYPE_TRANSMITTER, "FsMulticastTransmitter", &info, 0);

  return type;
}

FS_INIT_PLUGIN (fs_multicast_transmitter_register_type)

static void
fs_multicast_transmitter_class_init (FsMulticastTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsTransmitterClass *transmitter_class = FS_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_multicast_transmitter_set_property;
  gobject_class->get_property = fs_multicast_transmitter_get_property;

  gobject_class->constructed = fs_multicast_transmitter_constructed;

  g_object_class_override_property (gobject_class, PROP_GST_SRC, "gst-src");
  g_object_class_override_property (gobject_class, PROP_GST_SINK, "gst-sink");
  g_object_class_override_property (gobject_class, PROP_COMPONENTS,
    "components");
  g_object_class_override_property (gobject_class, PROP_TYPE_OF_SERVICE,
    "tos");
  g_object_class_override_property (gobject_class, PROP_DO_TIMESTAMP,
    "do-timestamp");

  transmitter_class->new_stream_transmitter =
    fs_multicast_transmitter_new_stream_transmitter;
  transmitter_class->get_stream_transmitter_type =
    fs_multicast_transmitter_get_stream_transmitter_type;

  gobject_class->dispose = fs_multicast_transmitter_dispose;
  gobject_class->finalize = fs_multicast_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsMulticastTransmitterPrivate));
}

static void
fs_multicast_transmitter_init (FsMulticastTransmitter *self)
{

  /* member init */
  self->priv = FS_MULTICAST_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->components = 2;
  self->priv->mutex = g_mutex_new ();
  self->priv->do_timestamp = TRUE;
}

static void
fs_multicast_transmitter_constructed (GObject *object)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER_CAST (object);
  FsTransmitter *trans = FS_TRANSMITTER_CAST (self);
  GstPad *pad = NULL, *pad2 = NULL;
  GstPad *ghostpad = NULL;
  gchar *padname;
  GstPadLinkReturn ret;
  int c; /* component_id */


  /* We waste one space in order to have the index be the component_id */
  self->priv->udpsrc_funnels = g_new0 (GstElement *, self->components+1);
  self->priv->udpsink_tees = g_new0 (GstElement *, self->components+1);
  self->priv->udpsocks = g_new0 (GList *, self->components+1);

  /* First we need the src elemnet */

  self->priv->gst_src = gst_bin_new (NULL);

  if (!self->priv->gst_src) {
    trans->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not build the transmitter src bin");
    return;
  }

  gst_object_ref (self->priv->gst_src);


  /* Second, we do the sink element */

  self->priv->gst_sink = gst_bin_new (NULL);

  if (!self->priv->gst_sink) {
    trans->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not build the transmitter sink bin");
    return;
  }

  g_object_set (G_OBJECT (self->priv->gst_sink),
      "async-handling", TRUE,
      NULL);

  gst_object_ref (self->priv->gst_sink);

  for (c = 1; c <= self->components; c++) {
    GstElement *fakesink = NULL;

    /* Lets create the RTP source funnel */

    self->priv->udpsrc_funnels[c] = gst_element_factory_make ("fsfunnel", NULL);

    if (!self->priv->udpsrc_funnels[c]) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make the fsfunnel element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_src),
        self->priv->udpsrc_funnels[c])) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not add the fsfunnel element to the transmitter src bin");
    }

    pad = gst_element_get_static_pad (self->priv->udpsrc_funnels[c], "src");
    padname = g_strdup_printf ("src%d", c);
    ghostpad = gst_ghost_pad_new (padname, pad);
    g_free (padname);
    gst_object_unref (pad);

    gst_pad_set_active (ghostpad, TRUE);
    gst_element_add_pad (self->priv->gst_src, ghostpad);


    /* Lets create the RTP sink tee */

    self->priv->udpsink_tees[c] = gst_element_factory_make ("tee", NULL);

    if (!self->priv->udpsink_tees[c]) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make the tee element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_sink),
        self->priv->udpsink_tees[c])) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not add the tee element to the transmitter sink bin");
    }

    pad = gst_element_get_static_pad (self->priv->udpsink_tees[c], "sink");
    padname = g_strdup_printf ("sink%d", c);
    ghostpad = gst_ghost_pad_new (padname, pad);
    g_free (padname);
    gst_object_unref (pad);

    gst_pad_set_active (ghostpad, TRUE);
    gst_element_add_pad (self->priv->gst_sink, ghostpad);

    fakesink = gst_element_factory_make ("fakesink", NULL);

    if (!fakesink) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make the fakesink element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_sink), fakesink))
    {
      gst_object_unref (fakesink);
      trans->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the fakesink element to the transmitter sink bin");
      return;
    }

    g_object_set (fakesink,
        "async", FALSE,
        "sync" , FALSE,
        NULL);

    pad = gst_element_get_request_pad (self->priv->udpsink_tees[c], "src%d");
    pad2 = gst_element_get_static_pad (fakesink, "sink");

    ret = gst_pad_link (pad, pad2);

    gst_object_unref (pad2);
    gst_object_unref (pad);

    if (GST_PAD_LINK_FAILED(ret)) {
      trans->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not link the tee to the fakesink");
      return;
    }
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
}

static void
fs_multicast_transmitter_dispose (GObject *object)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  if (self->priv->gst_src) {
    gst_object_unref (self->priv->gst_src);
    self->priv->gst_src = NULL;
  }

  if (self->priv->gst_sink) {
    gst_object_unref (self->priv->gst_sink);
    self->priv->gst_sink = NULL;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_multicast_transmitter_finalize (GObject *object)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (object);

  if (self->priv->udpsrc_funnels) {
    g_free (self->priv->udpsrc_funnels);
    self->priv->udpsrc_funnels = NULL;
  }

  if (self->priv->udpsink_tees) {
    g_free (self->priv->udpsink_tees);
    self->priv->udpsink_tees = NULL;
  }

  if (self->priv->udpsocks) {
    g_free (self->priv->udpsocks);
    self->priv->udpsocks = NULL;
  }

  g_mutex_free (self->priv->mutex);

  parent_class->finalize (object);
}

static void
fs_multicast_transmitter_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_GST_SINK:
      g_value_set_object (value, self->priv->gst_sink);
      break;
    case PROP_GST_SRC:
      g_value_set_object (value, self->priv->gst_src);
      break;
    case PROP_COMPONENTS:
      g_value_set_uint (value, self->components);
      break;
    case PROP_TYPE_OF_SERVICE:
      g_mutex_lock (self->priv->mutex);
      g_value_set_uint (value, self->priv->type_of_service);
      g_mutex_unlock (self->priv->mutex);
      break;
    case PROP_DO_TIMESTAMP:
      g_value_set_boolean (value, self->priv->do_timestamp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_multicast_transmitter_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_COMPONENTS:
      self->components = g_value_get_uint (value);
      break;
    case PROP_TYPE_OF_SERVICE:
      fs_multicast_transmitter_set_type_of_service (self,
          g_value_get_uint (value));
      break;
    case PROP_DO_TIMESTAMP:
      self->priv->do_timestamp = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/**
 * fs_multicast_transmitter_new_stream_multicast_transmitter:
 * @transmitter: a #FsTranmitter
 * @participant: the #FsParticipant for which the #FsStream using this
 * new #FsStreamTransmitter is created
 *
 * This function will create a new #FsStreamTransmitter element for a
 * specific participant for this #FsMulticastTransmitter
 *
 * Returns: a new #FsStreamTransmitter
 */

static FsStreamTransmitter *
fs_multicast_transmitter_new_stream_transmitter (FsTransmitter *transmitter,
  FsParticipant *participant, guint n_parameters, GParameter *parameters,
  GError **error)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (transmitter);

  return FS_STREAM_TRANSMITTER (fs_multicast_stream_transmitter_newv (
        self, n_parameters, parameters, error));
}


/*
 * The UdpSock structure is a ref-counted pseudo-object use to represent
 * one local_ip:port:multicast_ip trio on which we listen and send,
 * so it includes a udpsrc and a multiudpsink. It represents one BSD socket.
 * The TTL used is the max TTL requested by any stream.
 */

struct _UdpSock {

  GstElement *udpsrc;
  GstPad *udpsrc_requested_pad;

  GstElement *udpsink;
  GstElement *udpsink_recvonly_filter;
  GstPad *udpsink_requested_pad;

  gchar *local_ip;
  gchar *multicast_ip;
  guint16 port;
  /* Protected by the transmitter mutex */
  guint8 current_ttl;

  gint fd;

  /* Protected by the transmitter mutex */
  GByteArray *ttls;

  /* These are just convenience pointers to our parent transmitter */
  GstElement *funnel;
  GstElement *tee;

  guint component_id;

  volatile gint sendcount;
};

static gboolean
_ip_string_into_sockaddr_in (const gchar *ip_as_string,
    struct sockaddr_in *sockaddr_in, GError **error)
{
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  int retval;

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_flags = AI_NUMERICHOST;
  retval = getaddrinfo (ip_as_string, NULL, &hints, &result);
  if (retval != 0) {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Invalid IP address %s passed: %s", ip_as_string,
        gai_strerror (retval));
    return FALSE;
  }
  memcpy (sockaddr_in, result->ai_addr, sizeof (struct sockaddr_in));
  freeaddrinfo (result);

  return TRUE;
}

static gint
_bind_port (
    const gchar *local_ip,
    const gchar *multicast_ip,
    guint16 port,
    guchar ttl,
    int type_of_service,
    GError **error)
{
  int sock = -1;
  struct sockaddr_in address;
  int retval;
  guchar loop = 1;
  int reuseaddr = 1;
#ifdef HAVE_IP_MREQN
  struct ip_mreqn mreq;
#else
  struct ip_mreq mreq;
#endif

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;

  g_assert (multicast_ip);

  if (!_ip_string_into_sockaddr_in (multicast_ip, &address, error))
    goto error;
  memcpy (&mreq.imr_multiaddr, &address.sin_addr,
      sizeof (mreq.imr_multiaddr));

  if (local_ip)
  {
    struct sockaddr_in tmpaddr;
    if (!_ip_string_into_sockaddr_in (local_ip, &tmpaddr, error))
      goto error;
#ifdef HAVE_IP_MREQN
    memcpy (&mreq.imr_address, &tmpaddr.sin_addr, sizeof (mreq.imr_address));
#else
    memcpy (&mreq.imr_interface, &tmpaddr.sin_addr, sizeof (mreq.imr_interface));
#endif
  }
  else
  {
#ifdef HAVE_IP_MREQN
    mreq.imr_address.s_addr = INADDR_ANY;
#else
    mreq.imr_interface.s_addr = INADDR_ANY;
#endif
  }

#ifdef HAVE_IP_MREQN
  mreq.imr_ifindex = 0;
#endif

  if ((sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) <= 0) {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
      "Error creating socket: %s", g_strerror (errno));
    goto error;
  }

  if (setsockopt (sock, IPPROTO_IP, IP_MULTICAST_TTL, (const void *)&ttl,
          sizeof (ttl)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Error setting the multicast TTL: %s",
        g_strerror (errno));
    goto error;
  }

  if (setsockopt (sock, IPPROTO_IP, IP_MULTICAST_LOOP, (const void *)&loop,
          sizeof (loop)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Error setting the multicast loop to FALSE: %s",
        g_strerror (errno));
    goto error;
  }

  if (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuseaddr,
          sizeof (reuseaddr)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Error setting reuseaddr to TRUE: %s",
        g_strerror (errno));
    goto error;
  }

#ifdef SO_REUSEPORT
  if (setsockopt (sock, SOL_SOCKET, SO_REUSEPORT, (const void *)&reuseaddr,
          sizeof (reuseaddr)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Error setting reuseaddr to TRUE: %s",
        g_strerror (errno));
    goto error;
  }
#endif

  if (setsockopt (sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
          (const void *)&mreq, sizeof (mreq)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Could not join the socket to the multicast group: %s",
        g_strerror (errno));
    goto error;
  }

  if (setsockopt (sock, IPPROTO_IP, IP_TOS,
          &type_of_service, sizeof (type_of_service)) < 0)
    GST_WARNING ("could not set socket ToS: %s", g_strerror (errno));

#ifdef IPV6_TCLASS
  if (setsockopt (sock, IPPROTO_IPV6, IPV6_TCLASS,
          &type_of_service, sizeof (type_of_service)) < 0)
    GST_WARNING ("could not set TCLASS: %s", g_strerror (errno));
#endif

  address.sin_port = htons (port);
  retval = bind (sock, (struct sockaddr *) &address, sizeof (address));
  if (retval != 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Could not bind to port %d", port);
    goto error;
  }

  return sock;

 error:
  if (sock >= 0)
    close (sock);
  return -1;
}

static GstElement *
_create_sinksource (gchar *elementname, GstBin *bin,
    GstElement *teefunnel, GstElement *filter, gint fd,
    GstPadDirection direction, GstPad **requested_pad, GError **error)
{
  GstElement *elem;
  GstPadLinkReturn ret = GST_PAD_LINK_OK;
  GstPad *elempad = NULL;
  GstStateChangeReturn state_ret;

  g_assert (direction == GST_PAD_SINK || direction == GST_PAD_SRC);

  elem = gst_element_factory_make (elementname, NULL);
  if (!elem) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not create the %s element", elementname);
    return NULL;
  }

  g_object_set (elem,
    "closefd", FALSE,
    "sockfd", fd,
    "auto-multicast", FALSE,
    NULL);

  if (!gst_bin_add (bin, elem)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the %s element to the gst %s bin", elementname,
      (direction == GST_PAD_SINK) ? "sink" : "src");
    gst_object_unref (elem);
    return NULL;
  }

  if (direction == GST_PAD_SINK)
    *requested_pad = gst_element_get_request_pad (teefunnel, "src%d");
  else
    *requested_pad = gst_element_get_request_pad (teefunnel, "sink%d");

  if (!*requested_pad) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not get the %s request pad from the %s",
      (direction == GST_PAD_SINK) ? "src" : "sink",
      (direction == GST_PAD_SINK) ? "tee" : "funnel");
    goto error;
  }

  if (direction == GST_PAD_SINK)
    elempad = gst_element_get_static_pad (elem, "sink");
  else
    elempad = gst_element_get_static_pad (elem, "src");

  if (filter)
  {
    GstPad *filterpad = NULL;

    if (!gst_bin_add (bin, filter))
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not add the filter element to the gst %s bin",
          (direction == GST_PAD_SINK) ? "sink" : "src");
      goto error;
    }

    if (direction == GST_PAD_SINK)
      filterpad = gst_element_get_static_pad (filter, "src");
    else
      filterpad = gst_element_get_static_pad (filter, "sink");

    if (direction == GST_PAD_SINK)
      ret = gst_pad_link (filterpad, elempad);
    else
      ret = gst_pad_link (elempad, filterpad);

    gst_object_unref (elempad);
    gst_object_unref (filterpad);
    elempad = NULL;

    if (GST_PAD_LINK_FAILED(ret))
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not link the new element %s (%d)", elementname, ret);
      goto error;
    }

    if (direction == GST_PAD_SINK)
      elempad = gst_element_get_static_pad (filter, "sink");
    else
      elempad = gst_element_get_static_pad (filter, "src");


    if (!gst_element_sync_state_with_parent (filter))
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not sync the state of the new filte rwith its parent");
      goto error;
    }
  }

  if (direction != GST_PAD_SINK)
    ret = gst_pad_link (elempad, *requested_pad);

  if (GST_PAD_LINK_FAILED(ret))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not link the new element %s (%d)", elementname, ret);
    goto error;
  }

  if (!gst_element_sync_state_with_parent (elem)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not sync the state of the new %s with its parent",
      elementname);
    goto error;
  }

  if (direction == GST_PAD_SINK)
    ret = gst_pad_link (*requested_pad, elempad);

  if (GST_PAD_LINK_FAILED(ret))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the new element %s (%d)", elementname, ret);
    goto error;
  }

  gst_object_unref (elempad);

  return elem;

 error:

  gst_element_set_locked_state (elem, TRUE);
  state_ret = gst_element_set_state (elem, GST_STATE_NULL);
  if (state_ret != GST_STATE_CHANGE_SUCCESS)
    GST_ERROR ("On error, could not reset %s to state NULL (%s)", elementname,
      gst_element_state_change_return_get_name (state_ret));
  if (!gst_bin_remove (bin, elem))
    GST_ERROR ("Could not remove element %s from bin on error", elementname);

  if (elempad)
    gst_object_unref (elempad);

  return NULL;
}

static UdpSock *
fs_multicast_transmitter_get_udpsock_locked (FsMulticastTransmitter *trans,
    guint component_id,
    const gchar *local_ip,
    const gchar *multicast_ip,
    guint16 port,
    guint8 ttl,
    gboolean sending,
    GError **error)
{
  UdpSock *udpsock;
  GList *udpsock_e;

  for (udpsock_e = g_list_first (trans->priv->udpsocks[component_id]);
       udpsock_e;
       udpsock_e = g_list_next (udpsock_e))
  {
    udpsock = udpsock_e->data;

    if (port == udpsock->port &&
        !strcmp (multicast_ip, udpsock->multicast_ip) &&
        ((local_ip == NULL && udpsock->local_ip == NULL) ||
            (local_ip && udpsock->local_ip &&
                !strcmp (local_ip, udpsock->local_ip))))
    {
      if (ttl > udpsock->current_ttl)
      {

        if (setsockopt (udpsock->fd, IPPROTO_IP, IP_MULTICAST_TTL,
                (const void *)&ttl, sizeof (ttl)) < 0)
        {
          g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
              "Error setting the multicast TTL: %s",
              g_strerror (errno));
          return NULL;
        }
        udpsock->current_ttl = ttl;
      }
      g_byte_array_append (udpsock->ttls, &ttl, 1);

      return udpsock;
    }
  }
  return NULL;
}

UdpSock *
fs_multicast_transmitter_get_udpsock (FsMulticastTransmitter *trans,
    guint component_id,
    const gchar *local_ip,
    const gchar *multicast_ip,
    guint16 port,
    guint8 ttl,
    gboolean sending,
    GError **error)
{
  UdpSock *udpsock;
  UdpSock *tmpudpsock;
  GError *local_error = NULL;
  int tos;

  /* First lets check if we already have one */
  if (component_id > trans->components)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "Invalid component %d > %d", component_id, trans->components);
    return NULL;
  }

  g_mutex_lock (trans->priv->mutex);
  udpsock = fs_multicast_transmitter_get_udpsock_locked (trans, component_id,
      local_ip, multicast_ip, port, ttl, sending, &local_error);
  tos = trans->priv->type_of_service;
  g_mutex_unlock (trans->priv->mutex);

  if (local_error)
  {
    g_propagate_error (error, local_error);
    return NULL;
  }

  if (udpsock)
  {
    if (sending)
      fs_multicast_transmitter_udpsock_inc_sending (udpsock);
    return udpsock;
  }

  udpsock = g_slice_new0 (UdpSock);

  udpsock->local_ip = g_strdup (local_ip);
  udpsock->multicast_ip = g_strdup (multicast_ip);
  udpsock->fd = -1;
  udpsock->component_id = component_id;
  udpsock->port = port;
  udpsock->current_ttl = ttl;
  udpsock->ttls = g_byte_array_new ();
  g_byte_array_append (udpsock->ttls, &ttl, 1);

  /* Now lets bind both ports */

  udpsock->fd = _bind_port (local_ip, multicast_ip, port, ttl, tos, error);
  if (udpsock->fd < 0)
    goto error;

  /* Now lets create the elements */

  udpsock->tee = trans->priv->udpsink_tees[component_id];
  udpsock->funnel = trans->priv->udpsrc_funnels[component_id];

  udpsock->udpsrc = _create_sinksource ("udpsrc",
      GST_BIN (trans->priv->gst_src), udpsock->funnel, NULL, udpsock->fd,
      GST_PAD_SRC, &udpsock->udpsrc_requested_pad, error);
  if (!udpsock->udpsrc)
    goto error;

  udpsock->udpsink_recvonly_filter = fs_transmitter_get_recvonly_filter (
      FS_TRANSMITTER (trans), udpsock->component_id);

  udpsock->udpsink = _create_sinksource ("multiudpsink",
      GST_BIN (trans->priv->gst_sink), udpsock->tee,
      udpsock->udpsink_recvonly_filter,
      udpsock->fd, GST_PAD_SINK, &udpsock->udpsink_requested_pad, error);
  if (!udpsock->udpsink)
    goto error;

  g_object_set (udpsock->udpsink,
      "async", FALSE,
      "sync", FALSE,
      NULL);

  g_mutex_lock (trans->priv->mutex);
  /* Check if someone else has added the same thing at the same time */
  tmpudpsock = fs_multicast_transmitter_get_udpsock_locked (trans, component_id,
      local_ip, multicast_ip, port, ttl, sending, &local_error);

  if (tmpudpsock || local_error)
  {
    g_mutex_unlock (trans->priv->mutex);
    fs_multicast_transmitter_put_udpsock (trans, udpsock, ttl);
    if (local_error)
    {
      g_propagate_error (error, local_error);
      goto error;
    }
    if (sending)
      fs_multicast_transmitter_udpsock_inc_sending (udpsock);
    return tmpudpsock;
  }

  trans->priv->udpsocks[component_id] =
    g_list_prepend (trans->priv->udpsocks[component_id], udpsock);
  g_mutex_unlock (trans->priv->mutex);

  if (udpsock->udpsink_recvonly_filter)
  {
    g_object_set (udpsock->udpsink_recvonly_filter, "sending", sending, NULL);
    g_signal_emit_by_name (udpsock->udpsink, "add", udpsock->multicast_ip,
        udpsock->port);
  }

  if (sending)
    fs_multicast_transmitter_udpsock_inc_sending (udpsock);

  return udpsock;

 error:

  if (udpsock)
    fs_multicast_transmitter_put_udpsock (trans, udpsock, ttl);

  return NULL;
}

void
fs_multicast_transmitter_put_udpsock (FsMulticastTransmitter *trans,
    UdpSock *udpsock, guint8 ttl)
{
  guint i;

  g_mutex_lock (trans->priv->mutex);
  for (i = udpsock->ttls->len - 1;; i--)
  {
    if (udpsock->ttls->data[i] == ttl)
    {
      g_byte_array_remove_index_fast (udpsock->ttls, i);
      break;
    }

    g_return_if_fail (i > 0);
  }

  if (udpsock->ttls->len > 0)
  {
    /* If we were the max, check if there is a new max */
    if (udpsock->current_ttl == ttl && ttl > 1)
    {
      guint8 max = 1;
      for (i = 0; i < udpsock->ttls->len; i++)
      {
        if (udpsock->ttls->data[i] > max)
          max = udpsock->ttls->data[i];
      }

      if (max != udpsock->current_ttl)
      {

        if (setsockopt (udpsock->fd, IPPROTO_IP, IP_MULTICAST_TTL,
                (const void *)&max, sizeof (max)) < 0)
        {
          GST_WARNING ("Error setting the multicast TTL to %u: %s", max,
              g_strerror (errno));
          g_mutex_unlock (trans->priv->mutex);
          return;
        }
        udpsock->current_ttl = max;
      }
    }
    g_mutex_unlock (trans->priv->mutex);
    return;
  }

  trans->priv->udpsocks[udpsock->component_id] =
    g_list_remove (trans->priv->udpsocks[udpsock->component_id], udpsock);

  g_mutex_unlock (trans->priv->mutex);

  if (udpsock->udpsrc)
  {
    GstStateChangeReturn ret;
    gst_element_set_locked_state (udpsock->udpsrc, TRUE);
    ret = gst_element_set_state (udpsock->udpsrc, GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      GST_ERROR ("Error changing state of udpsrc: %s",
          gst_element_state_change_return_get_name (ret));
    if (!gst_bin_remove (GST_BIN (trans->priv->gst_src), udpsock->udpsrc))
      GST_ERROR ("Could not remove udpsrc element from transmitter source");
  }

  if (udpsock->udpsrc_requested_pad)
  {
    gst_element_release_request_pad (udpsock->funnel,
      udpsock->udpsrc_requested_pad);
    gst_object_unref (udpsock->udpsrc_requested_pad);
  }

  if (udpsock->udpsink_requested_pad)
  {
    gst_element_release_request_pad (udpsock->tee,
      udpsock->udpsink_requested_pad);
    gst_object_unref (udpsock->udpsink_requested_pad);
  }

  if (udpsock->udpsink)
  {
    GstStateChangeReturn ret;
    gst_element_set_locked_state (udpsock->udpsink, TRUE);
    ret = gst_element_set_state (udpsock->udpsink, GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      GST_ERROR ("Error changing state of udpsink: %s",
          gst_element_state_change_return_get_name (ret));
    if (!gst_bin_remove (GST_BIN (trans->priv->gst_sink), udpsock->udpsink))
      GST_ERROR ("Could not remove udpsink element from transmitter source");
  }

  if (udpsock->udpsink_recvonly_filter)
  {
    GstStateChangeReturn ret;
    gst_element_set_locked_state (udpsock->udpsink_recvonly_filter, TRUE);
    ret = gst_element_set_state (udpsock->udpsink_recvonly_filter,
        GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      GST_ERROR ("Error changing state of udpsink filter: %s",
          gst_element_state_change_return_get_name (ret));
    if (!gst_bin_remove (GST_BIN (trans->priv->gst_sink),
            udpsock->udpsink_recvonly_filter))
      GST_ERROR ("Could not remove sink filter element from transmitter sink");
  }

  if (udpsock->fd >= 0)
    close (udpsock->fd);

  g_byte_array_free (udpsock->ttls, TRUE);
  g_free (udpsock->multicast_ip);
  g_free (udpsock->local_ip);
  g_slice_free (UdpSock, udpsock);
}

void
fs_multicast_transmitter_udpsock_inc_sending (UdpSock *udpsock)
{
  if (g_atomic_int_exchange_and_add (&udpsock->sendcount, 1) == 0)
  {
    if (udpsock->udpsink_recvonly_filter)
      g_object_set (udpsock->udpsink_recvonly_filter, "sending", TRUE, NULL);
    else
      g_signal_emit_by_name (udpsock->udpsink, "add", udpsock->multicast_ip,
          udpsock->port);

    gst_element_send_event (udpsock->udpsink,
        gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
            gst_structure_new ("GstForceKeyUnit",
                "all-headers", G_TYPE_BOOLEAN, TRUE,
                NULL)));
  }
}

void
fs_multicast_transmitter_udpsock_dec_sending (UdpSock *udpsock)
{
  if (g_atomic_int_dec_and_test (&udpsock->sendcount))
  {
    if (udpsock->udpsink_recvonly_filter)
      g_object_set (udpsock->udpsink_recvonly_filter, "sending", FALSE, NULL);
    else
      g_signal_emit_by_name (udpsock->udpsink, "remove", udpsock->multicast_ip,
          udpsock->port);
  }
}

static GType
fs_multicast_transmitter_get_stream_transmitter_type (
    FsTransmitter *transmitter)
{
  return FS_TYPE_MULTICAST_STREAM_TRANSMITTER;
}

void
fs_multicast_transmitter_udpsock_ref (FsMulticastTransmitter *trans,
    UdpSock *udpsock, guint8 ttl)
{
  g_mutex_lock (trans->priv->mutex);
  g_byte_array_append (udpsock->ttls, &ttl, 1);
  g_mutex_unlock (trans->priv->mutex);
}


static void
fs_multicast_transmitter_set_type_of_service (FsMulticastTransmitter *self,
    gint tos)
{
  gint i;

  g_mutex_lock (self->priv->mutex);
  if (self->priv->type_of_service == tos)
    goto out;

  self->priv->type_of_service = tos;

  for (i = 0; i < self->components; i++)
  {
    GList *item;

    for (item = self->priv->udpsocks[i]; item; item = item->next)
    {
      UdpSock *udpsock = item->data;

      if (setsockopt (udpsock->fd, IPPROTO_IP, IP_TOS,
              &tos, sizeof (tos)) < 0)
        GST_WARNING ( "could not set socket tos: %s", g_strerror (errno));

#ifdef IPV6_TCLASS
      if (setsockopt (udpsock->fd, IPPROTO_IPV6, IPV6_TCLASS,
              &tos, sizeof (tos)) < 0)
        GST_WARNING ("could not set TCLASS: %s", g_strerror (errno));
#endif
    }
  }

 out:
  g_mutex_unlock (self->priv->mutex);
}

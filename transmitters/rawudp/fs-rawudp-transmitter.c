/*
 * Farstream - Farstream RAW UDP with STUN Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rawudp-transmitter.h - A Farstream UDP transmitter with STUN
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
 * SECTION:fs-rawudp-transmitter
 * @short_description: A transmitter for raw udp (with STUN)
 *
 * This transmitter provides RAW udp (with stun)
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rawudp-transmitter.h"
#include "fs-rawudp-stream-transmitter.h"

#include <farstream/fs-conference.h>
#include <farstream/fs-plugin.h>

#include <string.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef G_OS_WIN32
# include <winsock2.h>
# define close closesocket
#else /*G_OS_WIN32*/
# include <netdb.h>
# include <sys/socket.h>
# include <netinet/ip.h>
# include <arpa/inet.h>
#endif /*G_OS_WIN32*/

GST_DEBUG_CATEGORY (fs_rawudp_transmitter_debug);
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
  PROP_GST_SINK,
  PROP_GST_SRC,
  PROP_COMPONENTS,
  PROP_TYPE_OF_SERVICE
};

struct _FsRawUdpTransmitterPrivate
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
  /* Protected by the mutex */
  GList **udpports;

  gint type_of_service;

  gboolean disposed;
};

#define FS_RAWUDP_TRANSMITTER_GET_PRIVATE(o)                            \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAWUDP_TRANSMITTER,        \
      FsRawUdpTransmitterPrivate))

static void fs_rawudp_transmitter_class_init (FsRawUdpTransmitterClass *klass);
static void fs_rawudp_transmitter_init (FsRawUdpTransmitter *self);
static void fs_rawudp_transmitter_constructed (GObject *object);
static void fs_rawudp_transmitter_dispose (GObject *object);
static void fs_rawudp_transmitter_finalize (GObject *object);

static void fs_rawudp_transmitter_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_rawudp_transmitter_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);

static FsStreamTransmitter *fs_rawudp_transmitter_new_stream_transmitter (
    FsTransmitter *transmitter,
    FsParticipant *participant,
    guint n_parameters,
    GParameter *parameters,
    GError **error);
static GType fs_rawudp_transmitter_get_stream_transmitter_type (
    FsTransmitter *transmitter);

static void fs_rawudp_transmitter_set_type_of_service (
    FsRawUdpTransmitter *self,
    gint tos);


static GObjectClass *parent_class = NULL;
//static guint signals[LAST_SIGNAL] = { 0 };


/*
 * Lets register the plugin
 */

static GType type = 0;

GType
fs_rawudp_transmitter_get_type (void)
{
  g_assert (type);
  return type;
}

static GType
fs_rawudp_transmitter_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsRawUdpTransmitterClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_rawudp_transmitter_class_init,
    NULL,
    NULL,
    sizeof (FsRawUdpTransmitter),
    0,
    (GInstanceInitFunc) fs_rawudp_transmitter_init
  };

  GST_DEBUG_CATEGORY_INIT (fs_rawudp_transmitter_debug,
      "fsrawudptransmitter", 0,
      "Farstream raw UDP transmitter");

  fs_rawudp_stream_transmitter_register_type (module);

  type = g_type_module_register_type (G_TYPE_MODULE (module),
      FS_TYPE_TRANSMITTER, "FsRawUdpTransmitter", &info, 0);

  return type;
}


FS_INIT_PLUGIN (fs_rawudp_transmitter_register_type)

static void
fs_rawudp_transmitter_class_init (FsRawUdpTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsTransmitterClass *transmitter_class = FS_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_rawudp_transmitter_set_property;
  gobject_class->get_property = fs_rawudp_transmitter_get_property;

  gobject_class->constructed = fs_rawudp_transmitter_constructed;

  g_object_class_override_property (gobject_class, PROP_GST_SRC, "gst-src");
  g_object_class_override_property (gobject_class, PROP_GST_SINK, "gst-sink");
  g_object_class_override_property (gobject_class, PROP_COMPONENTS,
      "components");
  g_object_class_override_property (gobject_class, PROP_TYPE_OF_SERVICE,
      "tos");

  transmitter_class->new_stream_transmitter =
    fs_rawudp_transmitter_new_stream_transmitter;
  transmitter_class->get_stream_transmitter_type =
    fs_rawudp_transmitter_get_stream_transmitter_type;

  gobject_class->dispose = fs_rawudp_transmitter_dispose;
  gobject_class->finalize = fs_rawudp_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsRawUdpTransmitterPrivate));
}

static void
fs_rawudp_transmitter_init (FsRawUdpTransmitter *self)
{

  /* member init */
  self->priv = FS_RAWUDP_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->components = 2;
  self->priv->mutex = g_mutex_new ();
}

static void
fs_rawudp_transmitter_constructed (GObject *object)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER_CAST (object);
  FsTransmitter *trans = FS_TRANSMITTER_CAST (self);
  GstPad *pad = NULL, *pad2 = NULL;
  GstPad *ghostpad = NULL;
  gchar *padname;
  GstPadLinkReturn ret;
  int c; /* component_id */


  /* We waste one space in order to have the index be the component_id */
  self->priv->udpsrc_funnels = g_new0 (GstElement *, self->components+1);
  self->priv->udpsink_tees = g_new0 (GstElement *, self->components+1);
  self->priv->udpports = g_new0 (GList *, self->components+1);

  /* First we need the src elemnet */

  self->priv->gst_src = gst_bin_new (NULL);

  if (!self->priv->gst_src)
  {
    trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not build the transmitter src bin");
    return;
  }

  gst_object_ref (self->priv->gst_src);


  /* Second, we do the sink element */

  self->priv->gst_sink = gst_bin_new (NULL);

  if (!self->priv->gst_sink)
  {
    trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not build the transmitter sink bin");
    return;
  }

  g_object_set (G_OBJECT (self->priv->gst_sink),
      "async-handling", TRUE,
      NULL);

  gst_object_ref (self->priv->gst_sink);

  for (c = 1; c <= self->components; c++)
  {
    GstElement *fakesink = NULL;

    /* Lets create the RTP source funnel */

    self->priv->udpsrc_funnels[c] = gst_element_factory_make ("fsfunnel", NULL);

    if (!self->priv->udpsrc_funnels[c])
    {
      trans->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not make the fsfunnel element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_src),
            self->priv->udpsrc_funnels[c]))
    {
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

    if (!self->priv->udpsink_tees[c])
    {
      trans->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not make the tee element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_sink),
            self->priv->udpsink_tees[c]))
    {
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

    if (!fakesink)
    {
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
        "sync", FALSE,
        NULL);

    pad = gst_element_get_request_pad (self->priv->udpsink_tees[c], "src%d");
    pad2 = gst_element_get_static_pad (fakesink, "sink");

    ret = gst_pad_link (pad, pad2);

    gst_object_unref (pad2);
    gst_object_unref (pad);

    if (GST_PAD_LINK_FAILED(ret))
    {
      trans->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not link the tee to the fakesink");
      return;
    }
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
}

static void
fs_rawudp_transmitter_dispose (GObject *object)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (object);

  if (self->priv->disposed)
    /* If dispose did already run, return. */
    return;

  if (self->priv->gst_src)
  {
    gst_object_unref (self->priv->gst_src);
    self->priv->gst_src = NULL;
  }

  if (self->priv->gst_sink)
  {
    gst_object_unref (self->priv->gst_sink);
    self->priv->gst_sink = NULL;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rawudp_transmitter_finalize (GObject *object)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (object);

  if (self->priv->udpsrc_funnels)
  {
    g_free (self->priv->udpsrc_funnels);
    self->priv->udpsrc_funnels = NULL;
  }

  if (self->priv->udpsink_tees)
  {
    g_free (self->priv->udpsink_tees);
    self->priv->udpsink_tees = NULL;
  }

  if (self->priv->udpports)
  {
    g_free (self->priv->udpports);
    self->priv->udpports = NULL;
  }

  g_mutex_free (self->priv->mutex);

  parent_class->finalize (object);
}

static void
fs_rawudp_transmitter_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (object);

  switch (prop_id)
  {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rawudp_transmitter_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_COMPONENTS:
      self->components = g_value_get_uint (value);
      break;
    case PROP_TYPE_OF_SERVICE:
      fs_rawudp_transmitter_set_type_of_service (self,
          g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/**
 * fs_rawudp_transmitter_new_stream_rawudp_transmitter:
 * @transmitter: a #FsTranmitter
 * @participant: the #FsParticipant for which the #FsStream using this
 * new #FsStreamTransmitter is created
 *
 * This function will create a new #FsStreamTransmitter element for a
 * specific participant for this #FsRawUdpTransmitter
 *
 * Returns: a new #FsStreamTransmitter
 */

static FsStreamTransmitter *
fs_rawudp_transmitter_new_stream_transmitter (FsTransmitter *transmitter,
    FsParticipant *participant,
    guint n_parameters,
    GParameter *parameters,
    GError **error)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (transmitter);

  return FS_STREAM_TRANSMITTER (fs_rawudp_stream_transmitter_newv (
        self, n_parameters, parameters, error));
}


/*
 * The UdpPort structure is a ref-counted pseudo-object use to represent
 * one ip:port combo on which we listen and send, so it includes  a udpsrc
 * and a multiudpsink
 */

struct _UdpPort {
  /* Protected by the transmitter mutex */
  gint refcount;

  GstElement *udpsrc;
  GstPad *udpsrc_requested_pad;

  GstElement *udpsink;
  GstPad *udpsink_requested_pad;

  GstElement *recvonly_filter;
  GstElement *recvonly_udpsink;
  GstPad *recvonly_requested_pad;

  gchar *requested_ip;
  guint requested_port;

  guint port;

  gint fd;

  /* These are just convenience pointers to our parent transmitter */
  GstElement *funnel;
  GstElement *tee;

  guint component_id;

  /* Everything below is protected by the mutex */
  GMutex *mutex;
  GArray *known_addresses;
};

struct KnownAddress {
  FsRawUdpAddressUniqueCallbackFunc callback;
  gpointer user_data;
  GstNetAddress addr;
};

static gint
_bind_port (
    const gchar *ip,
    guint port,
    guint *used_port,
    int tos,
    GError **error)
{
  int sock;
  struct sockaddr_in address;
  int retval;

  memset (&address, 0, sizeof(struct sockaddr_in));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;

  if (ip)
  {
    struct addrinfo hints;
    struct addrinfo *result = NULL;

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_NUMERICHOST;
    retval = getaddrinfo (ip, NULL, &hints, &result);
    if (retval != 0)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "Invalid IP address %s passed: %s", ip, gai_strerror (retval));
      return -1;
    }
    memcpy (&address, result->ai_addr, sizeof (struct sockaddr_in));
    freeaddrinfo (result);
  }

  if ((sock = socket (AF_INET, SOCK_DGRAM, 0)) <= 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Error creating socket: %s", g_strerror (errno));
    return -1;
  }

  do {
    address.sin_port = htons (port);
    retval = bind (sock, (struct sockaddr *) &address, sizeof (address));
    if (retval != 0)
    {
      GST_INFO ("could not bind port %d", port);
      port += 2;
      if (port > 65535)
      {
        g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
            "Could not bind the socket to a port");
        close (sock);
        return -1;
      }
    }
  } while (retval != 0);

  *used_port = port;

  if (setsockopt (sock, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0)
    GST_WARNING ("could not set socket ToS: %s", g_strerror (errno));

#ifdef IPV6_TCLASS
  if (setsockopt (sock, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof (tos)) < 0)
    GST_WARNING ("could not set TCLASS: %s", g_strerror (errno));
#endif

  return sock;
}

static GstElement *
_create_sinksource (
    gchar *elementname,
    GstBin *bin,
    GstElement *teefunnel,
    GstElement *filter,
    gint fd,
    GstPadDirection direction,
    GstPad **requested_pad,
    GError **error)
{
  GstElement *elem;
  GstPadLinkReturn ret = GST_PAD_LINK_OK;
  GstPad *elempad = NULL;
  GstStateChangeReturn state_ret;

  g_assert (direction == GST_PAD_SINK || direction == GST_PAD_SRC);

  elem = gst_element_factory_make (elementname, NULL);
  if (!elem)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create the %s element", elementname);
    return NULL;
  }

  g_object_set (elem,
      "auto-multicast", FALSE,
      "closefd", FALSE,
      "sockfd", fd,
      NULL);

  if (direction == GST_PAD_SINK)
    g_object_set (elem,
        "async", FALSE,
        "sync", FALSE,
        NULL);

  if (!gst_bin_add (bin, elem))
  {
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

  if (!*requested_pad)
  {
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

  if (!gst_element_sync_state_with_parent (elem))
  {
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


static UdpPort *
fs_rawudp_transmitter_get_udpport_locked (FsRawUdpTransmitter *trans,
    guint component_id,
    const gchar *requested_ip,
    guint requested_port)
{
  UdpPort *udpport;
  GList *udpport_e;

  for (udpport_e = g_list_first (trans->priv->udpports[component_id]);
       udpport_e;
       udpport_e = g_list_next (udpport_e))
  {
    udpport = udpport_e->data;
    if (requested_port == udpport->requested_port &&
        ((requested_ip == NULL && udpport->requested_ip == NULL) ||
            (requested_ip && udpport->requested_ip &&
                !strcmp (requested_ip, udpport->requested_ip))))
    {
      GST_LOG ("Got port refcount %d->%d", udpport->refcount,
          udpport->refcount+1);
      udpport->refcount++;
      return udpport;
    }
  }

  return NULL;
}


UdpPort *
fs_rawudp_transmitter_get_udpport (FsRawUdpTransmitter *trans,
    guint component_id,
    const gchar *requested_ip,
    guint requested_port,
    GError **error)
{
  UdpPort *udpport;
  UdpPort *tmpudpport;
  int tos;

  /* First lets check if we already have one */
  if (component_id > trans->components)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid component %d > %d", component_id, trans->components);
    return NULL;
  }

  g_mutex_lock (trans->priv->mutex);
  udpport = fs_rawudp_transmitter_get_udpport_locked (trans, component_id,
      requested_ip, requested_port);
  tos = trans->priv->type_of_service;
  g_mutex_unlock (trans->priv->mutex);

  if (udpport)
    return udpport;

  GST_DEBUG ("Make new UdpPort for component %u requesting %s:%u", component_id,
      requested_ip ? requested_ip : "ANY", requested_port);

  udpport = g_slice_new0 (UdpPort);

  udpport->refcount = 1;
  udpport->requested_ip = g_strdup (requested_ip);
  udpport->requested_port = requested_port;
  udpport->fd = -1;
  udpport->component_id = component_id;
  udpport->mutex = g_mutex_new ();
  udpport->known_addresses = g_array_new (TRUE, FALSE,
      sizeof (struct KnownAddress));

  /* Now lets bind both ports */

  udpport->fd = _bind_port (requested_ip, requested_port, &udpport->port, tos,
      error);
  if (udpport->fd < 0)
    goto error;

  /* Now lets create the elements */

  udpport->tee = trans->priv->udpsink_tees[component_id];
  udpport->funnel = trans->priv->udpsrc_funnels[component_id];

  udpport->udpsrc = _create_sinksource ("udpsrc",
      GST_BIN (trans->priv->gst_src), udpport->funnel, NULL,
      udpport->fd, GST_PAD_SRC, &udpport->udpsrc_requested_pad, error);
  if (!udpport->udpsrc)
    goto error;

  udpport->udpsink = _create_sinksource ("multiudpsink",
      GST_BIN (trans->priv->gst_sink), udpport->tee, NULL,
      udpport->fd, GST_PAD_SINK, &udpport->udpsink_requested_pad, error);
  if (!udpport->udpsink)
    goto error;

  udpport->recvonly_filter = fs_transmitter_get_recvonly_filter (
      FS_TRANSMITTER (trans), udpport->component_id);

  if (udpport->recvonly_filter)
  {
    udpport->recvonly_udpsink = _create_sinksource ("multiudpsink",
        GST_BIN (trans->priv->gst_sink), udpport->tee, udpport->recvonly_filter,
        udpport->fd, GST_PAD_SINK, &udpport->recvonly_requested_pad, error);
    if (!udpport->recvonly_udpsink)
      goto error;
  }

  g_mutex_lock (trans->priv->mutex);

  /* Check if someone else added the same port at the same time */
  tmpudpport = fs_rawudp_transmitter_get_udpport_locked (trans, component_id,
      requested_ip, requested_port);

  if (tmpudpport)
  {
    g_mutex_unlock (trans->priv->mutex);
    fs_rawudp_transmitter_put_udpport (trans, udpport);
    return tmpudpport;
  }

  trans->priv->udpports[component_id] =
    g_list_prepend (trans->priv->udpports[component_id], udpport);
  g_mutex_unlock (trans->priv->mutex);

  return udpport;

 error:
  if (udpport)
    fs_rawudp_transmitter_put_udpport (trans, udpport);
  return NULL;
}

void
fs_rawudp_transmitter_put_udpport (FsRawUdpTransmitter *trans,
  UdpPort *udpport)
{
  GST_LOG ("Put port refcount %d->%d", udpport->refcount, udpport->refcount-1);

  g_mutex_lock (trans->priv->mutex);

  if (udpport->refcount > 1)
  {
    udpport->refcount--;
    g_mutex_unlock (trans->priv->mutex);
    return;
  }

  trans->priv->udpports[udpport->component_id] =
    g_list_remove (trans->priv->udpports[udpport->component_id], udpport);

  g_mutex_unlock (trans->priv->mutex);

  if (udpport->udpsrc)
  {
    GstStateChangeReturn ret;
    gst_element_set_locked_state (udpport->udpsrc, TRUE);
    ret = gst_element_set_state (udpport->udpsrc, GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      GST_ERROR ("Error changing state of udpsrc: %s",
          gst_element_state_change_return_get_name (ret));
    if (!gst_bin_remove (GST_BIN (trans->priv->gst_src), udpport->udpsrc))
      GST_ERROR ("Could not remove udpsrc element from transmitter source");
  }

  if (udpport->udpsrc_requested_pad)
  {
    gst_element_release_request_pad (udpport->funnel,
        udpport->udpsrc_requested_pad);
    gst_object_unref (udpport->udpsrc_requested_pad);
  }

  if (udpport->udpsink_requested_pad)
  {
    gst_element_release_request_pad (udpport->tee,
        udpport->udpsink_requested_pad);
    gst_object_unref (udpport->udpsink_requested_pad);
  }

  if (udpport->udpsink)
  {
    GstStateChangeReturn ret;
    gst_element_set_locked_state (udpport->udpsink, TRUE);
    ret = gst_element_set_state (udpport->udpsink, GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      GST_ERROR ("Error changing state of udpsink: %s",
          gst_element_state_change_return_get_name (ret));
    if (!gst_bin_remove (GST_BIN (trans->priv->gst_sink), udpport->udpsink))
      GST_ERROR ("Could not remove udpsink element from transmitter source");
  }

  if (udpport->recvonly_requested_pad)
  {
    gst_element_release_request_pad (udpport->tee,
        udpport->recvonly_requested_pad);
    gst_object_unref (udpport->recvonly_requested_pad);
  }

  if (udpport->recvonly_udpsink)
  {
    GstStateChangeReturn ret;
    gst_element_set_locked_state (udpport->recvonly_udpsink, TRUE);
    ret = gst_element_set_state (udpport->recvonly_udpsink, GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      GST_ERROR ("Error changing state of udpsink: %s",
          gst_element_state_change_return_get_name (ret));
    if (!gst_bin_remove (GST_BIN (trans->priv->gst_sink),
            udpport->recvonly_udpsink))
      GST_ERROR ("Could not remove udpsink element from transmitter source");
  }

  if (udpport->fd >= 0)
    close (udpport->fd);

  if (udpport->mutex)
    g_mutex_free (udpport->mutex);
  if (udpport->known_addresses)
    g_array_free (udpport->known_addresses, TRUE);

  g_free (udpport->requested_ip);
  g_slice_free (UdpPort, udpport);
}

void
fs_rawudp_transmitter_udpport_add_dest (UdpPort *udpport,
    const gchar *ip,
    gint port)
{
  GST_DEBUG ("Adding dest %s:%d", ip, port);
  g_signal_emit_by_name (udpport->udpsink, "add", ip, port);
  gst_element_send_event (udpport->udpsink,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("GstForceKeyUnit",
              "all-headers", G_TYPE_BOOLEAN, TRUE,
              NULL)));
}


void
fs_rawudp_transmitter_udpport_remove_dest (UdpPort *udpport,
  const gchar *ip,
    gint port)
{
  g_signal_emit_by_name (udpport->udpsink, "remove", ip, port);
}

gboolean
fs_rawudp_transmitter_udpport_sendto (UdpPort *udpport,
    gchar *msg,
    size_t len,
    const struct sockaddr *to,
    socklen_t tolen,
    GError **error)
{
  if (sendto (udpport->fd, msg, len, 0, to, tolen) != len)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Could not send STUN request: %s", g_strerror (errno));
    return FALSE;
  }

  return TRUE;
}

gulong
fs_rawudp_transmitter_udpport_connect_recv (UdpPort *udpport,
    GCallback callback,
    gpointer user_data)
{
  GstPad *pad;
  gulong id;

  pad = gst_element_get_static_pad (udpport->udpsrc, "src");

  id = gst_pad_add_buffer_probe (pad, callback, user_data);

  gst_object_unref (pad);

  return id;
}


void
fs_rawudp_transmitter_udpport_disconnect_recv (UdpPort *udpport,
    gulong id)
{
  GstPad *pad = gst_element_get_static_pad (udpport->udpsrc, "src");

  gst_pad_remove_buffer_probe (pad, id);

  gst_object_unref (pad);
}

gboolean
fs_rawudp_transmitter_udpport_is_pad (UdpPort *udpport,
    GstPad *pad)
{
  GstPad *mypad;
  gboolean res;

  mypad =  gst_element_get_static_pad (udpport->udpsrc, "src");

  res = (mypad == pad);

  gst_object_unref (mypad);

  return res;
}


gint
fs_rawudp_transmitter_udpport_get_port (UdpPort *udpport)
{
  return udpport->port;
}


static GType
fs_rawudp_transmitter_get_stream_transmitter_type (FsTransmitter *transmitter)
{
  return FS_TYPE_RAWUDP_STREAM_TRANSMITTER;
}

/**
 * fs_rawudp_transmitter_udpport_add_known_address:
 * @udpport: a #UdpPort
 * @address: the new #GstNetAddress that we know
 * @callback: a Callback that will be called if the uniqueness of an address
 *   changes
 * @user_data: data passed back to the callback
 *
 * This function stores the passed address and tells the caller if it was
 * unique or not. The callback is called when the uniqueness changes.
 *
 * Returns: %TRUE if the new address is unique, %FALSE otherwise
 */

gboolean
fs_rawudp_transmitter_udpport_add_known_address (UdpPort *udpport,
    GstNetAddress *address,
    FsRawUdpAddressUniqueCallbackFunc callback,
    gpointer user_data)
{
  gint i;
  gboolean unique = FALSE;
  struct KnownAddress newka = {0};
  guint counter = 0;
  struct KnownAddress *prev_ka = NULL;

  g_mutex_lock (udpport->mutex);

  for (i = 0;
       g_array_index (udpport->known_addresses, struct KnownAddress, i).callback;
       i++)
  {
    struct KnownAddress *ka = &g_array_index (udpport->known_addresses, struct KnownAddress, i);
    if (gst_netaddress_equal (address, &ka->addr))
    {
      g_assert (!(ka->callback == callback && ka->user_data == user_data));

      prev_ka = ka;
      counter++;
    }
  }

  if (counter == 0)
  {
    unique = TRUE;
  }
  else if (counter == 1)
  {
    if (prev_ka->callback)
      prev_ka->callback (FALSE, &prev_ka->addr, prev_ka->user_data);
  }

  memcpy (&newka.addr, address, sizeof (GstNetAddress));
  newka.callback = callback;
  newka.user_data = user_data;

  g_array_append_val (udpport->known_addresses, newka);

  g_mutex_unlock (udpport->mutex);

  return unique;
}

/**
 * fs_rawudp_transmitter_udpport_remove_known_address:
 * @udpport: a #UdpPort
 * @address: the address to remove
 * @callback: the callback passed to the corresponding
 *  fs_rawudp_transmitter_udpport_add_known_address() call
 * @user_data: the user_data passed to the corresponding
 *  fs_rawudp_transmitter_udpport_add_known_address() call
 *
 * Removes a known address from the list and calls the notifiers if another
 * address becomes unique
 */

void
fs_rawudp_transmitter_udpport_remove_known_address (UdpPort *udpport,
    GstNetAddress *address,
    FsRawUdpAddressUniqueCallbackFunc callback,
    gpointer user_data)
{
  gint i;
  gint remove_i = -1;
  guint counter = 0;
  struct KnownAddress *prev_ka = NULL;

  g_mutex_lock (udpport->mutex);

  for (i = 0;
       g_array_index (udpport->known_addresses, struct KnownAddress, i).callback;
       i++)
  {
    struct KnownAddress *ka = &g_array_index (udpport->known_addresses, struct KnownAddress, i);
    if (gst_netaddress_equal (address, &ka->addr))
    {
      if (ka->callback == callback && ka->user_data == user_data)
      {
        remove_i = i;
      }
      else
      {
        counter++;
        prev_ka = ka;
      }
    }
  }

  if (remove_i == -1)
  {
    GST_ERROR ("Tried to remove unknown known address");
    goto out;
  }

  if (counter == 1)
    prev_ka->callback (TRUE, &prev_ka->addr, prev_ka->user_data);

  g_array_remove_index_fast (udpport->known_addresses, remove_i);

 out:

  g_mutex_unlock (udpport->mutex);
}

void
fs_rawudp_transmitter_udpport_add_recvonly_dest (UdpPort *udpport,
    const gchar *ip,
    gint port)
{
  if (udpport->recvonly_udpsink)
    g_signal_emit_by_name (udpport->recvonly_udpsink, "add", ip, port);
}


void
fs_rawudp_transmitter_udpport_remove_recvonly_dest (UdpPort *udpport,
    const gchar *ip,
    gint port)
{
  if (udpport->recvonly_udpsink)
    g_signal_emit_by_name (udpport->recvonly_udpsink, "remove", ip, port);
}

static void
fs_rawudp_transmitter_set_type_of_service (FsRawUdpTransmitter *self,
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

    for (item = self->priv->udpports[i]; item; item = item->next)
    {
      UdpPort *udpport = item->data;

      if (setsockopt (udpport->fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0)
        GST_WARNING ( "could not set socket ToS: %s", g_strerror (errno));

#ifdef IPV6_TCLASS
      if (setsockopt (udpport->fd, IPPROTO_IPV6, IPV6_TCLASS,
              &tos, sizeof (tos)) < 0)
        GST_WARNING ("could not set TCLASS: %s", g_strerror (errno));
#endif
    }
  }

 out:
  g_mutex_unlock (self->priv->mutex);
}

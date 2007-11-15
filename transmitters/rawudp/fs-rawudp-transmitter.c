/*
 * Farsight2 - Farsight RAW UDP with STUN Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rawudp-transmitter.h - A Farsight UDP transmitter with STUN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#include <gst/farsight/fs-session.h>
#include <gst/farsight/fs-stream.h>

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>


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
  PROP_GST_SRC
};

struct _FsRawUdpTransmitterPrivate
{
  /* We hold references to this element */
  GstElement *gst_sink;
  GstElement *gst_src;

  /* We don't hold a reference to these elements, they are owned
     by the bins */
  GstElement *udpsrc_funnel;
  GstElement *udprtcpsrc_funnel;
  GstElement *udpsink_tee;
  GstElement *udprtcpsink_tee;

  GList *rtp_udpports;
  GList *rtcp_udpports;

  gboolean disposed;
};

#define FS_RAWUDP_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAWUDP_TRANSMITTER, \
    FsRawUdpTransmitterPrivate))

static void fs_rawudp_transmitter_class_init (FsRawUdpTransmitterClass *klass);
static void fs_rawudp_transmitter_init (FsRawUdpTransmitter *self);
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
    FsTransmitter *transmitter, FsParticipant *participant,
    guint n_parameters, GParameter *parameters, GError **error);


static GObjectClass *parent_class = NULL;
//static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_rawudp_transmitter_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
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

    type = g_type_register_static (FS_TYPE_TRANSMITTER,
        "FsRawUdpTransmitter", &info, 0);
  }

  return type;
}

static void
fs_rawudp_transmitter_class_init (FsRawUdpTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsTransmitterClass *transmitter_class = FS_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_rawudp_transmitter_set_property;
  gobject_class->get_property = fs_rawudp_transmitter_get_property;

  g_object_class_override_property (gobject_class, PROP_GST_SRC, "gst-src");
  g_object_class_override_property (gobject_class, PROP_GST_SINK, "gst-sink");

  transmitter_class->new_stream_transmitter =
    fs_rawudp_transmitter_new_stream_transmitter;

  gobject_class->dispose = fs_rawudp_transmitter_dispose;
  gobject_class->finalize = fs_rawudp_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsRawUdpTransmitterPrivate));
}

static void
fs_rawudp_transmitter_init (FsRawUdpTransmitter *self)
{
  FsTransmitter *trans = FS_TRANSMITTER_CAST (self);
  GstPad *pad = NULL;
  GstPad *ghostpad = NULL;

  /* member init */
  self->priv = FS_RAWUDP_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  /* First we need the src elemnet */

  self->priv->gst_src = gst_element_factory_make ("bin", NULL);

  if (!self->priv->gst_src) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not build the transmitter src bin");
    return;
  }

  /* Lets create the RTP source funnel */

  self->priv->udpsrc_funnel = gst_element_factory_make ("fsfunnel", NULL);

  if (!self->priv->udpsrc_funnel) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not make the fsfunnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->gst_src), self->priv->udpsrc_funnel)) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the fsfunnel element to the transmitter src bin");
  }

  pad = gst_element_get_static_pad (self->priv->udpsrc_funnel, "src");
  ghostpad = gst_ghost_pad_new ("src", pad);
  gst_object_unref (pad);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (self->priv->gst_src, ghostpad);

  /* Lets create the RTCP source funnel*/

  self->priv->udprtcpsrc_funnel = gst_element_factory_make ("fsfunnel", NULL);

  if (!self->priv->udprtcpsrc_funnel) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not make the fsfunnnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->gst_src),
      self->priv->udprtcpsrc_funnel)) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the rtcp fsfunnel element to the transmitter src bin");
  }

  pad = gst_element_get_static_pad (self->priv->udprtcpsrc_funnel, "src");
  ghostpad = gst_ghost_pad_new ("rtcpsrc", pad);
  gst_object_unref (pad);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (self->priv->gst_src, ghostpad);


  /* Second, we do the sink element */

  self->priv->gst_sink = gst_element_factory_make ("bin", NULL);

  if (!self->priv->gst_sink) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not build the transmitter sink bin");
    return;
  }

  /* Lets create the RTP source tee */

  self->priv->udpsink_tee = gst_element_factory_make ("tee", NULL);

  if (!self->priv->udpsink_tee) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not make the tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->gst_sink), self->priv->udpsink_tee)) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the tee element to the transmitter sink bin");
  }

  pad = gst_element_get_static_pad (self->priv->udpsink_tee, "sink");
  ghostpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (self->priv->gst_sink, ghostpad);

  /* Lets create the RTCP source tee*/

  self->priv->udprtcpsink_tee = gst_element_factory_make ("tee", NULL);

  if (!self->priv->udprtcpsink_tee) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not make the fsfunnnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->gst_sink),
      self->priv->udprtcpsink_tee)) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the rtcp tee element to the transmitter sink bin");
  }

  pad = gst_element_get_static_pad (self->priv->udprtcpsink_tee, "sink");
  ghostpad = gst_ghost_pad_new ("rtcpsink", pad);
  gst_object_unref (pad);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (self->priv->gst_sink, ghostpad);
}

static void
fs_rawudp_transmitter_dispose (GObject *object)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (object);

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
fs_rawudp_transmitter_finalize (GObject *object)
{
  parent_class->finalize (object);
}

static void
fs_rawudp_transmitter_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_GST_SINK:
      g_value_set_object (value, self->priv->gst_sink);
      break;
    case PROP_GST_SRC:
      g_value_set_object (value, self->priv->gst_src);
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
  FsParticipant *participant, guint n_parameters, GParameter *parameters,
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
  gint refcount;

  GstElement *udpsrc;
  GstPad *udpsrc_requested_pad;

  GstElement *udpsink;
  GstPad *udpsink_requested_pad;

  gchar *requested_ip;
  guint requested_port;

  guint port;

  gint fd;

  guint component_id;
};

static gint
_bind_port (const gchar *ip, guint port, guint *used_port, GError **error)
{
  int sock;
  struct sockaddr_in address;
  int retval;

  if (ip) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_NUMERICHOST;
    retval = getaddrinfo (ip, NULL, &hints, &result);
    if (retval != 0) {
      *error = g_error_new (FS_STREAM_ERROR, FS_STREAM_ERROR_NETWORK,
        "Invalid IP address %s passed: %s", ip, gai_strerror (retval));
      return -1;
    }
    memcpy (&address, result->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo (result);
  } else {
    address.sin_addr.s_addr = INADDR_ANY;
  }
  address.sin_family = AF_INET;
  address.sin_port = htons (port);


  if ((sock = socket (AF_INET, SOCK_DGRAM, 0)) <= 0) {
    *error = g_error_new (FS_STREAM_ERROR, FS_STREAM_ERROR_NETWORK,
      "Error creating socket: %s", g_strerror (errno));
    return -1;
  }

  do {
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons (port);
    retval = bind (sock, (struct sockaddr *) &address, sizeof (address));
    if (retval != 0)
    {
      g_debug ("could not bind port %d", port);
      port += 2;
      if (port > 65535) {
        *error = g_error_new (FS_STREAM_ERROR, FS_STREAM_ERROR_NETWORK,
          "Could not bind the socket to a port");
        close (sock);
        return -1;
      }
    }
  } while (retval != 0);

  *used_port = port;

  return sock;
}

static GstElement *
_create_sinksource (gchar *elementname, GstBin *bin,
  GstElement *teefunnel, gint fd, GstPadDirection direction,
  GstPad **requested_pad, GError **error)
{
  GstElement *elem;
  GstPadLinkReturn ret;
  GstPad *ourpad = NULL;

  g_assert (direction == GST_PAD_SINK || direction == GST_PAD_SRC);

  elem = gst_element_factory_make (elementname, NULL);
  if (!elem) {
    g_set_error (error, FS_SESSION_ERROR, FS_SESSION_ERROR_CONSTRUCTION,
      "Could not create the %s element", elementname);
    return NULL;
  }

  g_object_set (elem,
    "closefd", FALSE,
    "sockfd", fd,
    NULL);

  if (!gst_bin_add (bin, elem)) {
    g_set_error (error, FS_SESSION_ERROR, FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the udpsrc element to the gst sink");
    gst_object_unref (elem);
    return NULL;
  }

  if (direction == GST_PAD_SINK)
    *requested_pad = gst_element_get_request_pad (teefunnel, "sink%d");
  else
    *requested_pad = gst_element_get_request_pad (teefunnel, "src%d");

  if (!*requested_pad) {
    g_set_error (error, FS_SESSION_ERROR, FS_SESSION_ERROR_CONSTRUCTION,
      "Could not get the %s request pad from the %s",
      (direction == GST_PAD_SINK) ? "sink" : "src",
      (direction == GST_PAD_SINK) ? "tee" : "funnel");
    goto error;
  }

  if (direction == GST_PAD_SINK)
    ourpad = gst_element_get_static_pad (elem, "src");
  else
    ourpad = gst_element_get_static_pad (elem, "sink");

  if (direction == GST_PAD_SINK)
    ret = gst_pad_link (*requested_pad, ourpad);
  else
    ret = gst_pad_link (ourpad, *requested_pad);

  if (GST_PAD_LINK_FAILED(ret)) {
    g_set_error (error, FS_SESSION_ERROR, FS_SESSION_ERROR_CONSTRUCTION,
      "Could not link the new element %s (%d)", elementname, ret);
    goto error;
  }

  if (!gst_element_sync_state_with_parent (elem)) {
    g_set_error (error, FS_SESSION_ERROR, FS_SESSION_ERROR_CONSTRUCTION,
      "Could not sync the state of the new %s with its parent",
      elementname);
    goto error;
  }

  return elem;

 error:
  gst_element_set_state (elem, GST_STATE_NULL);
  gst_bin_remove (bin, elem);
  return NULL;
}


UdpPort *
fs_rawudp_transmitter_get_udpport (FsRawUdpTransmitter *trans,
  guint component_id, const gchar *requested_ip, guint requested_port,
  GError **error)
{
  UdpPort *udpport;
  GList *udpport_e;

  /* First lets check if we already have one */

  if (component_id == FS_COMPONENT_RTP)
    udpport_e = g_list_first (trans->priv->rtp_udpports);
  else if (component_id == FS_COMPONENT_RTCP)
    udpport_e = g_list_first (trans->priv->rtcp_udpports);
  else {
    g_set_error (error, FS_STREAM_ERROR, FS_STREAM_ERROR_INVALID_ARGUMENTS,
      "Invalid component %d", component_id);
    return NULL;
  }

  for (; udpport_e;
       udpport_e = g_list_next (udpport_e)) {
    udpport = udpport_e->data;
    if (requested_port == udpport->requested_port &&
        ((requested_ip == NULL && udpport->requested_ip == NULL) ||
          !strcmp (requested_ip, udpport->requested_ip))) {
      udpport->refcount++;
      return udpport;
    }
  }

  udpport = g_new0 (UdpPort, 1);

  udpport->refcount = 1;
  udpport->requested_ip = g_strdup (requested_ip);
  udpport->requested_port = requested_port;
  udpport->fd = -1;
  udpport->component_id = component_id;

  /* Now lets bind both ports */

  udpport->fd = _bind_port (requested_ip, requested_port, &udpport->port,
    error);
  if (udpport->fd < 0)
    goto error;

  /* Now lets create the elements */

  udpport->udpsrc = _create_sinksource ("udpsrc",
    GST_BIN (trans->priv->gst_src), trans->priv->udpsrc_funnel,
    udpport->fd, GST_PAD_SRC, &udpport->udpsrc_requested_pad,
    error);
  if (!udpport->udpsrc)
    goto error;

  udpport->udpsink = _create_sinksource ("udpsink",
    GST_BIN (trans->priv->gst_sink), trans->priv->udpsink_tee,
    udpport->fd, GST_PAD_SINK, &udpport->udpsink_requested_pad,
    error);
  if (!udpport->udpsink)
    goto error;

  if (component_id != FS_COMPONENT_RTP)
    g_object_set (udpport->udpsink, "async", FALSE, NULL);

  if (component_id == FS_COMPONENT_RTP)
    trans->priv->rtp_udpports = g_list_prepend (trans->priv->rtp_udpports,
      udpport);
  else if (component_id == FS_COMPONENT_RTCP)
    trans->priv->rtcp_udpports = g_list_prepend (trans->priv->rtcp_udpports,
      udpport);

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
  if (udpport->refcount > 1) {
    udpport->refcount--;
    return;
  }

  if (udpport->component_id == FS_COMPONENT_RTP)
    trans->priv->rtp_udpports = g_list_remove (trans->priv->rtp_udpports,
      udpport);
  else if (udpport->component_id == FS_COMPONENT_RTCP)
    trans->priv->rtcp_udpports = g_list_remove (trans->priv->rtcp_udpports,
      udpport);

  if (udpport->udpsrc) {
    GstStateChangeReturn ret;
    ret = gst_element_set_state (udpport->udpsrc, GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS) {
      g_warning ("Error changing state of udpsrc: %d", ret);
    }
    gst_bin_remove (GST_BIN (trans->priv->gst_src), udpport->udpsrc);
  }

  if (udpport->udpsrc_requested_pad) {
    gst_element_release_request_pad (trans->priv->udpsrc_funnel,
      udpport->udpsrc_requested_pad);
  }

  if (udpport->udpsink) {
    GstStateChangeReturn ret;
    gst_object_ref (udpport->udpsink);
    gst_bin_remove (GST_BIN (trans->priv->gst_sink), udpport->udpsink);
    ret = gst_element_set_state (udpport->udpsink, GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS) {
      g_warning ("Error changing state of udpsink: %d", ret);
    }
    gst_object_unref (udpport->udpsink);
  }

  if (udpport->udpsink_requested_pad) {
    gst_element_release_request_pad (trans->priv->udpsink_tee,
      udpport->udpsink_requested_pad);
  }

  if (udpport->fd >= 0)
    close (udpport->fd);

  g_free (udpport->requested_ip);
  g_free (udpport);
}

void
fs_rawudp_transmitter_udpport_add_dest (UdpPort *udpport,
  const gchar *ip, gint port)
{
  g_signal_emit_by_name (udpport->udpsink, "add", 0,
    ip, port, NULL);
}


void
fs_rawudp_transmitter_udpport_remove_dest (UdpPort *udpport,
  const gchar *ip, gint port)
{
  g_signal_emit_by_name (udpport->udpsink, "remove", 0,
    ip, port, NULL);
}

gboolean
fs_rawudp_transmitter_udpport_sendto (UdpPort *udpport,
  gchar *msg, size_t len, const struct sockaddr *to, socklen_t tolen,
  GError **error)
{
  if (sendto (udpport->fd, msg, len, 0, to, tolen) != len) {
    g_set_error (error, FS_STREAM_ERROR, FS_STREAM_ERROR_NETWORK,
      "Could not send STUN request: %s", g_strerror (errno));
    return FALSE;
  }

  return TRUE;
}

gulong
fs_rawudp_transmitter_udpport_connect_recv (UdpPort *udpport,
  GCallback callback, gpointer user_data)
{
  GstPad *pad;
  gulong id;

  pad = gst_element_get_static_pad (udpport->udpsrc, "src");

  id = gst_pad_add_buffer_probe (pad, callback, user_data);

  gst_object_unref (pad);

  return id;
}


void
fs_rawudp_transmitter_udpport_disconnect_recv (UdpPort *udpport, gulong id)
{
  GstPad *pad = gst_element_get_static_pad (udpport->udpsrc, "src");

  gst_pad_remove_buffer_probe (pad, id);

  gst_object_unref (pad);
}

gboolean
fs_rawudp_transmitter_udpport_is_pad (UdpPort *udpport, GstPad *pad)
{
  GstPad *mypad;
  gboolean res;

  mypad =  gst_element_get_static_pad (udpport->udpsrc, "src");

  res = (mypad == pad);

  if (mypad)
    gst_object_unref (mypad);

  return res;
}


gboolean
fs_rawudp_transmitter_udpport_get_port (UdpPort *udpport)
{
  return udpport->port;
}

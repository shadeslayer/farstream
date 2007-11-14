/*
 * Farsight2 - Farsight RAW UDP with STUN Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rawudp-transmitter.c - A Farsight UDP transmitter with STUN
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
 * SECTION:fs-stream-transmitter
 * @short_description: A stream transmitter object for UDP with STUN
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rawudp-stream-transmitter.h"

#include <gst/farsight/fs-stream.h>

#include <gst/gst.h>

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_SENDING,
  PROP_PREFERED_LOCAL_CANDIDATES,
  PROP_STUN_IP,
  PROP_STUN_PORT
};

struct _FsRawUdpStreamTransmitterPrivate
{
  gboolean disposed;

  /* We don't actually hold a ref to this,
   * But since our parent FsStream can not exist without its parent
   * FsSession, we should be safe
   */
  FsRawUdpTransmitter *transmitter;

  gboolean sending;

  FsCandidate *remote_rtp_candidate;
  FsCandidate *remote_rtcp_candidate;

  UdpStream *udpstream;

  gchar *stun_ip;
  guint stun_port;

  GList *prefered_local_candidates;
};

#define FS_RAWUDP_STREAM_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAWUDP_STREAM_TRANSMITTER, \
                                FsRawUdpStreamTransmitterPrivate))

static void fs_rawudp_stream_transmitter_class_init (FsRawUdpStreamTransmitterClass *klass);
static void fs_rawudp_stream_transmitter_init (FsRawUdpStreamTransmitter *self);
static void fs_rawudp_stream_transmitter_dispose (GObject *object);
static void fs_rawudp_stream_transmitter_finalize (GObject *object);

static void fs_rawudp_stream_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_rawudp_stream_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static gboolean fs_rawudp_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error);


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_rawudp_stream_transmitter_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsRawUdpStreamTransmitterClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_rawudp_stream_transmitter_class_init,
      NULL,
      NULL,
      sizeof (FsRawUdpStreamTransmitter),
      0,
      (GInstanceInitFunc) fs_rawudp_stream_transmitter_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsRawUdpStreamTransmitter", &info, G_TYPE_FLAG_ABSTRACT);
  }

  return type;
}

static void
fs_rawudp_stream_transmitter_class_init (FsRawUdpStreamTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsStreamTransmitterClass *streamtransmitterclass =
    FS_STREAM_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_rawudp_stream_transmitter_set_property;
  gobject_class->get_property = fs_rawudp_stream_transmitter_get_property;

  streamtransmitterclass->add_remote_candidate =
    fs_rawudp_stream_transmitter_add_remote_candidate;

  g_object_class_override_property (gobject_class, PROP_SENDING, "sending");
  g_object_class_override_property (gobject_class,
    PROP_PREFERED_LOCAL_CANDIDATES, "prefered-local-candidates");

  g_object_class_install_property (gobject_class,
    PROP_STUN_IP,
    g_param_spec_string ("stun-ip",
      "The IP address of the STUN server",
      "The IPv4 address of the STUN server as a x.x.x.x string",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
    PROP_STUN_IP,
    g_param_spec_uint ("stun-port",
      "The port of the STUN server",
      "The IPv4 UDP port of the STUN server as a ",
      1, 65535, 3478,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  gobject_class->dispose = fs_rawudp_stream_transmitter_dispose;
  gobject_class->finalize = fs_rawudp_stream_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsRawUdpStreamTransmitterPrivate));
}

static void
fs_rawudp_stream_transmitter_init (FsRawUdpStreamTransmitter *self)
{
  /* member init */
  self->priv = FS_RAWUDP_STREAM_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->priv->sending = TRUE;
}

static void
fs_rawudp_stream_transmitter_dispose (GObject *object)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rawudp_stream_transmitter_finalize (GObject *object)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (object);

  if (self->priv->stun_ip) {
    g_free (self->priv->stun_ip);
    self->priv->stun_ip = NULL;
  }

  if (self->priv->prefered_local_candidates) {
    fs_candidate_list_destroy (self->priv->prefered_local_candidates);
    self->priv->prefered_local_candidates = NULL;
  }

  if (self->priv->remote_rtp_candidate) {
    if (self->priv->sending) {
      fs_rawudp_transmitter_udpstream_remove_dest (self->priv->udpstream,
        self->priv->remote_rtp_candidate->ip,
        self->priv->remote_rtp_candidate->port,
        FALSE);
    }
    fs_candidate_destroy (self->priv->remote_rtp_candidate);
    self->priv->remote_rtp_candidate = NULL;
  }

  if (self->priv->remote_rtcp_candidate) {
    if (self->priv->sending) {
      fs_rawudp_transmitter_udpstream_remove_dest (self->priv->udpstream,
        self->priv->remote_rtcp_candidate->ip,
        self->priv->remote_rtcp_candidate->port,
        TRUE);
    }
    fs_candidate_destroy (self->priv->remote_rtcp_candidate);
    self->priv->remote_rtcp_candidate = NULL;
  }

  if (self->priv->udpstream) {
    fs_rawudp_transmitter_put_udpstream (self->priv->transmitter,
      self->priv->udpstream);
    self->priv->udpstream = NULL;
  }

  parent_class->finalize (object);
}

static void
fs_rawudp_stream_transmitter_get_property (GObject *object,
                                           guint prop_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_SENDING:
      g_value_set_boolean (value, self->priv->sending);
      break;
    case PROP_PREFERED_LOCAL_CANDIDATES:
      g_value_set_boxed (value, self->priv->prefered_local_candidates);
      break;
    case PROP_STUN_IP:
      g_value_set_string (value, self->priv->stun_ip);
      break;
    case PROP_STUN_PORT:
      g_value_set_uint (value, self->priv->stun_port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rawudp_stream_transmitter_set_property (GObject *object,
                                           guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_SENDING:
      {
        gboolean old_sending = self->priv->sending;
        self->priv->sending = g_value_get_boolean (value);

        if (self->priv->sending != old_sending) {
          if (self->priv->sending) {
            if (self->priv->remote_rtp_candidate)
              fs_rawudp_transmitter_udpstream_add_dest (
                  self->priv->udpstream,
                self->priv->remote_rtp_candidate->ip,
                self->priv->remote_rtp_candidate->port,
                FALSE);
            if (self->priv->remote_rtcp_candidate)
              fs_rawudp_transmitter_udpstream_add_dest (
                  self->priv->udpstream,
                self->priv->remote_rtcp_candidate->ip,
                self->priv->remote_rtcp_candidate->port,
                FALSE);
          } else {
            if (self->priv->remote_rtp_candidate)
              fs_rawudp_transmitter_udpstream_remove_dest (
                  self->priv->udpstream,
                self->priv->remote_rtp_candidate->ip,
                self->priv->remote_rtp_candidate->port,
                FALSE);
            if (self->priv->remote_rtcp_candidate)
              fs_rawudp_transmitter_udpstream_remove_dest (
                  self->priv->udpstream,
                  self->priv->remote_rtcp_candidate->ip,
                  self->priv->remote_rtcp_candidate->port,
                  FALSE);
          }
        }
      }
      break;
    case PROP_PREFERED_LOCAL_CANDIDATES:
      self->priv->prefered_local_candidates = g_value_dup_boxed (value);
      break;
    case PROP_STUN_IP:
      self->priv->stun_ip = g_value_dup_string (value);
      break;
    case PROP_STUN_PORT:
      self->priv->stun_port = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
fs_rawudp_stream_transmitter_build (FsRawUdpStreamTransmitter *self,
  GError **error)
{
  const gchar *ip = NULL, *rtcp_ip = NULL;
  guint port = 0, rtcp_port = 0;
  GList *item;

  for (item = g_list_first (self->priv->prefered_local_candidates);
       item;
       item = g_list_next (item)) {
    FsCandidate *candidate = item->data;

    if (candidate->proto != FS_NETWORK_PROTOCOL_UDP) {
      g_set_error (error, FS_STREAM_ERROR, FS_STREAM_ERROR_INVALID_ARGUMENTS,
        "You set prefered candidate of a type %d that is not"
        " FS_NETWORK_PROTOCOL_UDP",
        candidate->proto);
      return FALSE;
    }

    switch (candidate->component_id) {
      case 1:  /* RTP */
        if (ip) {
          g_set_error (error, FS_STREAM_ERROR,
            FS_STREAM_ERROR_INVALID_ARGUMENTS,
            "You set more than one candidate for component 1");
          return FALSE;
        }
        ip = candidate->ip;
        port = candidate->port;
        break;
      case 2:  /* RTCP */
        if (rtcp_ip) {
          g_set_error (error, FS_STREAM_ERROR,
            FS_STREAM_ERROR_INVALID_ARGUMENTS,
            "You set more than one candidate for component 2");
          return FALSE;
        }
        rtcp_ip = candidate->ip;
        rtcp_port = candidate->port;
        break;
      default:
        g_set_error (error, FS_STREAM_ERROR, FS_STREAM_ERROR_INVALID_ARGUMENTS,
          "Only components 1 and 2 are supported, %d isnt",
          candidate->component_id);
        return FALSE;
    }
  }


  self->priv->udpstream =
    fs_rawudp_transmitter_get_udpstream (self->priv->transmitter, ip, port,
      rtcp_ip, rtcp_port, error);
  if (!self->priv->udpstream)
    return FALSE;

  return TRUE;
}

/**
 * fs_rawudp_stream_transmitter_add_remote_candidate
 * @streamtransmitter: a #FsStreamTransmitter
 * @candidate: a remote #FsCandidate to add
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function is used to add remote candidates to the transmitter
 *
 * Returns: TRUE of the candidate could be added, FALSE if it couldnt
 *   (and the #GError will be set)
 */

static gboolean
fs_rawudp_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error)
{
  FsRawUdpStreamTransmitter *self =
    FS_RAWUDP_STREAM_TRANSMITTER (streamtransmitter);

  if (candidate->proto != FS_NETWORK_PROTOCOL_UDP) {
    g_set_error (error, FS_STREAM_ERROR, FS_STREAM_ERROR_INVALID_ARGUMENTS,
      "You set a candidate of a type %d that is not  FS_NETWORK_PROTOCOL_UDP",
      candidate->proto);
    return FALSE;
  }

  if (!candidate->ip || !candidate->port) {
    g_set_error (error, FS_STREAM_ERROR, FS_STREAM_ERROR_INVALID_ARGUMENTS,
      "The candidate passed does not contain a valid ip or port");
    return FALSE;
  }

  /*
   * IMPROVE ME: We should probably check that the candidate's IP
   *  has the format x.x.x.x where x is [0,255] using GRegex, etc
   */

  switch (candidate->component_id) {
    case 1:  /* RTP */
      if (self->priv->sending) {
        fs_rawudp_transmitter_udpstream_add_dest (self->priv->udpstream,
          candidate->ip, candidate->port, FALSE);
      }
      if (self->priv->remote_rtp_candidate) {
        fs_rawudp_transmitter_udpstream_remove_dest (self->priv->udpstream,
            self->priv->remote_rtp_candidate->ip,
            self->priv->remote_rtp_candidate->port,
            FALSE);
        fs_candidate_destroy (self->priv->remote_rtp_candidate);
      }
      self->priv->remote_rtp_candidate = fs_candidate_copy (candidate);
      break;

    case 2:  /* RTCP */
      if (self->priv->sending) {
        fs_rawudp_transmitter_udpstream_add_dest (self->priv->udpstream,
          candidate->ip, candidate->port, TRUE);
      }
      if (self->priv->remote_rtcp_candidate) {
        fs_rawudp_transmitter_udpstream_remove_dest (self->priv->udpstream,
            self->priv->remote_rtcp_candidate->ip,
            self->priv->remote_rtcp_candidate->port,
            TRUE);
        fs_candidate_destroy (self->priv->remote_rtcp_candidate);
      }
      self->priv->remote_rtcp_candidate = fs_candidate_copy (candidate);
      break;

    default:
      g_set_error (error, FS_STREAM_ERROR, FS_STREAM_ERROR_INVALID_ARGUMENTS,
        "Only components 1 and 2 are supported, %d isn't",
        candidate->component_id);
      return FALSE;
  }

  return TRUE;
}


FsRawUdpStreamTransmitter *
  fs_rawudp_stream_transmitter_newv (FsRawUdpTransmitter *transmitter,
    guint n_parameters, GParameter *parameters,
    GError **error)
{
  FsRawUdpStreamTransmitter *streamtransmitter = NULL;

  error = NULL;

  streamtransmitter = g_object_newv (FS_TYPE_RAWUDP_STREAM_TRANSMITTER,
    n_parameters, parameters);

  streamtransmitter->priv->transmitter = transmitter;

  if (!fs_rawudp_stream_transmitter_build (streamtransmitter, error)) {
    g_object_unref (streamtransmitter);
    return NULL;
  }

  return streamtransmitter;
}

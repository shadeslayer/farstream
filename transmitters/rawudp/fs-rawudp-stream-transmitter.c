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

#include "stun.h"
#include "fs-interfaces.h"

#include <gst/farsight/fs-candidate.h>
#include <gst/farsight/fs-conference-iface.h>

#include <gst/gst.h>

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
  PROP_SENDING,
  PROP_PREFERED_LOCAL_CANDIDATES,
  PROP_STUN_IP,
  PROP_STUN_PORT,
  PROP_STUN_TIMEOUT
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

  FsCandidate *local_forced_rtp_candidate;
  FsCandidate *local_forced_rtcp_candidate
;
  FsCandidate *local_stun_rtp_candidate;
  FsCandidate *local_stun_rtcp_candidate;

  FsCandidate *local_active_rtp_candidate;
  FsCandidate *local_active_rtcp_candidate;

  UdpPort *rtp_udpport;
  UdpPort *rtcp_udpport;

  gchar *stun_ip;
  guint stun_port;
  guint stun_timeout;

  /* These are protected by the sources_mutex too */
  gulong stun_rtp_recv_id;
  gulong stun_rtcp_recv_id;
  guint  stun_rtp_timeout_id;
  guint  stun_rtcp_timeout_id;

  gchar stun_cookie[16];

  GList *prefered_local_candidates;

  guint next_candidate_id;

  GMutex *sources_mutex;
  GList *sources;
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

static gboolean fs_rawudp_stream_transmitter_start_stun (
    FsRawUdpStreamTransmitter *self, guint component_id, GError **error);
static void fs_rawudp_stream_transmitter_stop_stun (
    FsRawUdpStreamTransmitter *self, guint component_id);

static FsCandidate * fs_rawudp_stream_transmitter_build_forced_candidate (
    FsRawUdpStreamTransmitter *self, const char *ip, gint port,
    guint component_id);
static gboolean fs_rawudp_stream_transmitter_finish_candidate_generation (
    gpointer user_data);
static void fs_rawudp_stream_transmitter_maybe_new_active_candidate_pair (
    FsRawUdpStreamTransmitter *self, guint component_id);


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

static GType type = 0;

GType
fs_rawudp_stream_transmitter_get_type (void)
{
  return type;
}

GType
fs_rawudp_stream_transmitter_register_type (FsPlugin *module)
{
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

  type = g_type_module_register_type (G_TYPE_MODULE (module),
    FS_TYPE_STREAM_TRANSMITTER, "FsRawUdpStreamTransmitter", &info, 0);

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
    PROP_STUN_PORT,
    g_param_spec_uint ("stun-port",
      "The port of the STUN server",
      "The IPv4 UDP port of the STUN server as a ",
      1, 65535, 3478,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
    PROP_STUN_TIMEOUT,
    g_param_spec_uint ("stun-timeout",
      "The timeout for the STUN reply",
      "How long to wait for for the STUN reply (in seconds) before giving up",
      1, G_MAXUINT, 30,
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

  /* Use a pseudo-random number for the STUN cookie */
  ((guint32*)self->priv->stun_cookie)[0] = g_random_int ();
  ((guint32*)self->priv->stun_cookie)[1] = g_random_int ();
  ((guint32*)self->priv->stun_cookie)[2] = g_random_int ();
  ((guint32*)self->priv->stun_cookie)[3] = g_random_int ();

  self->priv->sources_mutex = g_mutex_new ();
}

static void
fs_rawudp_stream_transmitter_dispose (GObject *object)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  g_mutex_lock (self->priv->sources_mutex);
  if (self->priv->stun_rtp_recv_id)
    fs_rawudp_stream_transmitter_stop_stun (self, FS_COMPONENT_RTP);
  if (self->priv->stun_rtp_recv_id)
    fs_rawudp_stream_transmitter_stop_stun (self, FS_COMPONENT_RTCP);


  if (self->priv->sources) {
    g_list_foreach (self->priv->sources, (GFunc) g_source_remove, NULL);
    g_list_free (self->priv->sources);
    self->priv->sources = NULL;
  }
  g_mutex_unlock (self->priv->sources_mutex);


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
    if (self->priv->sending)
      fs_rawudp_transmitter_udpport_remove_dest (self->priv->rtp_udpport,
        self->priv->remote_rtp_candidate->ip,
        self->priv->remote_rtp_candidate->port);
    fs_candidate_destroy (self->priv->remote_rtp_candidate);
    self->priv->remote_rtp_candidate = NULL;
  }

  if (self->priv->remote_rtcp_candidate) {
    if (self->priv->sending) {
      fs_rawudp_transmitter_udpport_remove_dest (self->priv->rtcp_udpport,
        self->priv->remote_rtcp_candidate->ip,
        self->priv->remote_rtcp_candidate->port);
    }
    fs_candidate_destroy (self->priv->remote_rtcp_candidate);
    self->priv->remote_rtcp_candidate = NULL;
  }

  if (self->priv->rtp_udpport) {
    fs_rawudp_transmitter_put_udpport (self->priv->transmitter,
      self->priv->rtp_udpport);
    self->priv->rtp_udpport = NULL;
  }

  if (self->priv->rtcp_udpport) {
    fs_rawudp_transmitter_put_udpport (self->priv->transmitter,
      self->priv->rtcp_udpport);
    self->priv->rtcp_udpport = NULL;
  }

  if (self->priv->local_forced_rtp_candidate) {
    fs_candidate_destroy (self->priv->local_forced_rtp_candidate);
    self->priv->local_forced_rtp_candidate = NULL;
  }

  if (self->priv->local_forced_rtcp_candidate) {
    fs_candidate_destroy (self->priv->local_forced_rtcp_candidate);
    self->priv->local_forced_rtcp_candidate = NULL;
  }

  if (self->priv->local_stun_rtp_candidate) {
    fs_candidate_destroy (self->priv->local_stun_rtp_candidate);
    self->priv->local_stun_rtp_candidate = NULL;
  }

  if (self->priv->local_stun_rtcp_candidate) {
    fs_candidate_destroy (self->priv->local_stun_rtcp_candidate);
    self->priv->local_stun_rtcp_candidate = NULL;
  }

  if (self->priv->local_active_rtp_candidate) {
    fs_candidate_destroy (self->priv->local_active_rtp_candidate);
    self->priv->local_active_rtp_candidate = NULL;
  }

  if (self->priv->local_active_rtcp_candidate) {
    fs_candidate_destroy (self->priv->local_active_rtcp_candidate);
    self->priv->local_active_rtcp_candidate = NULL;
  }

  if (self->priv->sources_mutex) {
    g_mutex_free (self->priv->sources_mutex);
    self->priv->sources_mutex = NULL;
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
    case PROP_STUN_TIMEOUT:
      g_value_set_uint (value, self->priv->stun_timeout);
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
              fs_rawudp_transmitter_udpport_add_dest (
                  self->priv->rtp_udpport,
                  self->priv->remote_rtp_candidate->ip,
                  self->priv->remote_rtp_candidate->port);

            if (self->priv->remote_rtcp_candidate)
              fs_rawudp_transmitter_udpport_add_dest (
                  self->priv->rtcp_udpport,
                  self->priv->remote_rtcp_candidate->ip,
                  self->priv->remote_rtcp_candidate->port);

          } else {

            if (self->priv->remote_rtp_candidate)
              fs_rawudp_transmitter_udpport_remove_dest (
                  self->priv->rtp_udpport,
                self->priv->remote_rtp_candidate->ip,
                self->priv->remote_rtp_candidate->port);

            if (self->priv->remote_rtcp_candidate)
              fs_rawudp_transmitter_udpport_remove_dest (
                  self->priv->rtcp_udpport,
                  self->priv->remote_rtcp_candidate->ip,
                  self->priv->remote_rtcp_candidate->port);
          }
        }
      }
      break;
    case PROP_PREFERED_LOCAL_CANDIDATES:
      self->priv->prefered_local_candidates = g_value_dup_boxed (value);
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
  guint port = 7078, rtcp_port = 0;
  GList *item;

  for (item = g_list_first (self->priv->prefered_local_candidates);
       item;
       item = g_list_next (item)) {
    FsCandidate *candidate = item->data;

    if (candidate->proto != FS_NETWORK_PROTOCOL_UDP) {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "You set prefered candidate of a type %d that is not"
        " FS_NETWORK_PROTOCOL_UDP",
        candidate->proto);
      return FALSE;
    }

    switch (candidate->component_id) {
      case FS_COMPONENT_RTP:  /* RTP */
        if (ip) {
          g_set_error (error, FS_ERROR,
            FS_ERROR_INVALID_ARGUMENTS,
            "You set more than one candidate for component 1");
          return FALSE;
        }
        ip = candidate->ip;
        port = candidate->port;
        break;
      case FS_COMPONENT_RTCP:  /* RTCP */
        if (rtcp_ip) {
          g_set_error (error, FS_ERROR,
            FS_ERROR_INVALID_ARGUMENTS,
            "You set more than one candidate for component 2");
          return FALSE;
        }
        rtcp_ip = candidate->ip;
        rtcp_port = candidate->port;
        break;
      default:
        g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "Only components 1 and 2 are supported, %d isnt",
          candidate->component_id);
        return FALSE;
    }
  }

  self->priv->rtp_udpport =
    fs_rawudp_transmitter_get_udpport (self->priv->transmitter,
      FS_COMPONENT_RTP, ip, port, error);
  if (!self->priv->rtp_udpport)
    return FALSE;

  if (!rtcp_port)
    rtcp_port = fs_rawudp_transmitter_udpport_get_port (
        self->priv->rtp_udpport) + 1;

  self->priv->rtcp_udpport =
    fs_rawudp_transmitter_get_udpport (self->priv->transmitter,
      FS_COMPONENT_RTCP, rtcp_ip, rtcp_port, error);
  if (!self->priv->rtcp_udpport)
    return FALSE;

  if (ip) {
    self->priv->local_forced_rtp_candidate =
      fs_rawudp_stream_transmitter_build_forced_candidate (self, ip,
        fs_rawudp_transmitter_udpport_get_port (self->priv->rtp_udpport),
        FS_COMPONENT_RTP);
  }

  if (rtcp_ip) {
    self->priv->local_forced_rtcp_candidate =
      fs_rawudp_stream_transmitter_build_forced_candidate (self, rtcp_ip,
        fs_rawudp_transmitter_udpport_get_port (self->priv->rtcp_udpport),
        FS_COMPONENT_RTCP);
  }


  if (self->priv->stun_ip && self->priv->stun_port) {
    if (!fs_rawudp_stream_transmitter_start_stun (self, FS_COMPONENT_RTP,
        error))
      return FALSE;
    if (!fs_rawudp_stream_transmitter_start_stun (self, FS_COMPONENT_RTCP,
        error))
      return FALSE;
  } else {
    guint id;
    id = g_idle_add (fs_rawudp_stream_transmitter_finish_candidate_generation,
      self);
    g_mutex_lock (self->priv->sources_mutex);
    self->priv->sources = g_list_prepend (self->priv->sources,
      GUINT_TO_POINTER(id));
    g_mutex_unlock (self->priv->sources_mutex);
  }

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
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "You set a candidate of a type %d that is not  FS_NETWORK_PROTOCOL_UDP",
      candidate->proto);
    return FALSE;
  }

  if (!candidate->ip || !candidate->port) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "The candidate passed does not contain a valid ip or port");
    return FALSE;
  }

  /*
   * IMPROVE ME: We should probably check that the candidate's IP
   *  has the format x.x.x.x where x is [0,255] using GRegex, etc
   */

  switch (candidate->component_id) {
    case FS_COMPONENT_RTP:
      if (self->priv->sending) {
        fs_rawudp_transmitter_udpport_add_dest (self->priv->rtp_udpport,
          candidate->ip, candidate->port);
      }
      if (self->priv->remote_rtp_candidate) {
        fs_rawudp_transmitter_udpport_remove_dest (self->priv->rtp_udpport,
          self->priv->remote_rtp_candidate->ip,
          self->priv->remote_rtp_candidate->port);
        fs_candidate_destroy (self->priv->remote_rtp_candidate);
      }
      self->priv->remote_rtp_candidate = fs_candidate_copy (candidate);
      break;

    case FS_COMPONENT_RTCP:
      if (self->priv->sending) {
        fs_rawudp_transmitter_udpport_add_dest (self->priv->rtcp_udpport,
          candidate->ip, candidate->port);
      }
      if (self->priv->remote_rtcp_candidate) {
        fs_rawudp_transmitter_udpport_remove_dest (self->priv->rtcp_udpport,
          self->priv->remote_rtcp_candidate->ip,
          self->priv->remote_rtcp_candidate->port);
        fs_candidate_destroy (self->priv->remote_rtcp_candidate);
      }
      self->priv->remote_rtcp_candidate = fs_candidate_copy (candidate);
      break;

    default:
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Only components 1 and 2 are supported, %d isn't",
        candidate->component_id);
      return FALSE;
  }

  fs_rawudp_stream_transmitter_maybe_new_active_candidate_pair (self,
    candidate->component_id);

  return TRUE;
}


FsRawUdpStreamTransmitter *
fs_rawudp_stream_transmitter_newv (FsRawUdpTransmitter *transmitter,
  guint n_parameters, GParameter *parameters, GError **error)
{
  FsRawUdpStreamTransmitter *streamtransmitter = NULL;

  streamtransmitter = g_object_newv (FS_TYPE_RAWUDP_STREAM_TRANSMITTER,
    n_parameters, parameters);

  if (!streamtransmitter) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not build the stream transmitter");
    return NULL;
  }

  streamtransmitter->priv->transmitter = transmitter;

  if (!fs_rawudp_stream_transmitter_build (streamtransmitter, error)) {
    g_object_unref (streamtransmitter);
    return NULL;
  }

  return streamtransmitter;
}

struct CandidateTransit {
  FsRawUdpStreamTransmitter *self;
  FsCandidate *candidate;
  guint component_id;
};

static gboolean
fs_rawudp_stream_transmitter_emit_stun_candidate (gpointer user_data)
{
  struct CandidateTransit *data = user_data;

  if (data->component_id == FS_COMPONENT_RTP) {
    data->self->priv->local_stun_rtp_candidate = data->candidate;
    data->self->priv->local_active_rtp_candidate =
      fs_candidate_copy (data->candidate);
  } else if (data->component_id == FS_COMPONENT_RTCP) {
    data->self->priv->local_stun_rtcp_candidate = data->candidate;
    data->self->priv->local_active_rtcp_candidate =
      fs_candidate_copy (data->candidate);
  }

  g_signal_emit_by_name (data->self, "new-local-candidate", data->candidate);

  /* Lets call it over if its over */
  g_mutex_lock (data->self->priv->sources_mutex);
  if (data->self->priv->stun_rtp_recv_id == 0 &&
    data->self->priv->stun_rtcp_recv_id == 0 ) {
    g_mutex_unlock (data->self->priv->sources_mutex);
    fs_rawudp_stream_transmitter_finish_candidate_generation (data->self);
  } else {
    /* Lets remove this source from the list of sources to destroy */
    GSource * source = g_main_current_source ();

    if (source)  {
      guint id = g_source_get_id (source);

      data->self->priv->sources =
        g_list_remove (data->self->priv->sources, GUINT_TO_POINTER (id));
    }
    g_mutex_unlock (data->self->priv->sources_mutex);
  }

  fs_rawudp_stream_transmitter_maybe_new_active_candidate_pair (data->self,
    data->component_id);

  g_free (data);

  /* We only pass one candidate at a time */
  return FALSE;
}

/*
 * We have to be extremely careful in this callback, it is executed
 * on the streaming thread, so all data must either be object-constant
 * (ie doesnt change for the life of the object) or locked by a mutex
 *
 */

static gboolean
fs_rawudp_stream_transmitter_stun_recv_cb (GstPad *pad, GstBuffer *buffer,
  gpointer user_data)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (user_data);
  guint component_id;
  FsCandidate *candidate = NULL;
  StunMessage *msg;
  StunAttribute **attr;


  if (GST_BUFFER_SIZE (buffer) < 4) {
    /* Packet is too small to be STUN */
    return TRUE;
  }

  if (GST_BUFFER_DATA (buffer)[0] >> 6) {
    /* Non stun packet */
    return TRUE;
  }

  if (fs_rawudp_transmitter_udpport_is_pad (self->priv->rtp_udpport, pad))
    component_id = FS_COMPONENT_RTP;
  else if (fs_rawudp_transmitter_udpport_is_pad (
        self->priv->rtcp_udpport, pad))
    component_id = FS_COMPONENT_RTCP;
  else
    return FALSE;

  msg = stun_message_unpack (GST_BUFFER_SIZE (buffer),
    (const gchar *) GST_BUFFER_DATA (buffer));
  if (!msg) {
    /* invalid message */
    return TRUE;
  }

  if (memcmp (msg->transaction_id, self->priv->stun_cookie, 16) != 0) {
    /* not ours */
    stun_message_free (msg);
    return TRUE;
  }

  if (msg->type == STUN_MESSAGE_BINDING_ERROR_RESPONSE) {
    fs_stream_transmitter_emit_error (FS_STREAM_TRANSMITTER (self),
      FS_ERROR_NETWORK, "Got an error message from the STUN server",
      "The STUN process produced an error");
    stun_message_free (msg);
    // fs_rawudp_stream_transmitter_stop_stun (self, component_id);
    /* Lets not stop the STUN now and wait for the timeout
     * in case the server answers with the right reply
     */
    return FALSE;
  }

  if (msg->type != STUN_MESSAGE_BINDING_RESPONSE) {
    stun_message_free (msg);
    return TRUE;
  }


  for (attr = msg->attributes; *attr; attr++) {
    if ((*attr)->type == STUN_ATTRIBUTE_MAPPED_ADDRESS) {

      candidate = g_new0 (FsCandidate,1);
      candidate->candidate_id = g_strdup_printf ("L%u",
        self->priv->next_candidate_id);
      candidate->component_id = component_id;
      candidate->ip = g_strdup_printf ("%u.%u.%u.%u",
          ((*attr)->address.ip & 0xff000000) >> 24,
          ((*attr)->address.ip & 0x00ff0000) >> 16,
          ((*attr)->address.ip & 0x0000ff00) >>  8,
          ((*attr)->address.ip & 0x000000ff));
      candidate->port = (*attr)->address.port;
      candidate->proto = FS_NETWORK_PROTOCOL_UDP;
      if (component_id == FS_COMPONENT_RTP)
        candidate->proto_subtype = g_strdup ("RTP");
      else if (component_id == FS_COMPONENT_RTCP)
        candidate->proto_subtype = g_strdup ("RTCP");
      candidate->proto_profile = g_strdup ("AVP");
      candidate->type = FS_CANDIDATE_TYPE_SRFLX;

      g_debug ("Stun server says we are %u.%u.%u.%u %u\n",
          ((*attr)->address.ip & 0xff000000) >> 24,
          ((*attr)->address.ip & 0x00ff0000) >> 16,
          ((*attr)->address.ip & 0x0000ff00) >>  8,
          ((*attr)->address.ip & 0x000000ff),(*attr)->address.port);
      break;
    }
  }

  g_mutex_lock (self->priv->sources_mutex);
  fs_rawudp_stream_transmitter_stop_stun (self, component_id);
  g_mutex_unlock (self->priv->sources_mutex);

  if (candidate) {
    guint id;
    struct CandidateTransit *data = g_new0 (struct CandidateTransit, 1);
    data->self = self;
    data->candidate = candidate;
    data->component_id = component_id;
    id = g_idle_add (fs_rawudp_stream_transmitter_emit_stun_candidate, data);
    g_mutex_lock (self->priv->sources_mutex);
    self->priv->sources = g_list_prepend (self->priv->sources,
      GUINT_TO_POINTER(id));
    g_mutex_unlock (self->priv->sources_mutex);
  }

  /* It was a stun packet, lets drop it */
    stun_message_free (msg);
  return FALSE;
}

static gboolean
fs_rawudp_stream_transmitter_stun_timeout_cb (gpointer user_data)
{
  struct CandidateTransit *data = user_data;

  g_mutex_lock (data->self->priv->sources_mutex);

  fs_rawudp_stream_transmitter_stop_stun (data->self, data->component_id);

  if (data->self->priv->stun_rtp_recv_id == 0 &&
    data->self->priv->stun_rtcp_recv_id == 0 ) {
    g_mutex_unlock (data->self->priv->sources_mutex);
    fs_rawudp_stream_transmitter_finish_candidate_generation (data->self);
  } else {
    g_mutex_unlock (data->self->priv->sources_mutex);
  }

  return FALSE;
}

static gboolean
fs_rawudp_stream_transmitter_start_stun (FsRawUdpStreamTransmitter *self,
  guint component_id, GError **error)
{
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  struct sockaddr_in address;
  gchar *packed;
  guint length;
  int retval;
  StunMessage *msg;
  gboolean ret = TRUE;
  UdpPort *udpport;
  struct CandidateTransit *data;

  if (component_id == FS_COMPONENT_RTP)
    udpport = self->priv->rtp_udpport;
  else if (component_id == FS_COMPONENT_RTCP)
    udpport = self->priv->rtcp_udpport;
  else {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "Only components RTP(1) and RTCP(2) are support, %u isnt",
      component_id);
    return FALSE;
  }

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_flags = AI_NUMERICHOST;
  retval = getaddrinfo (self->priv->stun_ip, NULL, &hints, &result);
  if (retval != 0) {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
      "Invalid IP address %s passed for STUN: %s",
      self->priv->stun_ip, gai_strerror (retval));
    return FALSE;
  }
  memcpy (&address, result->ai_addr, sizeof(struct sockaddr_in));
  freeaddrinfo (result);

  address.sin_family = AF_INET;
  address.sin_port = htons (self->priv->stun_port);

  g_mutex_lock (self->priv->sources_mutex);
  if (component_id == FS_COMPONENT_RTP)
    self->priv->stun_rtp_recv_id =
      fs_rawudp_transmitter_udpport_connect_recv (udpport,
        G_CALLBACK (fs_rawudp_stream_transmitter_stun_recv_cb), self);
  else if (component_id == FS_COMPONENT_RTCP)
    self->priv->stun_rtcp_recv_id =
      fs_rawudp_transmitter_udpport_connect_recv (udpport,
        G_CALLBACK (fs_rawudp_stream_transmitter_stun_recv_cb), self);
  g_mutex_unlock (self->priv->sources_mutex);

  msg = stun_message_new (STUN_MESSAGE_BINDING_REQUEST,
    self->priv->stun_cookie, 0);
  if (!msg) {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
      "Could not create a new STUN binding request");
    return FALSE;
  }

  length = stun_message_pack (msg, &packed);

  if (!fs_rawudp_transmitter_udpport_sendto (udpport,
      packed, length, (const struct sockaddr *)&address, sizeof(address),
      error)) {
    ret = FALSE;
  }

  g_free (packed);
  stun_message_free (msg);

  data = g_new0 (struct CandidateTransit, 1);
  data->self = self;
  data->component_id = component_id;

  g_mutex_lock (data->self->priv->sources_mutex);
  if (component_id == FS_COMPONENT_RTP)
    self->priv->stun_rtp_timeout_id = g_timeout_add_seconds_full (
        G_PRIORITY_DEFAULT,self->priv->stun_timeout,
        fs_rawudp_stream_transmitter_stun_timeout_cb, data, g_free);
  else if (component_id == FS_COMPONENT_RTCP)
    self->priv->stun_rtcp_timeout_id = g_timeout_add_seconds_full (
        G_PRIORITY_DEFAULT, self->priv->stun_timeout,
        fs_rawudp_stream_transmitter_stun_timeout_cb, data, g_free);
  g_mutex_unlock (data->self->priv->sources_mutex);

  return ret;
}

/*
 * This function MUST be called with the sources_mutex held
 */

static void
fs_rawudp_stream_transmitter_stop_stun (FsRawUdpStreamTransmitter *self,
  guint component_id)
{
  if (component_id == FS_COMPONENT_RTP) {
    if (self->priv->stun_rtp_recv_id) {
      fs_rawudp_transmitter_udpport_disconnect_recv (self->priv->rtp_udpport,
        self->priv->stun_rtp_recv_id);
      self->priv->stun_rtp_recv_id = 0;
    }
    if (self->priv->stun_rtp_timeout_id) {
      g_source_remove (self->priv->stun_rtp_timeout_id);
      self->priv->stun_rtp_timeout_id = 0;
    }
  }
  else if (component_id == FS_COMPONENT_RTCP) {
    if (self->priv->stun_rtcp_recv_id) {
      fs_rawudp_transmitter_udpport_disconnect_recv (self->priv->rtcp_udpport,
        self->priv->stun_rtcp_recv_id);
      self->priv->stun_rtcp_recv_id = 0;
    }
    if (self->priv->stun_rtcp_timeout_id) {
      g_source_remove (self->priv->stun_rtcp_timeout_id);
      self->priv->stun_rtcp_timeout_id = 0;
    }
  }
}

static FsCandidate *
fs_rawudp_stream_transmitter_build_forced_candidate (
    FsRawUdpStreamTransmitter *self, const char *ip, gint port,
    guint component_id)
{
  FsCandidate *candidate = g_new0 (FsCandidate, 1);

  candidate = g_new0 (FsCandidate,1);
  candidate->candidate_id = g_strdup ("L1");
  candidate->component_id = component_id;
  candidate->ip = g_strdup (ip);
  candidate->port = port;
  candidate->proto = FS_NETWORK_PROTOCOL_UDP;
  if (component_id == FS_COMPONENT_RTP)
    candidate->proto_subtype = g_strdup ("RTP");
  else if (component_id == FS_COMPONENT_RTCP)
    candidate->proto_subtype = g_strdup ("RTCP");
  candidate->proto_profile = g_strdup ("AVP");
  candidate->type = FS_CANDIDATE_TYPE_HOST;

  return candidate;
}

static void
fs_rawudp_stream_transmitter_emit_local_candidates (
    FsRawUdpStreamTransmitter *self, guint component_id)
{
  GList *ips = NULL;
  GList *current;
  guint port;

  if (component_id == FS_COMPONENT_RTP)
    port = fs_rawudp_transmitter_udpport_get_port (self->priv->rtp_udpport);
  else if (component_id == FS_COMPONENT_RTCP)
    port = fs_rawudp_transmitter_udpport_get_port (self->priv->rtcp_udpport);
  else
    return;

  ips = farsight_get_local_ips(FALSE);

  for (current = g_list_first (ips);
       current;
       current = g_list_next(current)) {
    FsCandidate *candidate = g_new0 (FsCandidate, 1);

    candidate->candidate_id = g_strdup_printf ("L%u",
      self->priv->next_candidate_id++);
    candidate->component_id = component_id;
    candidate->ip = g_strdup (current->data);
    candidate->port = port;
    candidate->proto = FS_NETWORK_PROTOCOL_UDP;
    if (component_id == FS_COMPONENT_RTP)
      candidate->proto_subtype = g_strdup ("RTP");
    else if (component_id == FS_COMPONENT_RTCP)
      candidate->proto_subtype = g_strdup ("RTCP");
    candidate->proto_profile = g_strdup ("AVP");
    candidate->type = FS_CANDIDATE_TYPE_HOST;

    g_signal_emit_by_name (self, "new-local-candidate", candidate);

    if (component_id == FS_COMPONENT_RTP)
      self->priv->local_active_rtp_candidate = fs_candidate_copy (candidate);
    else if (component_id == FS_COMPONENT_RTCP)
      self->priv->local_active_rtcp_candidate = fs_candidate_copy (candidate);

    fs_candidate_destroy (candidate);
  }

  /* free list of ips */
  g_list_foreach (ips, (GFunc) g_free, NULL);
  g_list_free (ips);

  fs_rawudp_stream_transmitter_maybe_new_active_candidate_pair (self,
    component_id);
}

/*
 * This is called when the local candidates have been generated
 */

static gboolean
fs_rawudp_stream_transmitter_finish_candidate_generation (gpointer user_data)
{
  FsRawUdpStreamTransmitter *self = user_data;
  GSource *source;

  /* If we have a STUN'd rtp candidate, dont send the locally generated
   * ones */
  if (!self->priv->local_stun_rtp_candidate) {
    if (self->priv->local_forced_rtp_candidate) {
      self->priv->local_active_rtp_candidate = fs_candidate_copy (
          self->priv->local_forced_rtp_candidate);
      g_signal_emit_by_name (self, "new-local-candidate",
        self->priv->local_forced_rtp_candidate);
      fs_rawudp_stream_transmitter_maybe_new_active_candidate_pair (self,
        FS_COMPONENT_RTP);
    } else {
      fs_rawudp_stream_transmitter_emit_local_candidates (self,
        FS_COMPONENT_RTP);
    }
  }

  /* Lets do the same for rtcp */
  if (!self->priv->local_stun_rtcp_candidate) {
    if (self->priv->local_forced_rtcp_candidate) {
      self->priv->local_active_rtcp_candidate = fs_candidate_copy (
          self->priv->local_forced_rtcp_candidate);
      g_signal_emit_by_name (self, "new-local-candidate",
        self->priv->local_forced_rtcp_candidate);
      fs_rawudp_stream_transmitter_maybe_new_active_candidate_pair (self,
        FS_COMPONENT_RTCP);
    } else {
      fs_rawudp_stream_transmitter_emit_local_candidates (self,
        FS_COMPONENT_RTCP);
    }
  }

  g_signal_emit_by_name (self, "local-candidates-prepared");

  /* Lets remove this source from the list of sources to destroy
   * For the case when its called from an idle source
   */
  source = g_main_current_source ();
  if (source)  {
    guint id = g_source_get_id (source);

    g_mutex_lock (self->priv->sources_mutex);
    self->priv->sources = g_list_remove (self->priv->sources,
      GUINT_TO_POINTER (id));
    g_mutex_unlock (self->priv->sources_mutex);
  }

  return FALSE;
}

static void
fs_rawudp_stream_transmitter_maybe_new_active_candidate_pair (
    FsRawUdpStreamTransmitter *self, guint component_id)
{
  switch (component_id) {
    case FS_COMPONENT_RTP:
      if (self->priv->local_active_rtp_candidate &&
        self->priv->remote_rtp_candidate) {
        g_signal_emit_by_name (self, "new-active-candidate-pair",
          self->priv->local_active_rtp_candidate,
          self->priv->remote_rtp_candidate);
      }
      break;
    case FS_COMPONENT_RTCP:
      if (self->priv->local_active_rtcp_candidate &&
        self->priv->remote_rtcp_candidate) {
        g_signal_emit_by_name (self, "new-active-candidate-pair",
          self->priv->local_active_rtcp_candidate,
          self->priv->remote_rtcp_candidate);
      }
      break;
  }
}

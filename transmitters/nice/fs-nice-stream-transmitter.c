/*
 * Farsight2 - Farsight libnice Stream Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-nice-stream-transmitter.c - A Farsight libnice stream transmitter
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
 * SECTION:fs-nice-stream-transmitter
 * @short_description: A stream transmitter object for ICE using libnice
 * @see_also: fs-rawudp-stream-transmitter
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-nice-stream-transmitter.h"
#include "fs-nice-transmitter.h"

#include <gst/farsight/fs-candidate.h>
#include <gst/farsight/fs-conference-iface.h>

#include <gst/gst.h>

#include <string.h>
#include <sys/types.h>

GST_DEBUG_CATEGORY_EXTERN (fs_nice_transmitter_debug);
#define GST_CAT_DEFAULT fs_nice_transmitter_debug

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
  PROP_PREFERRED_LOCAL_CANDIDATES,
  PROP_STUN_IP,
  PROP_STUN_PORT,
  PROP_TURN_IP,
  PROP_TURN_PORT,
  PROP_CONTROLLING_MODE,
  PROP_COMPATIBILITY
};

struct _FsNiceStreamTransmitterPrivate
{
  guint stream_id;

  FsNiceTransmitter *transmitter;

  gboolean created;

  gboolean sending;

  gchar *stun_ip;
  guint stun_port;
  gchar *turn_ip;
  guint turn_port;

  gboolean compatibility;
  gboolean controlling_mode;
};

#define FS_NICE_STREAM_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_NICE_STREAM_TRANSMITTER, \
                                FsNiceStreamTransmitterPrivate))

static void fs_nice_stream_transmitter_class_init (FsNiceStreamTransmitterClass *klass);
static void fs_nice_stream_transmitter_init (FsNiceStreamTransmitter *self);
static void fs_nice_stream_transmitter_dispose (GObject *object);
static void fs_nice_stream_transmitter_finalize (GObject *object);

static void fs_nice_stream_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_nice_stream_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static gboolean fs_nice_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error);
static void fs_nice_stream_transmitter_remote_candidates_added (
    FsStreamTransmitter *streamtransmitter);
static gboolean fs_nice_stream_transmitter_select_candidate_pair (
    FsStreamTransmitter *streamtransmitter,
    const gchar *local_foundation,
    const gchar *remote_foundation,
    GError **error);
static gboolean fs_nice_stream_transmitter_gather_local_candidates (
    FsStreamTransmitter *streamtransmitter,
    GError **error);


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

static GType type = 0;

GType
fs_nice_stream_transmitter_get_type (void)
{
  return type;
}

GType
fs_nice_stream_transmitter_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsNiceStreamTransmitterClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_nice_stream_transmitter_class_init,
    NULL,
    NULL,
    sizeof (FsNiceStreamTransmitter),
    0,
    (GInstanceInitFunc) fs_nice_stream_transmitter_init
  };

  type = g_type_module_register_type (G_TYPE_MODULE (module),
    FS_TYPE_STREAM_TRANSMITTER, "FsNiceStreamTransmitter", &info, 0);

  return type;
}

static void
fs_nice_stream_transmitter_class_init (FsNiceStreamTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsStreamTransmitterClass *streamtransmitterclass =
    FS_STREAM_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_nice_stream_transmitter_set_property;
  gobject_class->get_property = fs_nice_stream_transmitter_get_property;
  gobject_class->dispose = fs_nice_stream_transmitter_dispose;
  gobject_class->finalize = fs_nice_stream_transmitter_finalize;

  streamtransmitterclass->add_remote_candidate =
    fs_nice_stream_transmitter_add_remote_candidate;
  streamtransmitterclass->remote_candidates_added =
    fs_nice_stream_transmitter_remote_candidates_added;
  streamtransmitterclass->select_candidate_pair =
    fs_nice_stream_transmitter_select_candidate_pair;
  streamtransmitterclass->gather_local_candidates =
    fs_nice_stream_transmitter_gather_local_candidates;

  g_type_class_add_private (klass, sizeof (FsNiceStreamTransmitterPrivate));

  g_object_class_override_property (gobject_class, PROP_SENDING, "sending");
  g_object_class_override_property (gobject_class,
      PROP_PREFERRED_LOCAL_CANDIDATES, "preferred-local-candidates");

  g_object_class_install_property (gobject_class, PROP_STUN_IP,
      g_param_spec_string (
          "stun-ip",
          "STUN server",
          "The STUN server used to obtain server-reflexive candidates",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (gobject_class, PROP_STUN_PORT,
      g_param_spec_uint (
          "stun-port",
          "STUN server port",
          "The STUN server used to obtain server-reflexive candidates",
          1, 65536,
          3478,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TURN_IP,
      g_param_spec_string (
          "turn-ip",
          "TURN server",
          "The TURN server used to obtain relay candidates",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TURN_PORT,
      g_param_spec_uint (
          "turn-port",
          "TURN server port",
          "The TURN server used to obtain relay candidates",
          1, 65536,
          3478,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONTROLLING_MODE,
      g_param_spec_boolean (
          "controlling-mode",
          "ICE controlling mode",
          "Whether the agent is in controlling mode",
          TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
fs_nice_stream_transmitter_init (FsNiceStreamTransmitter *self)
{
  /* member init */
  self->priv = FS_NICE_STREAM_TRANSMITTER_GET_PRIVATE (self);

  self->priv->sending = TRUE;
}

static void
fs_nice_stream_transmitter_dispose (GObject *object)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);

  if (self->priv->created)
  {
    nice_agent_remove_stream (self->priv->transmitter->agent,
        self->priv->stream_id);
    self->priv->created = FALSE;
  }

  parent_class->dispose (object);
}

static void
fs_nice_stream_transmitter_finalize (GObject *object)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);

  g_free (self->priv->stun_ip);
  g_free (self->priv->turn_ip);

  parent_class->finalize (object);
}

static void
fs_nice_stream_transmitter_get_property (GObject *object,
                                           guint prop_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_SENDING:
      g_value_set_boolean (value, self->priv->sending);
      break;
    case PROP_STUN_IP:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_string (value, self->priv->stun_ip);
      break;
    case PROP_STUN_PORT:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_uint (value, self->priv->stun_port);
      break;
    case PROP_TURN_IP:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_string (value, self->priv->turn_ip);
      break;
    case PROP_TURN_PORT:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_uint (value, self->priv->turn_port);

      break;
    case PROP_CONTROLLING_MODE:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_boolean (value, self->priv->controlling_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_nice_stream_transmitter_set_property (GObject *object,
                                           guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_SENDING:
      self->priv->sending = g_value_get_boolean (value);
      break;
    case PROP_STUN_IP:
      self->priv->stun_ip = g_value_dup_string (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    case PROP_STUN_PORT:
      self->priv->stun_port = g_value_get_uint (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    case PROP_TURN_IP:
      self->priv->turn_ip = g_value_dup_string (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    case PROP_TURN_PORT:
      self->priv->turn_port = g_value_get_uint (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    case PROP_CONTROLLING_MODE:
      self->priv->controlling_mode = g_value_get_boolean (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * fs_nice_stream_transmitter_add_remote_candidate
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
fs_nice_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error)
{
  return FALSE;
}


static void
fs_nice_stream_transmitter_remote_candidates_added (
    FsStreamTransmitter *streamtransmitter)
{
}

static gboolean
fs_nice_stream_transmitter_select_candidate_pair (
    FsStreamTransmitter *streamtransmitter,
    const gchar *local_foundation,
    const gchar *remote_foundation,
    GError **error)
{
  FsNiceStreamTransmitter *self =
    FS_NICE_STREAM_TRANSMITTER (streamtransmitter);
  gint c;
  gboolean res = TRUE;

  if (!self->priv->created)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Can not call this function before gathering local candidates");
    return FALSE;
  }

  for (c = 1; c <= self->priv->transmitter->components; c++)
    if (!nice_agent_set_selected_pair (self->priv->transmitter->agent,
            self->priv->stream_id, c, local_foundation, remote_foundation))
      res = FALSE;

  if (!res)
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Unknown error while selecting pairs");

  return res;
}

static FsCandidateType
nice_candidate_type_to_fs_candidate_type (NiceCandidateType type)
{
  switch (type)
  {
    case NICE_CANDIDATE_TYPE_HOST:
      return FS_CANDIDATE_TYPE_HOST;
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
      return FS_CANDIDATE_TYPE_SRFLX;
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
      return FS_CANDIDATE_TYPE_PRFLX;
    case NICE_CANDIDATE_TYPE_RELAYED:
      return FS_CANDIDATE_TYPE_RELAY;
    default:
      g_warning ("Invalid candidate type %d, defaulting to type host", type);
      return FS_CANDIDATE_TYPE_HOST;
  }
}

static FsNetworkProtocol
nice_candidate_transport_to_fs_network_protocol (NiceCandidateTransport trans)
{
  switch (trans)
  {
    case NICE_CANDIDATE_TRANSPORT_UDP:
      return FS_NETWORK_PROTOCOL_UDP;
    default:
      g_warning ("Invalid Nice network transport type %u", trans);
      return FS_NETWORK_PROTOCOL_UDP;
  }
}

static FsCandidate *
nice_candidate_to_fs_candidate (NiceCandidate *nicecandidate)
{
  FsCandidate *fscandidate;
  gchar *ipaddr = g_malloc (INET_ADDRSTRLEN);

  nice_address_to_string (&nicecandidate->addr, ipaddr);

  fscandidate = fs_candidate_new (
      nicecandidate->foundation,
      nicecandidate->component_id,
      nice_candidate_type_to_fs_candidate_type (nicecandidate->type),
      nice_candidate_transport_to_fs_network_protocol (
          nicecandidate->transport),
      ipaddr,
      nice_address_get_port (&nicecandidate->addr));

  nice_address_to_string (&nicecandidate->base_addr, ipaddr);

  fscandidate->base_ip = ipaddr;
  fscandidate->base_port = nice_address_get_port (&nicecandidate->base_addr);

  fscandidate->username = g_strdup (nicecandidate->username);
  fscandidate->password = g_strdup (nicecandidate->password);
  fscandidate->priority = nicecandidate->priority;

  return fscandidate;
}

static gboolean
fs_nice_stream_transmitter_gather_local_candidates (
    FsStreamTransmitter *streamtransmitter,
    GError **error)
{
  return TRUE;
}



void
fs_nice_stream_transmitter_state_changed (FsNiceStreamTransmitter *self,
    guint component_id,
    guint state)
{
}


void
fs_nice_stream_transmitter_selected_pair (
    FsNiceStreamTransmitter *self,
    guint component_id,
    const gchar *lfoundation,
    const gchar *rfoundation)
{
  GSList *candidates, *item;
  FsCandidate *local = NULL;
  FsCandidate *remote = NULL;

  candidates = nice_agent_get_local_candidates (
      self->priv->transmitter->agent,
      self->priv->stream_id, component_id);

  for (item = candidates; item; item = g_slist_next (item))
  {
    NiceCandidate *candidate = item->data;

    if (!strcmp (item->data, lfoundation))
    {
      local = nice_candidate_to_fs_candidate (candidate);
      break;
    }
  }
  g_slist_free (candidates);

  candidates = nice_agent_get_remote_candidates (
      self->priv->transmitter->agent,
      self->priv->stream_id, component_id);

  for (item = candidates; item; item = g_slist_next (item))
  {
    NiceCandidate *candidate = item->data;

    if (!strcmp (item->data, lfoundation))
    {
      remote = nice_candidate_to_fs_candidate (candidate);
      break;
    }
  }
  g_slist_free (candidates);


  if (local && remote)
    g_signal_emit_by_name (self, "new-active-candidate-pair", local, remote);

  if (local)
    fs_candidate_destroy (local);
  if (remote)
    fs_candidate_destroy (remote);
}


void
fs_nice_stream_transmitter_new_candidate (FsNiceStreamTransmitter *self,
    guint component_id,
    const gchar *foundation)
{
  FsCandidate *fscandidate = NULL;
  GSList *candidates, *item;

  candidates = nice_agent_get_local_candidates (
      self->priv->transmitter->agent,
      self->priv->stream_id, component_id);

  for (item = candidates; item; item = g_slist_next (item))
  {
    NiceCandidate *candidate = item->data;

    if (!strcmp (item->data, foundation))
    {
      fscandidate = nice_candidate_to_fs_candidate (candidate);
      break;
    }
  }
  g_slist_free (candidates);

  if (fscandidate)
  {
    g_signal_emit_by_name (self, "new-local-candidate", fscandidate);
    fs_candidate_destroy (fscandidate);
  }
}

void
fs_nice_stream_transmitter_gathering_done (FsNiceStreamTransmitter *self)
{
}


FsNiceStreamTransmitter *
fs_nice_stream_transmitter_newv (FsNiceTransmitter *transmitter,
    guint stream_id,
    guint n_parameters,
    GParameter *parameters,
    GError **error)
{
  FsNiceStreamTransmitter *streamtransmitter = NULL;

  streamtransmitter = g_object_newv (FS_TYPE_NICE_STREAM_TRANSMITTER,
    n_parameters, parameters);

  if (!streamtransmitter)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not build the stream transmitter");
    return NULL;
  }

  streamtransmitter->priv->transmitter = transmitter;
  streamtransmitter->priv->stream_id = stream_id;

  return streamtransmitter;
}

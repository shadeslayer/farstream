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
#include "fs-nice-agent.h"

#include <gst/farsight/fs-conference-iface.h>
#include <gst/farsight/fs-interfaces.h>

#include <gst/gst.h>

#include <string.h>
#include <sys/types.h>

#include <udp-bsd.h>

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
  PROP_STATE,
  PROP_STUN_IP,
  PROP_STUN_PORT,
  PROP_TURN_IP,
  PROP_TURN_PORT,
  PROP_CONTROLLING_MODE,
  PROP_STREAM_ID,
  PROP_COMPATIBILITY_MODE
};

struct _FsNiceStreamTransmitterPrivate
{
  FsNiceTransmitter *transmitter;

  NiceAgent *agent;

  guint stream_id;

  gboolean sending;

  gchar *stun_ip;
  guint stun_port;
  gchar *turn_ip;
  guint turn_port;

  gboolean controlling_mode;

  guint compatibility_mode;

  GMutex *mutex;

  GList *preferred_local_candidates;

  gulong state_changed_handler_id;
  gulong gathering_done_handler_id;
  gulong new_selected_pair_handler_id;
  gulong new_candidate_handler_id;

  /* Everything below is protected by the mutex */

  gboolean gathered;

  gboolean candidates_added;

  GList *candidates_to_set;

  NiceGstStream *gststream;

  FsStreamState state;
  FsStreamState *component_states;
};

#define FS_NICE_STREAM_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_NICE_STREAM_TRANSMITTER, \
                                FsNiceStreamTransmitterPrivate))

#define FS_NICE_STREAM_TRANSMITTER_LOCK(o)   g_mutex_lock ((o)->priv->mutex)
#define FS_NICE_STREAM_TRANSMITTER_UNLOCK(o) g_mutex_unlock ((o)->priv->mutex)


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

static void agent_state_changed (NiceAgent *agent,
    guint stream_id,
    guint component_id,
    guint state,
    gpointer user_data);
static void agent_gathering_done (NiceAgent *agent, gpointer user_data);
static void agent_new_selected_pair (NiceAgent *agent,
    guint stream_id,
    guint component_id,
    const gchar *lfoundation,
    const gchar *rfoundation,
    gpointer user_data);
static void agent_new_candidate (NiceAgent *agent,
    guint stream_id,
    guint component_id,
    const gchar *foundation,
    gpointer user_data);


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

static GType type = 0;

/*
 * This is global because its not a ref-counted object
 */
static NiceUDPSocketFactory udpfactory;


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
  g_object_class_override_property (gobject_class,
      PROP_STATE, "state");

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

  g_object_class_install_property (gobject_class, PROP_STREAM_ID,
      g_param_spec_uint (
          "stream-id",
          "The id of the stream",
          "The id of the stream according to libnice",
          0, G_MAXINT,
          0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COMPATIBILITY_MODE,
      g_param_spec_uint (
          "compatibility-mode",
          "The compability-mode",
          "The id of the stream according to libnice",
          NICE_COMPATIBILITY_ID19, NICE_COMPATIBILITY_LAST,
          NICE_COMPATIBILITY_ID19,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  nice_udp_bsd_socket_factory_init (&udpfactory);

}

static void
fs_nice_stream_transmitter_init (FsNiceStreamTransmitter *self)
{
  /* member init */
  self->priv = FS_NICE_STREAM_TRANSMITTER_GET_PRIVATE (self);

  self->priv->sending = TRUE;
  self->priv->state = FS_STREAM_STATE_DISCONNECTED;
  self->priv->mutex = g_mutex_new ();

  self->priv->controlling_mode = TRUE;
}

static void
fs_nice_stream_transmitter_dispose (GObject *object)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);

  FS_NICE_STREAM_TRANSMITTER_LOCK (self);
  if (self->priv->gststream)
    fs_nice_transmitter_free_gst_stream (self->priv->transmitter,
        self->priv->gststream);
  self->priv->gststream = NULL;

  if (self->priv->stream_id)
    nice_agent_remove_stream (self->priv->agent,
        self->priv->stream_id);
  self->priv->stream_id = 0;

  if (self->priv->state_changed_handler_id)
    g_signal_handler_disconnect (self->priv->agent,
        self->priv->state_changed_handler_id);
  self->priv->state_changed_handler_id = 0;

  if (self->priv->gathering_done_handler_id)
    g_signal_handler_disconnect (self->priv->agent,
        self->priv->gathering_done_handler_id);
  self->priv->gathering_done_handler_id = 0;

  if (self->priv->new_selected_pair_handler_id)
    g_signal_handler_disconnect (self->priv->agent,
        self->priv->new_selected_pair_handler_id);
  self->priv->new_selected_pair_handler_id = 0;

  if (self->priv->new_candidate_handler_id)
    g_signal_handler_disconnect (self->priv->agent,
        self->priv->new_candidate_handler_id);
  self->priv->new_candidate_handler_id = 0;

  if (self->priv->agent)
  {
    g_object_unref (self->priv->agent);
    self->priv->agent = NULL;
  }
  FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);

  if (self->priv->transmitter)
  {
    g_object_unref (self->priv->transmitter);
    self->priv->transmitter = NULL;
  }

  parent_class->dispose (object);
}

static void
fs_nice_stream_transmitter_finalize (GObject *object)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);

  fs_candidate_list_destroy (self->priv->preferred_local_candidates);

  g_free (self->priv->component_states);

  g_free (self->priv->stun_ip);
  g_free (self->priv->turn_ip);

  g_mutex_free (self->priv->mutex);

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
    case PROP_PREFERRED_LOCAL_CANDIDATES:
      g_value_set_boxed (value, self->priv->preferred_local_candidates);
      break;
    case PROP_STATE:
      FS_NICE_STREAM_TRANSMITTER_LOCK (self);
      g_value_set_enum (value, self->priv->state);
      FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);
      break;
    case PROP_STUN_IP:
      if (self->priv->agent)
        g_object_get_property (G_OBJECT (self->priv->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_string (value, self->priv->stun_ip);
      break;
    case PROP_STUN_PORT:
      if (self->priv->agent)
        g_object_get_property (G_OBJECT (self->priv->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_uint (value, self->priv->stun_port);
      break;
    case PROP_TURN_IP:
      if (self->priv->agent)
        g_object_get_property (G_OBJECT (self->priv->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_string (value, self->priv->turn_ip);
      break;
    case PROP_TURN_PORT:
      if (self->priv->agent)
        g_object_get_property (G_OBJECT (self->priv->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_uint (value, self->priv->turn_port);

      break;
    case PROP_CONTROLLING_MODE:
      if (self->priv->agent)
        g_object_get_property (G_OBJECT (self->priv->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_boolean (value, self->priv->controlling_mode);
      break;
    case PROP_STREAM_ID:
      FS_NICE_STREAM_TRANSMITTER_LOCK (self);
      g_value_set_uint (value, self->priv->stream_id);
      FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);
      break;
    case PROP_COMPATIBILITY_MODE:
      g_value_set_uint (value, self->priv->compatibility_mode);
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
    case PROP_PREFERRED_LOCAL_CANDIDATES:
      self->priv->preferred_local_candidates = g_value_dup_boxed (value);
      break;
    case PROP_STUN_IP:
      self->priv->stun_ip = g_value_dup_string (value);
      break;
    case PROP_STUN_PORT:
      self->priv->stun_port = g_value_get_uint (value);
      break;
    case PROP_TURN_IP:
      self->priv->turn_ip = g_value_dup_string (value);
      break;
    case PROP_TURN_PORT:
      self->priv->turn_port = g_value_get_uint (value);
      break;
    case PROP_CONTROLLING_MODE:
      self->priv->controlling_mode = g_value_get_boolean (value);
      if (self->priv->transmitter && self->priv->agent)
        g_object_set_property (G_OBJECT (self->priv->agent),
            g_param_spec_get_name (pspec), value);
      break;
    case PROP_COMPATIBILITY_MODE:
      self->priv->compatibility_mode = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static NiceCandidateType
fs_candidate_type_to_nice_candidate_type (FsCandidateType type)
{
  switch (type)
  {
    case FS_CANDIDATE_TYPE_HOST:
      return NICE_CANDIDATE_TYPE_HOST;
    case FS_CANDIDATE_TYPE_SRFLX:
      return NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
    case FS_CANDIDATE_TYPE_PRFLX:
      return NICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
    case FS_CANDIDATE_TYPE_RELAY:
      return NICE_CANDIDATE_TYPE_RELAYED;
    default:
      GST_WARNING ("Invalid candidate type %d, defaulting to type host", type);
      return NICE_CANDIDATE_TYPE_HOST;
  }
}

static NiceCandidateTransport
fs_network_protocol_to_nice_candidate_protocol (FsNetworkProtocol proto)
{
  switch (proto)
  {
    case FS_NETWORK_PROTOCOL_UDP:
      return NICE_CANDIDATE_TRANSPORT_UDP;
    default:
      GST_WARNING ("Invalid Fs network protocol type %u", proto);
      return NICE_CANDIDATE_TRANSPORT_UDP;
  }
}

static NiceCandidate *
fs_candidate_to_nice_candidate (FsNiceStreamTransmitter *self,
    FsCandidate *candidate)
{
  NiceCandidate *nc = nice_candidate_new (
      fs_candidate_type_to_nice_candidate_type (candidate->type));

  nc->transport =
    fs_network_protocol_to_nice_candidate_protocol (candidate->proto);
  nc->priority = candidate->priority;
  nc->stream_id = self->priv->stream_id;
  nc->component_id = candidate->component_id;
  strncpy (nc->foundation, candidate->foundation,
      NICE_CANDIDATE_MAX_FOUNDATION);

  nc->username = g_strdup(candidate->username);
  nc->password = g_strdup(candidate->password);


  g_warning ("%s %s", nc->username, nc->password);

  if (candidate->ip == NULL || candidate->port == 0)
    goto error;
  if (!nice_address_set_from_string (&nc->addr, candidate->ip))
    goto error;
  nice_address_set_port (&nc->addr, candidate->port);

  if (candidate->base_ip && candidate->base_port)
  {
    if (!nice_address_set_from_string (&nc->base_addr, candidate->base_ip))
      goto error;
    nice_address_set_port (&nc->base_addr, candidate->base_port);
  }

  return nc;

 error:
  g_free (nc);
  return NULL;
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
  FsNiceStreamTransmitter *self =
    FS_NICE_STREAM_TRANSMITTER (streamtransmitter);
  GSList *list = NULL;
  NiceCandidate *cand = NULL;

  FS_NICE_STREAM_TRANSMITTER_LOCK (self);
  if (!self->priv->candidates_added)
  {
    self->priv->candidates_to_set = g_list_append (
        self->priv->candidates_to_set,
        fs_candidate_copy (candidate));
    FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);
    return TRUE;
  }
  FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);

  cand = fs_candidate_to_nice_candidate (self, candidate);

  if (!cand)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid candidate added");
    goto error;
  }

  list = g_slist_prepend (NULL, cand);

  nice_agent_set_remote_candidates (self->priv->agent,
      self->priv->stream_id, candidate->component_id, list);

  g_slist_foreach (list, (GFunc)nice_candidate_free, NULL);
  g_slist_free (list);

  return TRUE;

 error:

  FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);
  return FALSE;
}


static void
fs_nice_stream_transmitter_remote_candidates_added (
    FsStreamTransmitter *streamtransmitter)
{
  FsNiceStreamTransmitter *self =
    FS_NICE_STREAM_TRANSMITTER (streamtransmitter);
  GList *candidates = NULL, *item;
  GSList *nice_candidates = NULL;
  gint c;

  FS_NICE_STREAM_TRANSMITTER_LOCK (self);
  if (self->priv->candidates_added)
  {
    FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);
    GST_LOG ("remote_candidates_added already called, ignoring");
    return;
  }

  self->priv->candidates_added = TRUE;
  candidates = self->priv->candidates_to_set;
  self->priv->candidates_to_set = NULL;
  FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);

  /*
  if (candidates)
  {
    FsCandidate *cand = candidates->data;
    nice_agent_set_remote_credentials (self->priv->agent,
        self->priv->stream_id, cand->username, cand->password);
  }
  else
  {
    GST_DEBUG ("Candidates added called before any candidate set,"
        " assuming we're in ice-6 with dribble, so every candidate has"
        " its own password");
  }
  */

  for (c = 1; c <= self->priv->transmitter->components; c++)
  {
    for (item = candidates;
         item;
         item = g_list_next (item))
    {
      FsCandidate *candidate = item->data;

      if (candidate->component_id == c)
      {
        NiceCandidate *nc = fs_candidate_to_nice_candidate (self, candidate);

        if (!nc)
          goto error;

        nice_candidates = g_slist_append (nice_candidates, nc);
      }
    }

    nice_agent_set_remote_candidates (self->priv->agent,
        self->priv->stream_id, c, nice_candidates);

    g_slist_foreach (nice_candidates, (GFunc)nice_candidate_free, NULL);
    g_slist_free (nice_candidates);
    nice_candidates = NULL;
  }

  fs_candidate_list_destroy (candidates);

  return;
 error:
  fs_stream_transmitter_emit_error (FS_STREAM_TRANSMITTER (self),
      FS_ERROR_INVALID_ARGUMENTS,
      "Invalid remote candidate passed",
      "Remote candidate passed in previous add_remote_candidate() call invalid");
  g_slist_foreach (nice_candidates, (GFunc) nice_candidate_free, NULL);
  g_slist_free (nice_candidates);
  fs_candidate_list_destroy (candidates);
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

  if (self->priv->stream_id == 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Can not call this function before gathering local candidates");
    return FALSE;
  }

  for (c = 1; c <= self->priv->transmitter->components; c++)
    if (!nice_agent_set_selected_pair (self->priv->agent,
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
      GST_WARNING ("Invalid candidate type %d, defaulting to type host", type);
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
      GST_WARNING ("Invalid Nice network transport type %u", trans);
      return FS_NETWORK_PROTOCOL_UDP;
  }
}

static FsCandidate *
nice_candidate_to_fs_candidate (NiceAgent *agent, NiceCandidate *nicecandidate,
    gboolean local)
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

  if (nice_address_is_valid (&nicecandidate->base_addr))
  {
    nice_address_to_string (&nicecandidate->base_addr, ipaddr);
    fscandidate->base_ip = ipaddr;
    fscandidate->base_port = nice_address_get_port (&nicecandidate->base_addr);
  }
  else
  {
    g_free (ipaddr);
    ipaddr = NULL;
  }

  fscandidate->username = g_strdup (nicecandidate->username);
  fscandidate->password = g_strdup (nicecandidate->password);
  fscandidate->priority = nicecandidate->priority;

  if (local && fscandidate->username == NULL && fscandidate->password == NULL)
  {
    const gchar *username = NULL, *password = NULL;
    nice_agent_get_local_credentials (agent, nicecandidate->stream_id,
        &username, &password);
    fscandidate->username = g_strdup (username);
    fscandidate->password = g_strdup (password);

    if (username == NULL || password == NULL)
    {
      GST_WARNING ("The stream has no credentials??");
    }
  }



  return fscandidate;
}


static gboolean
candidate_list_are_equal (GList *list1, GList *list2)
{
  for (;
       list1 && list2;
       list1 = list1->next, list2 = list2->next)
  {
    FsCandidate *cand1 = list1->data;
    FsCandidate *cand2 = list2->data;

    if (strcmp (cand1->ip, cand2->ip))
        return FALSE;
  }

  return TRUE;
}

static void
weak_agent_removed (gpointer user_data, GObject *where_the_object_was)
{
  GList *agents = NULL;
  FsParticipant *participant = user_data;

  agents = g_object_get_data (G_OBJECT (participant), "nice-agents");
  agents = g_list_remove (agents, where_the_object_was);
  g_object_set_data (G_OBJECT (participant), "nice-agents", agents);
  g_object_unref (participant);
}

static gboolean
fs_nice_stream_transmitter_build (FsNiceStreamTransmitter *self,
    FsParticipant *participant,
    GError **error)
{
  GList *item;
  gboolean set = FALSE;
  GList *agents  = NULL;
  FsNiceAgent *thread = NULL;
  NiceAgent *agent = NULL;

  /* Before going any further, check that the list of candidates are ok */

  for (item = g_list_first (self->priv->preferred_local_candidates);
       item;
       item = g_list_next (item))
  {
    FsCandidate *cand = item->data;

    if (cand->ip == NULL)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "You have to set an ip on your preferred candidate");
      return FALSE;
    }

    if (cand->port || cand->component_id)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "You can not set a port or component id"
          " for the preferred nice candidate");
      return FALSE;
    }

    if (cand->type != FS_CANDIDATE_TYPE_HOST)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "You can only set preferred candidates of type host");
      return FALSE;
    }

    if (cand->proto != FS_NETWORK_PROTOCOL_UDP)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "Only UDP preferred candidates can be set");
      return FALSE;
    }
  }


  /* First find if there is already a matching agent */

  agents = g_object_get_data (G_OBJECT (participant), "nice-agents");

  for (item = g_list_first (agents);
       item;
       item = g_list_next (item))
  {
    guint stun_port, turn_port;
    gchar *stun_server, *turn_server;
    guint compatibility;

    agent = item->data;

    g_object_get (agent,
        "stun-server", &stun_server,
        "stun-server-port", &stun_port,
        "turn-server", &turn_server,
        "turn-server-port", &turn_port,
        "compatibility", &compatibility,
        NULL);

    if (!thread)
      thread = g_object_get_data (G_OBJECT (agent), "nice-thread");

    /*
     * Check if the agent matches our requested criteria
     */
    if (compatibility == self->priv->compatibility_mode &&
        stun_port == self->priv->stun_port &&
        turn_port == self->priv->turn_port &&
        (stun_server == self->priv->stun_ip ||
            (stun_server && self->priv->stun_ip &&
                !strcmp (stun_server, self->priv->stun_ip))) &&
        (turn_server == self->priv->turn_ip ||
            (turn_server && self->priv->turn_ip &&
                !strcmp (turn_server, self->priv->turn_ip))))
    {
      GList *prefs = g_object_get_data (G_OBJECT (agent),
          "preferred-local-candidates");

      if (candidate_list_are_equal (prefs,
              self->priv->preferred_local_candidates))
        break;
    }
  }


  /* In this case we need to build a new agent */
  if (item == NULL)
  {
    GMainContext *ctx = NULL;
    GList *local_prefs_copy;

    /* If we don't have a thread, build one */
    if (thread == NULL)
    {
      thread = fs_nice_agent_new (self->priv->compatibility_mode,
          self->priv->preferred_local_candidates,
          error);
      if (!thread)
        return FALSE;
    }

    ctx = fs_nice_agent_get_context (thread);

    agent = nice_agent_new (&udpfactory, ctx, self->priv->compatibility_mode);

    if (!agent)
    {
      g_object_unref (thread);
      g_object_unref (thread);
      g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
          "Could not make nice agent");
      return FALSE;
    }

    fs_nice_agent_add_weak_object (thread, G_OBJECT (agent));

    g_object_set_data (G_OBJECT (thread), "nice-thread", thread);

    g_object_unref (thread);

    if (self->priv->stun_ip && self->priv->stun_port)
      g_object_set (agent,
          "stun-server", self->priv->stun_ip,
          "stun-server-port", self->priv->stun_port,
          NULL);

    if (self->priv->turn_ip && self->priv->turn_port)
      g_object_set (agent,
          "turn-server", self->priv->turn_ip,
          "turn-server-port", self->priv->turn_port,
          NULL);

    g_object_set (agent,
        "controlling-mode", self->priv->controlling_mode,
        NULL);

    local_prefs_copy = fs_candidate_list_copy (
        self->priv->preferred_local_candidates);
    g_object_set_data (G_OBJECT (agent), "preferred-local-candidates",
        local_prefs_copy);
    g_object_weak_ref (G_OBJECT (agent),
        (GWeakNotify) fs_candidate_list_destroy,
        local_prefs_copy);

    agents = g_list_prepend (agents, agent);
    g_object_set_data (G_OBJECT (participant), "nice-agents", agents);
    g_object_weak_ref (G_OBJECT (agent), weak_agent_removed, participant);
    g_object_ref (participant);

    self->priv->agent = agent;

    for (item = self->priv->preferred_local_candidates;
         item;
         item = g_list_next (item))
    {
      FsCandidate *cand = item->data;
      NiceAddress *addr = nice_address_new ();

      if (nice_address_set_from_string (addr, cand->ip))
      {
        if (!nice_agent_add_local_address (self->priv->agent, addr))
        {
          g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
              "Unable to set preferred local candidate");
          return FALSE;
        }
        set = TRUE;
      }
      else
      {
        g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
            "Invalid local address passed");
        nice_address_free (addr);
        return FALSE;
      }
      nice_address_free (addr);
    }

    if (!set)
    {      GList *addresses = fs_interfaces_get_local_ips (FALSE);

      for (item = addresses;
           item;
           item = g_list_next (item))
      {
        NiceAddress *addr = nice_address_new ();;

        if (nice_address_set_from_string (addr, item->data))
        {
          if (!nice_agent_add_local_address (self->priv->agent,
                  addr))
          {
            g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
                "Unable to set preferred local candidate");
            return FALSE;
          }
        }
        else
        {
          g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
              "Invalid local address passed");
          nice_address_free (addr);
          return FALSE;
        }
        nice_address_free (addr);
      }

      g_list_foreach (addresses, (GFunc) g_free, NULL);
      g_list_free (addresses);
    }


  } else {
    self->priv->agent = g_object_ref (agent);
  }

  self->priv->component_states = g_new0 (FsStreamState,
      self->priv->transmitter->components);


  self->priv->stream_id = nice_agent_add_stream (
      self->priv->agent,
      self->priv->transmitter->components);

  if (self->priv->stream_id == 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create libnice stream");
    return FALSE;
  }

  self->priv->state_changed_handler_id = g_signal_connect (agent,
      "component-state-changed", G_CALLBACK (agent_state_changed), self);
  self->priv->gathering_done_handler_id = g_signal_connect (agent,
      "candidate-gathering-done", G_CALLBACK (agent_gathering_done), self);
  self->priv->new_selected_pair_handler_id = g_signal_connect (agent,
      "new-selected-pair", G_CALLBACK (agent_new_selected_pair), self);
  self->priv->new_candidate_handler_id = g_signal_connect (agent,
      "new-candidate", G_CALLBACK (agent_new_candidate), self);


  self->priv->gststream = fs_nice_transmitter_add_gst_stream (
      self->priv->transmitter,
      self->priv->agent,
      self->priv->stream_id,
      error);
  if (self->priv->gststream == NULL)
    return FALSE;

  GST_DEBUG ("Created a stream with %u components",
      self->priv->transmitter->components);

  return TRUE;
}

static gboolean
fs_nice_stream_transmitter_gather_local_candidates (
    FsStreamTransmitter *streamtransmitter,
    GError **error)
{
  FsNiceStreamTransmitter *self =
    FS_NICE_STREAM_TRANSMITTER (streamtransmitter);

  GST_DEBUG ("Stream %u started", self->priv->stream_id);

  nice_agent_gather_candidates (self->priv->agent,
      self->priv->stream_id);

  return TRUE;
}

static FsStreamState
nice_component_state_to_fs_stream_state (NiceComponentState state)
{
  switch (state)
  {
    case NICE_COMPONENT_STATE_DISCONNECTED:
      return FS_STREAM_STATE_DISCONNECTED;
    case NICE_COMPONENT_STATE_GATHERING:
      return FS_STREAM_STATE_GATHERING;
    case NICE_COMPONENT_STATE_CONNECTING:
      return FS_STREAM_STATE_CONNECTING;
    case NICE_COMPONENT_STATE_CONNECTED:
      return FS_STREAM_STATE_CONNECTED;
    case NICE_COMPONENT_STATE_READY:
      return FS_STREAM_STATE_READY;
    case NICE_COMPONENT_STATE_FAILED:
      return FS_STREAM_STATE_FAILED;
    default:
      GST_ERROR ("Invalid state %u", state);
      return FS_STREAM_STATE_FAILED;
  }
}

static void
agent_state_changed (NiceAgent *agent,
    guint stream_id,
    guint component_id,
    guint state,
    gpointer user_data)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (user_data);
  FsStreamState fs_state = nice_component_state_to_fs_stream_state (state);
  gboolean identical = TRUE;
  gint i;

  if (stream_id != self->priv->stream_id)
    return;

  GST_DEBUG ("Stream: %u Component %u has state %u",
      self->priv->stream_id, component_id, state);

  FS_NICE_STREAM_TRANSMITTER_LOCK (self);

  self->priv->component_states[component_id - 1] = fs_state;

  for (i = 0; i < self->priv->transmitter->components; i++)
    if (self->priv->component_states[i] != fs_state)
    {
      identical = FALSE;
      break;
    }
  if (identical)
    self->priv->state = fs_state;

  FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);

  if (identical)
    g_object_notify (G_OBJECT (self), "state");
}


static void
agent_new_selected_pair (NiceAgent *agent,
    guint stream_id,
    guint component_id,
    const gchar *lfoundation,
    const gchar *rfoundation,
    gpointer user_data)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (user_data);
  GSList *candidates, *item;
  FsCandidate *local = NULL;
  FsCandidate *remote = NULL;

  if (stream_id != self->priv->stream_id)
    return;

  candidates = nice_agent_get_local_candidates (
      self->priv->agent,
      self->priv->stream_id, component_id);

  for (item = candidates; item; item = g_slist_next (item))
  {
    NiceCandidate *candidate = item->data;

    if (!strcmp (candidate->foundation, lfoundation))
    {
      local = nice_candidate_to_fs_candidate (self->priv->agent,
          candidate, TRUE);
      break;
    }
  }
  g_slist_foreach (candidates, (GFunc)nice_candidate_free, NULL);
  g_slist_free (candidates);

  candidates = nice_agent_get_remote_candidates (
      self->priv->agent,
      self->priv->stream_id, component_id);

  for (item = candidates; item; item = g_slist_next (item))
  {
    NiceCandidate *candidate = item->data;

    if (!strcmp (candidate->foundation, rfoundation))
    {
      remote = nice_candidate_to_fs_candidate (self->priv->agent,
          candidate, FALSE);
      break;
    }
  }
  g_slist_foreach (candidates, (GFunc)nice_candidate_free, NULL);
  g_slist_free (candidates);


  if (local && remote)
    g_signal_emit_by_name (self, "new-active-candidate-pair", local, remote);

  if (local)
    fs_candidate_destroy (local);
  if (remote)
    fs_candidate_destroy (remote);
}


static void
agent_new_candidate (NiceAgent *agent,
    guint stream_id,
    guint component_id,
    const gchar *foundation,
    gpointer user_data)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (user_data);
  FsCandidate *fscandidate = NULL;
  GSList *candidates, *item;

  if (stream_id != self->priv->stream_id)
    return;

  FS_NICE_STREAM_TRANSMITTER_LOCK (self);
  if (!self->priv->gathered)
  {
    FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);
    return;
  }
  FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);

  GST_DEBUG ("New candidate found for stream %u component %u",
      stream_id, component_id);

  candidates = nice_agent_get_local_candidates (
      self->priv->agent,
      self->priv->stream_id, component_id);

  for (item = candidates; item; item = g_slist_next (item))
  {
    NiceCandidate *candidate = item->data;

    if (!strcmp (item->data, foundation))
    {
      fscandidate = nice_candidate_to_fs_candidate (
          self->priv->agent, candidate, TRUE);
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

static void
agent_gathering_done (NiceAgent *agent, gpointer user_data)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (user_data);
  GSList *candidates, *item;
  gint c;

  FS_NICE_STREAM_TRANSMITTER_LOCK (self);
  if (self->priv->gathered)
  {
    FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);
    return;
  }
  self->priv->gathered = TRUE;
  FS_NICE_STREAM_TRANSMITTER_UNLOCK (self);

  GST_DEBUG ("Candidates gathered for stream %u", self->priv->stream_id);

  for (c = 1; c <= self->priv->transmitter->components; c++)
  {
    candidates = nice_agent_get_local_candidates (
        self->priv->agent,
        self->priv->stream_id, c);

    for (item = candidates; item; item = g_slist_next (item))
    {
      NiceCandidate *candidate = item->data;
      FsCandidate *fscandidate;

      fscandidate = nice_candidate_to_fs_candidate (
          self->priv->agent, candidate, TRUE);
      g_signal_emit_by_name (self, "new-local-candidate", fscandidate);
      fs_candidate_destroy (fscandidate);
    }


    g_slist_foreach (candidates, (GFunc)nice_candidate_free, NULL);
    g_slist_free (candidates);
  }
  g_signal_emit_by_name (self, "local-candidates-prepared");
}


FsNiceStreamTransmitter *
fs_nice_stream_transmitter_newv (FsNiceTransmitter *transmitter,
    FsParticipant *participant,
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

  streamtransmitter->priv->transmitter = g_object_ref (transmitter);

  if (!fs_nice_stream_transmitter_build (streamtransmitter, participant, error))
  {
    g_object_unref (streamtransmitter);
    return NULL;
  }

  return streamtransmitter;
}

/*
 * Farsight2 - Farsight RAW UDP with STUN Stream Transmitter
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008x Nokia Corp.
 *
 * fs-rawudp-transmitter.c - A Farsight UDPs stream transmitter with STUN
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
 * SECTION:fs-rawudp-stream-transmitter
 * @short_description: A stream transmitter object for UDP with STUN
 * @see_also: fs-multicast-stream-transmitter
 *
 * <refsect2>
 * <para>
 * This transmitter sends and receives unicast UDP packets.
 * </para>
 *
 * <para>
 * It will detect its own address using a STUN request if the
 * #FsRawUdpStreamTransmitter:stun-ip and #FsRawUdpStreamTransmitter:stun-port
 * properties are set. If the STUN request does not get a reply
 * or no STUN is requested. It will return the IP address of all the local
 * network interfaces, listing link-local addresses after other addresses
 * and the loopback interface last.
 * </para>
 *
 * <para>
 * You can configure the address and port it will listen on by setting the
 * "preferred-local-candidates" property. This property will contain a #GList
 * of #FsCandidate. These #FsCandidate must be for #FS_NETWORK_PROTOCOL_UDP.
 * These port and/or the ip can be set on these candidates to force them,
 * and this is per-component. If not all components have a port set, the
 * following components will be on the following ports. There is no guarantee
 * that the requested port will be available so a different port may the
 * native candidate. But it is guaranteed that components that do not have
 * specified ports will be sequential.
 * </para>
 *
 * <para>
 * Example: Candidate {proto=UDP, component_id=RTP, ip=NULL, port=9098} will
 *  produce native candidates
 * ({component_id=RTP, ip=IP, port=9078},{component_id=RTCP, ip=IP, port=9079})
 *  or
 * if this one is not available
 * ({component_id=RTP, ip=IP, port=9080},{component_id=RTCP, ip=IP, port=9081}).
 * The default port starts at 7078 for the first component.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rawudp-stream-transmitter.h"

#include "fs-rawudp-component.h"

#include "fs-interfaces.h"

#include <gst/farsight/fs-candidate.h>
#include <gst/farsight/fs-conference-iface.h>

#include <gst/gst.h>

#include <string.h>


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
  PROP_SENDING,
  PROP_PREFERRED_LOCAL_CANDIDATES,
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

  /* This is an array of size n_components+1 */
  FsRawUdpComponent **component;

  gchar *stun_ip;
  guint stun_port;
  guint stun_timeout;

  GList *preferred_local_candidates;
  guint next_candidate_id;

  /* Everything below this line is protected by the mutex */
  GMutex *mutex;
  gboolean *candidates_prepared;
};

#define FS_RAWUDP_STREAM_TRANSMITTER_GET_PRIVATE(o)                     \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAWUDP_STREAM_TRANSMITTER, \
      FsRawUdpStreamTransmitterPrivate))

static void fs_rawudp_stream_transmitter_class_init (
    FsRawUdpStreamTransmitterClass *klass);
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
    FsStreamTransmitter *streamtransmitter,
    FsCandidate *candidate,
    GError **error);
static gboolean fs_rawudp_stream_transmitter_gather_local_candidates (
    FsStreamTransmitter *streamtransmitter,
    GError **error);

static FsCandidate* fs_rawudp_stream_transmitter_build_forced_candidate (
    FsRawUdpStreamTransmitter *self,
    const char *ip,
    gint port,
    guint component_id);

static void
_component_new_local_candidate (FsRawUdpComponent *component,
    FsCandidate *candidate, gpointer user_data);
static void
_component_local_candidates_prepared (FsRawUdpComponent *component,
    gpointer user_data);
static void
_component_new_active_candidate_pair (FsRawUdpComponent *component,
    FsCandidate *local, FsCandidate *remote, gpointer user_data);



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

  fs_rawudp_component_register_type (module);

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
  streamtransmitterclass->gather_local_candidates =
    fs_rawudp_stream_transmitter_gather_local_candidates;

  g_object_class_override_property (gobject_class, PROP_SENDING, "sending");
  g_object_class_override_property (gobject_class,
      PROP_PREFERRED_LOCAL_CANDIDATES, "preferred-local-candidates");

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

  self->priv->mutex = g_mutex_new ();
}

static void
fs_rawudp_stream_transmitter_dispose (GObject *object)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (object);
  gint c;

  if (self->priv->disposed)
    /* If dispose did already run, return. */
    return;

  if (self->priv->component)
  {
    for (c = 1; c <= self->priv->transmitter->components; c++)
    {
      if (self->priv->component[c])
      {
        g_object_unref (self->priv->component[c]);
        self->priv->component[c] = NULL;
      }
    }
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rawudp_stream_transmitter_finalize (GObject *object)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (object);

  g_free (self->priv->stun_ip);

  if (self->priv->preferred_local_candidates)
    fs_candidate_list_destroy (self->priv->preferred_local_candidates);

  if (self->priv->component)
  {
    g_free (self->priv->component);
    self->priv->component = NULL;
  }

  g_mutex_free (self->priv->mutex);

  g_free (self->priv->candidates_prepared);

  parent_class->finalize (object);
}

static void
fs_rawudp_stream_transmitter_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_SENDING:
      g_value_set_boolean (value, self->priv->sending);
      break;
    case PROP_PREFERRED_LOCAL_CANDIDATES:
      g_value_set_boxed (value, self->priv->preferred_local_candidates);
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

  switch (prop_id)
  {
    case PROP_SENDING:
      {
        gint c;

        self->priv->sending = g_value_get_boolean (value);

        for (c = 1; c <= self->priv->transmitter->components; c++)
          if (self->priv->component[c])
            g_object_set_property (G_OBJECT (self->priv->component[c]),
                "sending", value);
      }
      break;
    case PROP_PREFERRED_LOCAL_CANDIDATES:
      self->priv->preferred_local_candidates = g_value_dup_boxed (value);
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
  const gchar **ips = g_new0 (const gchar *,
      self->priv->transmitter->components + 1);
  guint16 *ports = g_new0 (guint16, self->priv->transmitter->components + 1);

  GList *item;
  gint c;
  guint16 next_port;

  self->priv->component = g_new0 (FsRawUdpComponent *,
      self->priv->transmitter->components + 1);
  self->priv->candidates_prepared = g_new0 (gboolean,
      self->priv->transmitter->components + 1);

  for (item = g_list_first (self->priv->preferred_local_candidates);
       item;
       item = g_list_next (item)) {
    FsCandidate *candidate = item->data;

    if (candidate->proto != FS_NETWORK_PROTOCOL_UDP) {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "You set preferred candidate of a type %d that is not"
          " FS_NETWORK_PROTOCOL_UDP",
          candidate->proto);
      goto error;
    }

    if (candidate->component_id == 0) {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "Component id 0 is invalid");
      goto error;
    }

    if (candidate->component_id > self->priv->transmitter->components)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "You specified an invalid component id %d with is higher"
          " than the maximum %d", candidate->component_id,
          self->priv->transmitter->components);
      goto error;
    }

    if (ips[candidate->component_id] || ports[candidate->component_id])
    {
      g_set_error (error, FS_ERROR,
          FS_ERROR_INVALID_ARGUMENTS,
          "You set more than one preferred local candidate for component %u",
          candidate->component_id);
      goto error;
    }

    /*
     * We should verify that the IP is valid now!!
     *
     */

    ips[candidate->component_id] = candidate->ip;
    if (candidate->port)
      ports[candidate->component_id] = candidate->port;
  }

  /* Lets make sure we start from a reasonnable value */
  if (ports[1] == 0)
    ports[1] = 7078;

  next_port = ports[1];

  for (c = 1; c <= self->priv->transmitter->components; c++)
  {
    gint requested_port = ports[c];
    guint used_port;

    if (!requested_port)
      requested_port = next_port;


    self->priv->component[c] = fs_rawudp_component_new (c,
        self->priv->transmitter,
        ips[c],
        requested_port,
        self->priv->stun_ip,
        self->priv->stun_port,
        self->priv->stun_timeout,
        &used_port,
        error);
    if (self->priv->component[c] == NULL)
      goto error;

    g_signal_connect (self->priv->component[c], "new-local-candidate",
        G_CALLBACK (_component_new_local_candidate), self);
    g_signal_connect (self->priv->component[c], "local-candidates-prepared",
        G_CALLBACK (_component_local_candidates_prepared), self);
    g_signal_connect (self->priv->component[c], "new-active-candidate-pair",
        G_CALLBACK (_component_new_active_candidate_pair), self);

    /* If we dont get the requested port and it wasnt a forced port,
     * then we rewind up to the last forced port and jump to the next
     * package of components, all non-forced ports must be consecutive!
     */

    if (used_port != requested_port  &&  !ports[c])
    {
      do {
        g_object_unref (self->priv->component[c]);
        self->priv->component[c] = NULL;

        c--;
      } while (!ports[c]);  /* Will always stop because ports[1] != 0 */
      ports[c] += self->priv->transmitter->components;
      next_port = ports[c];
      continue;
    }

    if (ips[c])
    {
      FsCandidate *forced =
        fs_rawudp_stream_transmitter_build_forced_candidate (self, ips[c],
            used_port, c);
      g_object_set (self->priv->component[c], "forced-candidate", forced, NULL);
      fs_candidate_destroy (forced);
    }

    next_port = used_port+1;
  }

  g_free (ips);
  g_free (ports);

  return TRUE;

 error:
  g_free (ips);
  g_free (ports);

  return FALSE;
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

  if (candidate->proto != FS_NETWORK_PROTOCOL_UDP)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "You set a candidate of a type %d that is not  FS_NETWORK_PROTOCOL_UDP",
        candidate->proto);
    return FALSE;
  }

  if (!candidate->ip || !candidate->port)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "The candidate passed does not contain a valid ip or port");
    return FALSE;
  }

  if (candidate->component_id == 0 ||
      candidate->component_id > self->priv->transmitter->components)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "The candidate passed has has an invalid component id %u (not in [0,%u])",
        candidate->component_id, self->priv->transmitter->components);
    return FALSE;
  }

  /*
   * IMPROVE ME: We should probably check that the candidate's IP
   *  has the format x.x.x.x where x is [0,255] using GRegex, etc
   */


  if (!fs_rawudp_component_add_remote_candidate (
          self->priv->component[candidate->component_id],
          candidate, error))
    return FALSE;

  return TRUE;
}


FsRawUdpStreamTransmitter *
fs_rawudp_stream_transmitter_newv (FsRawUdpTransmitter *transmitter,
    guint n_parameters, GParameter *parameters, GError **error)
{
  FsRawUdpStreamTransmitter *streamtransmitter = NULL;

  streamtransmitter = g_object_newv (FS_TYPE_RAWUDP_STREAM_TRANSMITTER,
      n_parameters, parameters);

  if (!streamtransmitter)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not build the stream transmitter");
    return NULL;
  }

  streamtransmitter->priv->transmitter = transmitter;

  if (!fs_rawudp_stream_transmitter_build (streamtransmitter, error))
  {
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

static FsCandidate *
fs_rawudp_stream_transmitter_build_forced_candidate (
    FsRawUdpStreamTransmitter *self, const char *ip, gint port,
    guint component_id)
{
  FsCandidate *candidate;
  gchar *id;

  id = g_strdup_printf ("L%u",
      self->priv->next_candidate_id++);
  candidate = fs_candidate_new (id, component_id, FS_CANDIDATE_TYPE_HOST,
      FS_NETWORK_PROTOCOL_UDP, ip, port);
  g_free (id);

  return candidate;
}


static gboolean
fs_rawudp_stream_transmitter_gather_local_candidates (
    FsStreamTransmitter *streamtransmitter,
    GError **error)
{
  FsRawUdpStreamTransmitter *self =
    FS_RAWUDP_STREAM_TRANSMITTER (streamtransmitter);
  int c;

  for (c = 1; c <= self->priv->transmitter->components; c++)
    if (!fs_rawudp_component_gather_local_candidates (self->priv->component[c],
            error))
      return FALSE;

  return TRUE;
}

static void
_component_new_local_candidate (FsRawUdpComponent *component,
    FsCandidate *candidate, gpointer user_data)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (user_data);

  g_signal_emit_by_name (self, "new-local-candidate", candidate);
}

static void
_component_local_candidates_prepared (FsRawUdpComponent *component,
    gpointer user_data)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (user_data);
  guint component_id;
  guint c;
  gboolean emit = TRUE;

  g_object_get (component, "component", &component_id, NULL);

  g_mutex_lock (self->priv->mutex);
  self->priv->candidates_prepared[component_id] = TRUE;

  for (c = 1; c <= self->priv->transmitter->components; c++)
  {
    if (self->priv->candidates_prepared[c] == FALSE)
    {
      emit = FALSE;
      break;
    }
  }
  g_mutex_unlock (self->priv->mutex);

  if (emit)
    g_signal_emit_by_name (self, "local-candidates-prepared");
}

static void
_component_new_active_candidate_pair (FsRawUdpComponent *component,
    FsCandidate *local, FsCandidate *remote, gpointer user_data)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (user_data);

  g_signal_emit_by_name (self, "new-active-candidate-pair", local, remote);
}

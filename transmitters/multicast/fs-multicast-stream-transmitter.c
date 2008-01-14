/*
 * Farsight2 - Farsight Multicast UDP Stream Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-multicast-transmitter.c - A Farsight Multiast UDP stream transmitter
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
 * SECTION:fs-multicast-stream-transmitter
 * @short_description: A stream transmitter object for Multicast UDP
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

GST_DEBUG_CATEGORY_EXTERN (fs_multicast_transmitter_debug);
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
  PROP_SENDING,
  PROP_PREFERED_LOCAL_CANDIDATES
};

struct _FsMulticastStreamTransmitterPrivate
{
  gboolean disposed;

  /* We don't actually hold a ref to this,
   * But since our parent FsStream can not exist without its parent
   * FsSession, we should be safe
   */
  FsMulticastTransmitter *transmitter;

  gboolean sending;

  /*
   * We have at most of those per component (index 0 is unused)
   */
  FsCandidate **remote_candidate;

  FsCandidate **local_forced_candidate;
  FsCandidate **local_active_candidate;

  UdpPort **udpports;

  GList *prefered_local_candidates;

  guint next_candidate_id;

  GList *sources;
};

#define FS_MULTICAST_STREAM_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_MULTICAST_STREAM_TRANSMITTER, \
                                FsMulticastStreamTransmitterPrivate))

static void fs_multicast_stream_transmitter_class_init (FsMulticastStreamTransmitterClass *klass);
static void fs_multicast_stream_transmitter_init (FsMulticastStreamTransmitter *self);
static void fs_multicast_stream_transmitter_dispose (GObject *object);
static void fs_multicast_stream_transmitter_finalize (GObject *object);

static void fs_multicast_stream_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_multicast_stream_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static gboolean fs_multicast_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error);

static FsCandidate * fs_multicast_stream_transmitter_build_forced_candidate (
    FsMulticastStreamTransmitter *self, const char *ip, gint port,
    guint component_id);
static void fs_multicast_stream_transmitter_maybe_new_active_candidate_pair (
    FsMulticastStreamTransmitter *self, guint component_id);
static gboolean
fs_multicast_stream_transmitter_emit_local_candidates (
    FsMulticastStreamTransmitter *self, guint component_id);


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

static GType type = 0;

GType
fs_multicast_stream_transmitter_get_type (void)
{
  return type;
}

GType
fs_multicast_stream_transmitter_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsMulticastStreamTransmitterClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_multicast_stream_transmitter_class_init,
    NULL,
    NULL,
    sizeof (FsMulticastStreamTransmitter),
    0,
    (GInstanceInitFunc) fs_multicast_stream_transmitter_init
  };

  type = g_type_module_register_type (G_TYPE_MODULE (module),
    FS_TYPE_STREAM_TRANSMITTER, "FsMulticastStreamTransmitter", &info, 0);

  return type;
}

static void
fs_multicast_stream_transmitter_class_init (FsMulticastStreamTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsStreamTransmitterClass *streamtransmitterclass =
    FS_STREAM_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_multicast_stream_transmitter_set_property;
  gobject_class->get_property = fs_multicast_stream_transmitter_get_property;

  streamtransmitterclass->add_remote_candidate =
    fs_multicast_stream_transmitter_add_remote_candidate;

  g_object_class_override_property (gobject_class, PROP_SENDING, "sending");
  g_object_class_override_property (gobject_class,
    PROP_PREFERED_LOCAL_CANDIDATES, "prefered-local-candidates");

  gobject_class->dispose = fs_multicast_stream_transmitter_dispose;
  gobject_class->finalize = fs_multicast_stream_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsMulticastStreamTransmitterPrivate));
}

static void
fs_multicast_stream_transmitter_init (FsMulticastStreamTransmitter *self)
{
  /* member init */
  self->priv = FS_MULTICAST_STREAM_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->priv->sending = TRUE;
}

static void
fs_multicast_stream_transmitter_dispose (GObject *object)
{
  FsMulticastStreamTransmitter *self = FS_MULTICAST_STREAM_TRANSMITTER (object);
  gint c;

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  if (self->priv->sources) {
    g_list_foreach (self->priv->sources, (GFunc) g_source_remove, NULL);
    g_list_free (self->priv->sources);
    self->priv->sources = NULL;
  }


  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_multicast_stream_transmitter_finalize (GObject *object)
{
  FsMulticastStreamTransmitter *self = FS_MULTICAST_STREAM_TRANSMITTER (object);
  gint c; /* component_id */

  if (self->priv->prefered_local_candidates) {
    fs_candidate_list_destroy (self->priv->prefered_local_candidates);
    self->priv->prefered_local_candidates = NULL;
  }

  if (self->priv->remote_candidate) {
    for (c = 1; c <= self->priv->transmitter->components; c++) {
      if (self->priv->remote_candidate[c]) {
        if (self->priv->udpports && self->priv->udpports[c] &&
          self->priv->sending)
          fs_multicast_transmitter_udpport_remove_dest (self->priv->udpports[c],
            self->priv->remote_candidate[c]->ip,
            self->priv->remote_candidate[c]->port);
        fs_candidate_destroy (self->priv->remote_candidate[c]);
      }
    }

    g_free (self->priv->remote_candidate);
    self->priv->remote_candidate = NULL;
  }

  if (self->priv->udpports) {
    for (c = 1; c <= self->priv->transmitter->components; c++) {
      if (self->priv->udpports[c]) {
        fs_multicast_transmitter_put_udpport (self->priv->transmitter,
          self->priv->udpports[c]);
        self->priv->udpports[c] = NULL;
      }
    }

    g_free (self->priv->udpports);
    self->priv->udpports = NULL;
  }

  if (self->priv->local_forced_candidate) {
    for (c = 1; c <= self->priv->transmitter->components; c++) {
      if (self->priv->local_forced_candidate[c]) {
        fs_candidate_destroy (self->priv->local_forced_candidate[c]);
        self->priv->local_forced_candidate[c] = NULL;
      }
    }
    g_free (self->priv->local_forced_candidate);
    self->priv->local_forced_candidate = NULL;
  }

  if (self->priv->local_active_candidate) {
    for (c = 1; c <= self->priv->transmitter->components; c++) {
      if (self->priv->local_active_candidate[c]) {
        fs_candidate_destroy (self->priv->local_active_candidate[c]);
        self->priv->local_active_candidate[c] = NULL;
      }
    }
    g_free (self->priv->local_active_candidate);
    self->priv->local_active_candidate = NULL;
  }

  parent_class->finalize (object);
}

static void
fs_multicast_stream_transmitter_get_property (GObject *object,
                                           guint prop_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  FsMulticastStreamTransmitter *self = FS_MULTICAST_STREAM_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_SENDING:
      g_value_set_boolean (value, self->priv->sending);
      break;
    case PROP_PREFERED_LOCAL_CANDIDATES:
      g_value_set_boxed (value, self->priv->prefered_local_candidates);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_multicast_stream_transmitter_set_property (GObject *object,
                                           guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec)
{
  FsMulticastStreamTransmitter *self = FS_MULTICAST_STREAM_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_SENDING:
      {
        gboolean old_sending = self->priv->sending;
        gint c;

        self->priv->sending = g_value_get_boolean (value);

        if (self->priv->sending != old_sending) {
          if (self->priv->sending) {

            for (c = 1; c <= self->priv->transmitter->components; c++)
              if (self->priv->remote_candidate[c])
                fs_multicast_transmitter_udpport_add_dest (
                    self->priv->udpports[c],
                    self->priv->remote_candidate[c]->ip,
                    self->priv->remote_candidate[c]->port);
          } else {

            for (c = 1; c <= self->priv->transmitter->components; c++)
              if (self->priv->remote_candidate[c])
                fs_multicast_transmitter_udpport_remove_dest (
                    self->priv->udpports[c],
                    self->priv->remote_candidate[c]->ip,
                    self->priv->remote_candidate[c]->port);
          }
        }
      }
      break;
    case PROP_PREFERED_LOCAL_CANDIDATES:
      self->priv->prefered_local_candidates = g_value_dup_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
fs_multicast_stream_transmitter_build (FsMulticastStreamTransmitter *self,
  GError **error)
{
  const gchar **ips = g_new0 (const gchar *,
    self->priv->transmitter->components + 1);
  guint16 *ports = g_new0 (guint16, self->priv->transmitter->components + 1);

  GList *item;
  gint c;
  guint16 next_port;

  self->priv->udpports = g_new0 (UdpPort *,
    self->priv->transmitter->components + 1);
  self->priv->remote_candidate = g_new0 (FsCandidate *,
    self->priv->transmitter->components + 1);
  self->priv->local_forced_candidate = g_new0 (FsCandidate *,
    self->priv->transmitter->components + 1);
  self->priv->local_active_candidate = g_new0 (FsCandidate *,
    self->priv->transmitter->components + 1);

  for (item = g_list_first (self->priv->prefered_local_candidates);
       item;
       item = g_list_next (item)) {
    FsCandidate *candidate = item->data;

    if (candidate->proto != FS_NETWORK_PROTOCOL_UDP) {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "You set prefered candidate of a type %d that is not"
        " FS_NETWORK_PROTOCOL_UDP",
        candidate->proto);
      goto error;
    }

    if (candidate->component_id == 0) {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Component id 0 is invalid");
      goto error;
    }

    if (candidate->component_id > self->priv->transmitter->components) {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "You specified an invalid component id %d with is higher"
        " than the maximum %d", candidate->component_id,
        self->priv->transmitter->components);
      goto error;
    }

    if (ips[candidate->component_id] || ports[candidate->component_id]) {
      g_set_error (error, FS_ERROR,
        FS_ERROR_INVALID_ARGUMENTS,
        "You set more than one prefered local candidate for component %u",
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

  for (c = 1; c <= self->priv->transmitter->components; c++) {
    gint requested_port = ports[c];
    gint used_port;

    if (!requested_port)
      requested_port = next_port;

    self->priv->udpports[c] =
      fs_multicast_transmitter_get_udpport (self->priv->transmitter, c, ips[c],
        requested_port, error);
    if (!self->priv->udpports[c])
      goto error;

    used_port = fs_multicast_transmitter_udpport_get_port (self->priv->udpports[c]);

    /* If we dont get the requested port and it wasnt a forced port,
     * then we rewind up to the last forced port and jump to the next
     * package of components, all non-forced ports must be consecutive!
     */

    if (used_port != requested_port  &&  !ports[c]) {
      do {
        fs_multicast_transmitter_put_udpport (self->priv->transmitter,
          self->priv->udpports[c]);
        self->priv->udpports[c] = NULL;

        if (self->priv->local_forced_candidate[c]) {
          fs_candidate_destroy (self->priv->local_forced_candidate[c]);
          self->priv->local_forced_candidate[c] = NULL;
        }

        c--;
      } while (!ports[c]);  /* Will always stop because ports[1] != 0 */
      ports[c] += self->priv->transmitter->components;
      next_port = ports[c];
      continue;
    }

    if (ips[c])
      self->priv->local_forced_candidate[c] =
        fs_multicast_stream_transmitter_build_forced_candidate (self, ips[c],
          used_port, c);

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
 * fs_multicast_stream_transmitter_add_remote_candidate
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
fs_multicast_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error)
{
  FsMulticastStreamTransmitter *self =
    FS_MULTICAST_STREAM_TRANSMITTER (streamtransmitter);

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

  if (candidate->component_id == 0 ||
    candidate->component_id > self->priv->transmitter->components) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "The candidate passed has has an invalid component id %u (not in [0,%u])",
      candidate->component_id, self->priv->transmitter->components);
    return FALSE;
  }

  /*
   * IMPROVE ME: We should probably check that the candidate's IP
   *  has the format x.x.x.x where x is [0,255] using GRegex, etc
   */
  if (self->priv->sending) {
    fs_multicast_transmitter_udpport_add_dest (
        self->priv->udpports[candidate->component_id],
        candidate->ip, candidate->port);
  }
  if (self->priv->remote_candidate[candidate->component_id]) {
    fs_multicast_transmitter_udpport_remove_dest (
        self->priv->udpports[candidate->component_id],
        self->priv->remote_candidate[candidate->component_id]->ip,
        self->priv->remote_candidate[candidate->component_id]->port);
    fs_candidate_destroy (
        self->priv->remote_candidate[candidate->component_id]);
  }
  self->priv->remote_candidate[candidate->component_id] =
    fs_candidate_copy (candidate);

  fs_multicast_stream_transmitter_maybe_new_active_candidate_pair (self,
    candidate->component_id);

  return TRUE;
}


FsMulticastStreamTransmitter *
fs_multicast_stream_transmitter_newv (FsMulticastTransmitter *transmitter,
  guint n_parameters, GParameter *parameters, GError **error)
{
  FsMulticastStreamTransmitter *streamtransmitter = NULL;

  streamtransmitter = g_object_newv (FS_TYPE_MULTICAST_STREAM_TRANSMITTER,
    n_parameters, parameters);

  if (!streamtransmitter) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not build the stream transmitter");
    return NULL;
  }

  streamtransmitter->priv->transmitter = transmitter;

  if (!fs_multicast_stream_transmitter_build (streamtransmitter, error)) {
    g_object_unref (streamtransmitter);
    return NULL;
  }

  return streamtransmitter;
}

struct CandidateTransit {
  FsMulticastStreamTransmitter *self;
  FsCandidate *candidate;
  guint component_id;
};

static FsCandidate *
fs_multicast_stream_transmitter_build_forced_candidate (
    FsMulticastStreamTransmitter *self, const char *ip, gint port,
    guint component_id)
{
  FsCandidate *candidate = g_new0 (FsCandidate, 1);

  candidate = g_new0 (FsCandidate,1);
  candidate->candidate_id = g_strdup_printf ("L%u",
    self->priv->next_candidate_id++);
  candidate->component_id = component_id;
  candidate->ip = g_strdup (ip);
  candidate->port = port;
  candidate->proto = FS_NETWORK_PROTOCOL_UDP;
  candidate->type = FS_CANDIDATE_TYPE_HOST;

  return candidate;
}

static gboolean
fs_multicast_stream_transmitter_emit_local_candidates (
    FsMulticastStreamTransmitter *self, guint component_id)
{
  GList *ips = NULL;
  GList *current;
  guint port;

  if (component_id > self->priv->transmitter->components) {
    gchar *text = g_strdup_printf ("Internal error: invalid component %d",
      component_id);
    fs_stream_transmitter_emit_error (FS_STREAM_TRANSMITTER (self),
      FS_ERROR_INVALID_ARGUMENTS, text, text);
    g_free (text);
    return FALSE;
  }

  if (self->priv->local_forced_candidate[component_id]) {
    self->priv->local_active_candidate[component_id] = fs_candidate_copy (
        self->priv->local_forced_candidate[component_id]);
    g_signal_emit_by_name (self, "new-local-candidate",
      self->priv->local_forced_candidate[component_id]);
    fs_multicast_stream_transmitter_maybe_new_active_candidate_pair (self,
      component_id);
    return TRUE;
  }

  port = fs_multicast_transmitter_udpport_get_port (
      self->priv->udpports[component_id]);

  ips = farsight_get_local_ips (TRUE);

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
    candidate->type = FS_CANDIDATE_TYPE_HOST;

    g_signal_emit_by_name (self, "new-local-candidate", candidate);

    self->priv->local_active_candidate[component_id] = candidate;

    /* FIXME: Emit only the first candidate ?? */
    break;
  }

  /* free list of ips */
  g_list_foreach (ips, (GFunc) g_free, NULL);
  g_list_free (ips);

  if (!self->priv->local_active_candidate[component_id])
  {
    gchar *text = g_strdup_printf (
        "We have no local candidate for component %d", component_id);
    fs_stream_transmitter_emit_error (FS_STREAM_TRANSMITTER (self),
        FS_ERROR_NETWORK, "Could not generate local candidate", text);
    g_free (text);
    return FALSE;
  }

  fs_multicast_stream_transmitter_maybe_new_active_candidate_pair (self,
    component_id);

  return TRUE;
}

static gboolean
fs_multicast_stream_transmitter_no_stun (gpointer user_data)
{
  FsMulticastStreamTransmitter *self = user_data;
  GSource *source;
  gint c;

  /* If we have a STUN'd candidate, dont send the locally generated
   * ones */

  for (c = 1; c <= self->priv->transmitter->components; c++) {
    if (!self->priv->local_active_candidate[c]) {
      if (!fs_multicast_stream_transmitter_emit_local_candidates (self, c))
        return FALSE;
    }
    g_assert (self->priv->local_active_candidate[c]);
  }

  g_signal_emit_by_name (self, "local-candidates-prepared");

  /* Lets remove this source from the list of sources to destroy
   * For the case when its called from an idle source
   */
  source = g_main_current_source ();
  if (source)  {
    guint id = g_source_get_id (source);

    self->priv->sources = g_list_remove (self->priv->sources,
      GUINT_TO_POINTER (id));
  }

  return FALSE;
}

static void
fs_multicast_stream_transmitter_maybe_new_active_candidate_pair (
    FsMulticastStreamTransmitter *self, guint component_id)
{
  if (self->priv->local_active_candidate[component_id] &&
    self->priv->remote_candidate[component_id]) {

    g_signal_emit_by_name (self, "new-active-candidate-pair",
      self->priv->local_active_candidate[component_id],
      self->priv->remote_candidate[component_id]);
  }
}

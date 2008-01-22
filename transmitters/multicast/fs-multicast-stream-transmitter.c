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
 * @see_also: fs-rawudp-stream-transmitter
 *
 * <refsect2>
 * <para>
 * The multicast transmitter allows data to be sent over and received from
 * multicasted UDP on IPv4.
 * </para>
 *
 * <para>
 * This stream transmitter never emits local candidates. It will listen
 * to the port specified in the remote candidate. And will also send to that
 * port. It accepts only a single remote candidate per component, if a new one
 * is given, it will replace the previous one for that component.
 * </para>
 *
 * <para>
 * The transmitter will only stop sending to a multicast group when all of its
 * StreamTransmitters that have this multicast group as destination have their
 * "sending" property set to false. Multiple stream transmitters can point to
 * the same multicast groups from the same Transmitter (session), and only one
 * copy of each packet will be received.
 * </para>
 *
 * <para>
 * It will only listen to and send from the IP specified in the
 * prefered-local-candidates. There can be only one prefered candidate per
 * component.
 * </para>
 *
 * <para>
 * Packets sent will be looped back (so that other clients on the same session
 * can be on the same machine.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-multicast-stream-transmitter.h"
#include "fs-multicast-transmitter.h"

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
  PROP_PREFERRED_LOCAL_CANDIDATES
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
  FsCandidate **local_candidate;

  UdpMulticastGroup **recvmcasts;
  UdpMulticastGroup **sendmcasts;

  GList *preferred_local_candidates;

  guint next_candidate_id;
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
    PROP_PREFERRED_LOCAL_CANDIDATES, "preferred-local-candidates");

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

  if (self->priv->disposed)
    /* If dispose did already run, return. */
    return;

  if (self->priv->sendmcasts)
  {
    for (c = 1; c <= self->priv->transmitter->components; c++)
    {
      if (self->priv->sendmcasts[c])
      {
        if (self->priv->sending)
          fs_multicast_transmitter_group_dec_sending (
              self->priv->sendmcasts[c]);
        fs_multicast_transmitter_put_group (self->priv->transmitter,
            self->priv->sendmcasts[c]);
        self->priv->sendmcasts[c] = NULL;
      }
    }
  }

  if (self->priv->recvmcasts)
  {
    for (c = 1; c <= self->priv->transmitter->components; c++)
    {
      if (self->priv->recvmcasts[c])
      {
        fs_multicast_transmitter_put_group (self->priv->transmitter,
            self->priv->recvmcasts[c]);
        self->priv->recvmcasts[c] = NULL;
      }
    }
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

  if (self->priv->preferred_local_candidates)
  {
    fs_candidate_list_destroy (self->priv->preferred_local_candidates);
    self->priv->preferred_local_candidates = NULL;
  }

  if (self->priv->remote_candidate)
  {
    for (c = 1; c <= self->priv->transmitter->components; c++)
    {
      if (self->priv->remote_candidate[c])
        fs_candidate_destroy (self->priv->remote_candidate[c]);
      self->priv->remote_candidate[c] = NULL;
    }
    g_free (self->priv->remote_candidate);
    self->priv->remote_candidate = NULL;
  }

  g_free (self->priv->sendmcasts);
  self->priv->sendmcasts = NULL;

  g_free (self->priv->recvmcasts);
  self->priv->recvmcasts = NULL;

  parent_class->finalize (object);
}

static void
fs_multicast_stream_transmitter_get_property (GObject *object,
                                           guint prop_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  FsMulticastStreamTransmitter *self = FS_MULTICAST_STREAM_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_SENDING:
      g_value_set_boolean (value, self->priv->sending);
      break;
    case PROP_PREFERRED_LOCAL_CANDIDATES:
      g_value_set_boxed (value, self->priv->preferred_local_candidates);
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

        if (self->priv->sending != old_sending)
          for (c = 1; c <= self->priv->transmitter->components; c++)
            if (self->priv->sendmcasts[c])
            {
              if (self->priv->sending)
                fs_multicast_transmitter_group_inc_sending (
                    self->priv->sendmcasts[c]);
              else
                fs_multicast_transmitter_group_dec_sending (
                    self->priv->sendmcasts[c]);
            }
      }
      break;
    case PROP_PREFERRED_LOCAL_CANDIDATES:
      self->priv->preferred_local_candidates = g_value_dup_boxed (value);
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
  GList *item;
  gint c;

  self->priv->sendmcasts = g_new0 (UdpMulticastGroup *,
      self->priv->transmitter->components + 1);
  self->priv->recvmcasts = g_new0 (UdpMulticastGroup *,
      self->priv->transmitter->components + 1);
  self->priv->local_candidate = g_new0 (FsCandidate *,
      self->priv->transmitter->components + 1);
  self->priv->remote_candidate = g_new0 (FsCandidate *,
      self->priv->transmitter->components + 1);

  for (item = g_list_first (self->priv->preferred_local_candidates);
       item;
       item = g_list_next (item))
  {
    FsCandidate *candidate = item->data;

    if (candidate->proto != FS_NETWORK_PROTOCOL_UDP)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "You set preferred candidate of a type %d that is not"
        " FS_NETWORK_PROTOCOL_UDP",
        candidate->proto);
      return FALSE;
    }

    if (candidate->component_id == 0)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Component id 0 is invalid");
      return FALSE;
    }

    if (candidate->component_id > self->priv->transmitter->components)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "You specified an invalid component id %d with is higher"
        " than the maximum %d", candidate->component_id,
        self->priv->transmitter->components);
      return FALSE;
    }

    if (self->priv->local_candidate[candidate->component_id])
    {
      g_set_error (error, FS_ERROR,
          FS_ERROR_INVALID_ARGUMENTS,
          "You set more than one preferred local candidate for component %u",
          candidate->component_id);
      return FALSE;
    }

    if (candidate->ip == NULL)
    {
      g_set_error (error, FS_ERROR,
          FS_ERROR_INVALID_ARGUMENTS,
          "You have not set the local ip address for the preferred candidate"
          " for this component");
      return FALSE;
    }

    self->priv->local_candidate[candidate->component_id] = candidate;
  }

  for (c = 1; c <= self->priv->transmitter->components; c++)
  {
    if (!self->priv->local_candidate[c])
    {
      self->priv->local_candidate[c] = fs_candidate_new (NULL, c,
          FS_CANDIDATE_TYPE_MULTICAST, FS_NETWORK_PROTOCOL_UDP, NULL, 0);
    }
  }

  return TRUE;
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
  UdpMulticastGroup *newrecvmcast = NULL;
  UdpMulticastGroup *newsendmcast = NULL;

  if (candidate->proto != FS_NETWORK_PROTOCOL_UDP) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "You set a candidate of a type %d that is not  FS_NETWORK_PROTOCOL_UDP",
      candidate->proto);
    return FALSE;
  }

  if (candidate->type != FS_CANDIDATE_TYPE_MULTICAST)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "The remote candidate is not of the right type, it should be"
        " FS_ERROR_INVALID_ARGUMENTS, but it is %d", candidate->type);
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

  if (candidate->ttl == 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "The TTL for IPv4 multicast candidates must not be 0");
    return FALSE;
  }

  if (self->priv->remote_candidate[candidate->component_id])
  {
    FsCandidate *old_candidate =
      self->priv->remote_candidate[candidate->component_id];
    if (old_candidate->port == candidate->port &&
        old_candidate->ttl == candidate->ttl &&
        !strcmp (old_candidate->ip, candidate->ip))
    {
      GST_DEBUG ("Re-set the same candidate, ignoring");
      return TRUE;
    }
    fs_candidate_destroy (old_candidate);
    self->priv->remote_candidate[candidate->component_id] = NULL;
  }

  /*
   * IMPROVE ME: We should probably check that the candidate's IP
   *  has the format x.x.x.x where x is [0,255] using GRegex, etc
   * We should also check if the address is in the multicast range
   */

  newrecvmcast = fs_multicast_transmitter_get_group (self->priv->transmitter,
      candidate->component_id, candidate->ip, candidate->port,
      self->priv->local_candidate[candidate->component_id]->ip,
      candidate->ttl, TRUE, error);

  if (!newrecvmcast)
    return FALSE;

  newsendmcast = fs_multicast_transmitter_get_group (self->priv->transmitter,
      candidate->component_id, candidate->ip, candidate->port,
      self->priv->local_candidate[candidate->component_id]->ip,
      candidate->ttl, FALSE, error);

  if (!newsendmcast)
  {
    fs_multicast_transmitter_put_group (self->priv->transmitter, newrecvmcast);
    return FALSE;
  }

  if (self->priv->sendmcasts[candidate->component_id])
  {
    if (self->priv->sending)
      fs_multicast_transmitter_group_dec_sending (
          self->priv->sendmcasts[candidate->component_id]);
    fs_multicast_transmitter_put_group (self->priv->transmitter,
        self->priv->sendmcasts[candidate->component_id]);
  }

  if (self->priv->recvmcasts[candidate->component_id])
    fs_multicast_transmitter_put_group (self->priv->transmitter,
        self->priv->recvmcasts[candidate->component_id]);

  self->priv->sendmcasts[candidate->component_id] = newsendmcast;
  self->priv->recvmcasts[candidate->component_id] = newrecvmcast;

  if (self->priv->sending)
    fs_multicast_transmitter_group_inc_sending (
        self->priv->sendmcasts[candidate->component_id]);

  self->priv->remote_candidate[candidate->component_id] =
    fs_candidate_copy (candidate);

  self->priv->local_candidate[candidate->component_id]->port = candidate->port;


  g_signal_emit_by_name (self, "new-active-candidate-pair",
      self->priv->local_candidate[candidate->component_id],
      self->priv->remote_candidate[candidate->component_id]);

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

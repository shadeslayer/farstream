/*
 * Farsight2 - Farsight Shared Memory Stream Transmitter
 *
 * Copyright 2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2009 Nokia Corp.
 *
 * fs-shm-stream-transmitter.c - A Farsight Shared memory stream transmitter
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
 * SECTION:fs-shm-stream-transmitter
 * @short_description: A stream transmitter object for Shared Memory
 *
 *
 * The name of this transmitter is "shm".
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-shm-stream-transmitter.h"
#include "fs-shm-transmitter.h"

#include <gst/farsight/fs-candidate.h>
#include <gst/farsight/fs-conference-iface.h>

#include <gst/gst.h>

#include <string.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

GST_DEBUG_CATEGORY_EXTERN (fs_shm_transmitter_debug);
#define GST_CAT_DEFAULT fs_shm_transmitter_debug

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

struct _FsShmStreamTransmitterPrivate
{
  gboolean disposed;

  /* We don't actually hold a ref to this,
   * But since our parent FsStream can not exist without its parent
   * FsSession, we should be safe
   */
  FsShmTransmitter *transmitter;

  GMutex *mutex;

  /* Protected by the mutex */
  gboolean sending;

  /*
   * We have at most of those per component (index 0 is unused)
   */
  FsCandidate **local_candidate;
  /* Protected by the mutex */
  FsCandidate **remote_candidate;

  GList *preferred_local_candidates;
};

#define FS_SHM_STREAM_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_SHM_STREAM_TRANSMITTER, \
                                FsShmStreamTransmitterPrivate))

#define FS_SHM_STREAM_TRANSMITTER_LOCK(s) \
  g_mutex_lock ((s)->priv->mutex)
#define FS_SHM_STREAM_TRANSMITTER_UNLOCK(s) \
  g_mutex_unlock ((s)->priv->mutex)

static void fs_shm_stream_transmitter_class_init (FsShmStreamTransmitterClass *klass);
static void fs_shm_stream_transmitter_init (FsShmStreamTransmitter *self);
static void fs_shm_stream_transmitter_dispose (GObject *object);
static void fs_shm_stream_transmitter_finalize (GObject *object);

static void fs_shm_stream_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_shm_stream_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static gboolean fs_shm_stream_transmitter_set_remote_candidates (
    FsStreamTransmitter *streamtransmitter, GList *candidates,
    GError **error);


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

static GType type = 0;

GType
fs_shm_stream_transmitter_get_type (void)
{
  return type;
}

GType
fs_shm_stream_transmitter_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsShmStreamTransmitterClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_shm_stream_transmitter_class_init,
    NULL,
    NULL,
    sizeof (FsShmStreamTransmitter),
    0,
    (GInstanceInitFunc) fs_shm_stream_transmitter_init
  };

  type = g_type_module_register_type (G_TYPE_MODULE (module),
    FS_TYPE_STREAM_TRANSMITTER, "FsShmStreamTransmitter", &info, 0);

  return type;
}

static void
fs_shm_stream_transmitter_class_init (FsShmStreamTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsStreamTransmitterClass *streamtransmitterclass =
    FS_STREAM_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_shm_stream_transmitter_set_property;
  gobject_class->get_property = fs_shm_stream_transmitter_get_property;

  streamtransmitterclass->set_remote_candidates =
    fs_shm_stream_transmitter_set_remote_candidates;

  g_object_class_override_property (gobject_class, PROP_SENDING, "sending");
  g_object_class_override_property (gobject_class,
    PROP_PREFERRED_LOCAL_CANDIDATES, "preferred-local-candidates");

  gobject_class->dispose = fs_shm_stream_transmitter_dispose;
  gobject_class->finalize = fs_shm_stream_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsShmStreamTransmitterPrivate));
}

static void
fs_shm_stream_transmitter_init (FsShmStreamTransmitter *self)
{
  /* member init */
  self->priv = FS_SHM_STREAM_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->priv->sending = TRUE;

  self->priv->mutex = g_mutex_new ();
}

static void
fs_shm_stream_transmitter_dispose (GObject *object)
{
  FsShmStreamTransmitter *self = FS_SHM_STREAM_TRANSMITTER (object);

  if (self->priv->disposed)
    /* If dispose did already run, return. */
    return;

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_shm_stream_transmitter_finalize (GObject *object)
{
  FsShmStreamTransmitter *self = FS_SHM_STREAM_TRANSMITTER (object);
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

  if (self->priv->local_candidate)
  {
    for (c = 1; c <= self->priv->transmitter->components; c++)
    {
      if (self->priv->local_candidate[c])
        fs_candidate_destroy (self->priv->local_candidate[c]);
      self->priv->local_candidate[c] = NULL;
    }
    g_free (self->priv->local_candidate);
    self->priv->local_candidate = NULL;
  }

  g_mutex_free (self->priv->mutex);

  parent_class->finalize (object);
}

static void
fs_shm_stream_transmitter_get_property (GObject *object,
                                           guint prop_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  FsShmStreamTransmitter *self = FS_SHM_STREAM_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_SENDING:
      FS_SHM_STREAM_TRANSMITTER_LOCK (self);
      g_value_set_boolean (value, self->priv->sending);
      FS_SHM_STREAM_TRANSMITTER_UNLOCK (self);
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
fs_shm_stream_transmitter_set_property (GObject *object,
                                           guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec)
{
  FsShmStreamTransmitter *self = FS_SHM_STREAM_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_SENDING:
      {
        FS_SHM_STREAM_TRANSMITTER_LOCK (self);
        self->priv->sending = g_value_get_boolean (value);

        FS_SHM_STREAM_TRANSMITTER_UNLOCK (self);

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
fs_shm_stream_transmitter_build (FsShmStreamTransmitter *self,
  GError **error)
{
  GList *item;
  gint c;

  self->priv->local_candidate = g_new0 (FsCandidate *,
      self->priv->transmitter->components + 1);
  self->priv->remote_candidate = g_new0 (FsCandidate *,
      self->priv->transmitter->components + 1);

  for (item = g_list_first (self->priv->preferred_local_candidates);
       item;
       item = g_list_next (item))
  {
    FsCandidate *candidate = item->data;

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

    self->priv->local_candidate[candidate->component_id] =
      fs_candidate_copy (candidate);
  }

  for (c = 1; c <= self->priv->transmitter->components; c++)
  {
    if (!self->priv->local_candidate[c])
    {
      self->priv->local_candidate[c] = fs_candidate_new (NULL, c,
          FS_CANDIDATE_TYPE_HOST, FS_NETWORK_PROTOCOL_UDP, NULL, 0);
    }
  }

  return TRUE;
}


static gboolean
fs_shm_stream_transmitter_add_remote_candidate (
    FsShmStreamTransmitter *self, FsCandidate *candidate,
    GError **error)
{
  g_signal_emit_by_name (self, "new-active-candidate-pair",
      self->priv->local_candidate[candidate->component_id],
      self->priv->remote_candidate[candidate->component_id]);

  return TRUE;
}

/**
 * fs_shm_stream_transmitter_set_remote_candidates
 */

static gboolean
fs_shm_stream_transmitter_set_remote_candidates (
    FsStreamTransmitter *streamtransmitter, GList *candidates,
    GError **error)
{
  GList *item = NULL;
  FsShmStreamTransmitter *self =
    FS_SHM_STREAM_TRANSMITTER (streamtransmitter);

  for (item = candidates; item; item = g_list_next (item))
  {
    FsCandidate *candidate = item->data;

    if (candidate->component_id == 0 ||
        candidate->component_id > self->priv->transmitter->components) {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The candidate passed has an invalid component id %u (not in [1,%u])",
          candidate->component_id, self->priv->transmitter->components);
      return FALSE;
    }
  }

  for (item = candidates; item; item = g_list_next (item))
    if (!fs_shm_stream_transmitter_add_remote_candidate (self,
            item->data, error))
      return FALSE;


  return TRUE;
}


FsShmStreamTransmitter *
fs_shm_stream_transmitter_newv (FsShmTransmitter *transmitter,
  guint n_parameters, GParameter *parameters, GError **error)
{
  FsShmStreamTransmitter *streamtransmitter = NULL;

  streamtransmitter = g_object_newv (FS_TYPE_SHM_STREAM_TRANSMITTER,
    n_parameters, parameters);

  if (!streamtransmitter) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not build the stream transmitter");
    return NULL;
  }

  streamtransmitter->priv->transmitter = transmitter;

  if (!fs_shm_stream_transmitter_build (streamtransmitter, error)) {
    g_object_unref (streamtransmitter);
    return NULL;
  }

  return streamtransmitter;
}

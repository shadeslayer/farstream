/*
 * Farsight2 - Farsight Stream Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-stream-transmitter.c - A Farsight Stream Transmitter gobject
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
 * SECTION:fs-stream-transmitter
 * @short_description: A stream transmitter object used to convey per-stream
 *   information to a transmitter.
 *
 * This object is the base implementation of a Farsight Stream Transmitter.
 * It needs to be derived and implement by a Farsight transmitter.
 * A Farsight Stream transmitter is used to convery per-stream information
 * to a transmitter, this is mostly local and remote candidates
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-marshal.h"
#include "fs-stream-transmitter.h"

#include "fs-conference-iface.h"
#include "fs-private.h"

#include <gst/gst.h>

#define GST_CAT_DEFAULT fs_base_conference_debug

/* Signals */
enum
{
  ERROR_SIGNAL,
  NEW_LOCAL_CANDIDATE,
  NEW_ACTIVE_CANDIDATE_PAIR,
  LOCAL_CANDIDATES_PREPARED,
  KNOWN_SOURCE_PACKET_RECEIVED,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_SENDING,
  PROP_PREFERRED_LOCAL_CANDIDATES,
  PROP_ASSOCIATE_ON_SOURCE
};

struct _FsStreamTransmitterPrivate
{
  gboolean disposed;
};

G_DEFINE_ABSTRACT_TYPE(FsStreamTransmitter, fs_stream_transmitter,
    G_TYPE_OBJECT);


#define FS_STREAM_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_STREAM_TRANSMITTER, \
                                FsStreamTransmitterPrivate))

static void fs_stream_transmitter_dispose (GObject *object);
static void fs_stream_transmitter_finalize (GObject *object);

static void fs_stream_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_stream_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_stream_transmitter_class_init (FsStreamTransmitterClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_stream_transmitter_set_property;
  gobject_class->get_property = fs_stream_transmitter_get_property;



  /**
   * FsStreamTransmitter:sending:
   *
   * A network source #GstElement to be used by the #FsSession
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_SENDING,
      g_param_spec_boolean ("sending",
        "Whether to send from this transmitter",
        "If set to FALSE, the transmitter will stop sending to this person",
        TRUE,
        G_PARAM_READWRITE));

  /**
   * FsStreamTransmitter:preferred-local-candidate:
   *
   * The list of preferred local candidates for this stream
   * It is a #GList of #FsCandidates
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_PREFERRED_LOCAL_CANDIDATES,
      g_param_spec_boxed ("preferred-local-candidates",
        "The preferred candidates",
        "A GList of FsCandidates",
        FS_TYPE_CANDIDATE_LIST,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  /**
   * FsStreamTransmitter:associate-on-source
   *
   * This tells the stream transmitter to associate incoming data with this
   * based on the source without looking at the content if possible.
   *
   */

  g_object_class_install_property (gobject_class,
      PROP_ASSOCIATE_ON_SOURCE,
      g_param_spec_boolean ("associate-on-source",
        "Associate incoming data based on the source address",
        "Whether to associate incoming data stream based on the source address",
        TRUE,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  /**
   * FsStreamTransmitter::error:
   * @self: #FsStreamTransmitter that emitted the signal
   * @errorno: The number of the error
   * @error_msg: Error message to be displayed to user
   * @debug_msg: Debugging error message
   *
   * This signal is emitted in any error condition
   *
   */
  signals[ERROR_SIGNAL] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_marshal_VOID__ENUM_STRING_STRING,
      G_TYPE_NONE, 3, FS_TYPE_ERROR, G_TYPE_STRING, G_TYPE_STRING);

    /**
   * FsStreamTransmitter::new-active-candidate-pair:
   * @self: #FsStreamTransmitter that emitted the signal
   * @local_candidate: #FsCandidate of the local candidate being used
   * @remote_candidate: #FsCandidate of the remote candidate being used
   *
   * This signal is emitted when there is a new active chandidate pair that has
   * been established. This is specially useful for ICE where the active
   * candidate pair can change automatically due to network conditions. The user
   * must not modify the candidates and must copy them if he wants to use them
   * outside the callback scope.
   *
   */
  signals[NEW_ACTIVE_CANDIDATE_PAIR] = g_signal_new
    ("new-active-candidate-pair",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL,
        NULL,
        _fs_marshal_VOID__BOXED_BOXED,
        G_TYPE_NONE, 2, FS_TYPE_CANDIDATE, FS_TYPE_CANDIDATE);

 /**
   * FsStreamTransmitter::new-local-candidate:
   * @self: #FsStream that emitted the signal
   * @local_candidate: #FsCandidate of the local candidate
   *
   * This signal is emitted when a new local candidate is discovered.
   *
   */
  signals[NEW_LOCAL_CANDIDATE] = g_signal_new
    ("new-local-candidate",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__BOXED,
      G_TYPE_NONE, 1, FS_TYPE_CANDIDATE);

 /**
   * FsStreamTransmitter::local-candidates-prepared:
   * @self: #FsStreamTransmitter that emitted the signal
   *
   * This signal is emitted when all local candidates have been
   * prepared, an ICE implementation would send its SDP offer or answer.
   *
   */
  signals[LOCAL_CANDIDATES_PREPARED] = g_signal_new
    ("local-candidates-prepared",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

 /**
   * FsStreamTransmitter::known-source-packet-received:
   * @self: #FsStreamTransmitter that emitted the signal
   * @component: The Component on which this buffer was received
   * @buffer: the #GstBuffer coming from the known source
   *
   * This signal is emitted when a buffer coming from a confirmed known source
   * is received.
   *
   */
  signals[KNOWN_SOURCE_PACKET_RECEIVED] = g_signal_new
    ("known-source-packet-received",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_marshal_VOID__UINT_POINTER,
      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

  gobject_class->dispose = fs_stream_transmitter_dispose;
  gobject_class->finalize = fs_stream_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsStreamTransmitterPrivate));
}

static void
fs_stream_transmitter_init (FsStreamTransmitter *self)
{
  /* member init */
  self->priv = FS_STREAM_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_stream_transmitter_dispose (GObject *object)
{
  FsStreamTransmitter *self = FS_STREAM_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_stream_transmitter_finalize (GObject *object)
{
  parent_class->finalize (object);
}

static void
fs_stream_transmitter_get_property (GObject *object,
                                    guint prop_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
}

static void
fs_stream_transmitter_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
}


/**
 * fs_stream_transmitter_set_remote_candidates
 * @streamtransmitter: a #FsStreamTranmitter
 * @candidates: a #GList of the remote candidates
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function is used to set the remote candidates to the transmitter
 *
 * Returns: TRUE of the candidate could be added, FALSE if it couldnt
 *   (and the #GError will be set)
 */

gboolean
fs_stream_transmitter_set_remote_candidates (
    FsStreamTransmitter *streamtransmitter,
    GList *candidates,
    GError **error)
{
  FsStreamTransmitterClass *klass =
    FS_STREAM_TRANSMITTER_GET_CLASS (streamtransmitter);

  if (klass->set_remote_candidates) {
    return klass->set_remote_candidates (streamtransmitter, candidates, error);
  } else {
    g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
      "add_remote_candidate not defined in stream transmitter class");
  }

  return FALSE;
}

/**
 * fs_stream_transmitter_select_candidate_pair:
 * @streamtransmitter: a #FsStreamTransmitter
 * @local_foundation: The foundation of the local candidates to be selected
 * @remote_foundation: The foundation of the remote candidates to be selected
 * @error: location of a #GErrorh, or NULL if no error occured
 *
 * This function selects one pair of candidates to be selected to start
 * sending media on.
 *
 * Returns: TRUE if the candidate pair could be selected, FALSE otherwise
 */

gboolean
fs_stream_transmitter_select_candidate_pair (
    FsStreamTransmitter *streamtransmitter,
    const gchar *local_foundation,
    const gchar *remote_foundation,
    GError **error)
{
  FsStreamTransmitterClass *klass =
    FS_STREAM_TRANSMITTER_GET_CLASS (streamtransmitter);

  if (klass->select_candidate_pair) {
    return klass->select_candidate_pair (streamtransmitter,
        local_foundation, remote_foundation, error);
  } else {
    g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
      "select_candidate_pair not defined in stream transmitter class");
  }

  return FALSE;
}

/**
 * fs_stream_transmitter_gather_local_candidates:
 * @streamtransmitter: a #FsStreamTransmitter
 * @error: location of a #GErrorh, or NULL if no error occured
 *
 * This function tells the transmitter to start gathering local candidates,
 * signals for new candidates and newly active candidates can be emitted
 * during the call to this function.
 *
 * Returns: %TRUE if it succeeds (or is not implemented), %FALSE otherwise
 */

gboolean
fs_stream_transmitter_gather_local_candidates (
    FsStreamTransmitter *streamtransmitter,
    GError **error)
{
  FsStreamTransmitterClass *klass =
    FS_STREAM_TRANSMITTER_GET_CLASS (streamtransmitter);

  if (klass->gather_local_candidates)
    return klass->gather_local_candidates (streamtransmitter, error);
  else
    return TRUE;
}



/**
 * fs_stream_transmitter_stop:
 * @streamtransmitter: a #FsStreamTransmitter
 *
 * This functions stops the #FsStreamTransmitter, it must be called before
 * the last reference is dropped.
 */

void
fs_stream_transmitter_stop (FsStreamTransmitter *streamtransmitter)
{
  FsStreamTransmitterClass *klass =
    FS_STREAM_TRANSMITTER_GET_CLASS (streamtransmitter);

  if (klass->stop)
    return klass->stop (streamtransmitter);
}


/**
 * fs_stream_transmitter_emit_error:
 * @streamtransmitter: #FsStreamTransmitter on which to emit the error signal
 * @error_no: The number of the error
 * @error_msg: Error message to be displayed to user
 * @debug_msg: Debugging error message
 *
 * This function emit the "error" signal on a #FsStreamTransmitter, it should
 * only be called by subclasses.
 */
void
fs_stream_transmitter_emit_error (FsStreamTransmitter *streamtransmitter,
  gint error_no, gchar *error_msg, gchar *debug_msg)
{
  g_signal_emit (streamtransmitter, signals[ERROR_SIGNAL], 0, error_no,
      error_msg, debug_msg);
}

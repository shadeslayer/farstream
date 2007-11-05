/*
 * Farsight2 - Farsight Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-stream.c - A Farsight Stream gobject (base implementation)
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
 * SECTION:fs-stream
 * @short_description: A stream in a session in a conference
 *
 * This object is the base implementation of a Farsight Stream. It
 * needs to be derived and implemented by a farsight conference gstreamer
 * element. A Farsight Stream is a media stream originating from a participant
 * inside a session. In fact, a FarsightStream instance is obtained by adding a
 * participant into a session using #fs_session_add_participant.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-session.h"
#include "fs-stream.h"
#include "fs-marshal.h"
#include "fs-codec.h"

#include <gst/gst.h>

/* Signals */
enum
{
  ERROR,
  SRC_PAD_ADDED,
  RECV_CODEC_CHANGED,
  NEW_ACTIVE_CANDIDATE_PAIR,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
#if 0
  /* TODO Do we really need this? */
  PROP_SOURCE_PADS,
#endif
  PROP_REMOTE_CODECS,
  PROP_CURRENT_RECV_CODEC,
  PROP_DIRECTION,
  PROP_PARTICIPANT,
  PROP_SESSION
};

struct _FsStreamPrivate
{
  gboolean disposed;
};

#define FS_STREAM_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_SESSION, FsStreamPrivate))

static void fs_stream_class_init (FsStreamClass *klass);
static void fs_stream_init (FsStream *self);
static void fs_stream_dispose (GObject *object);
static void fs_stream_finalize (GObject *object);

static void fs_stream_get_property (GObject *object,
                                    guint prop_id,
                                    GValue *value,
                                    GParamSpec *pspec);
static void fs_stream_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_stream_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsStreamClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_stream_class_init,
      NULL,
      NULL,
      sizeof (FsStream),
      0,
      (GInstanceInitFunc) fs_stream_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsStream", &info, 0);
  }

  return type;
}

static GType
fs_stream_direction_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      { FS_DIRECTION_NONE, "No data transfer (default)", "none"},
      { FS_DIRECTION_BOTH, "Both (send and receive)", "both"},
      { FS_DIRECTION_SEND, "Send only", "send" },
      { FS_DIRECTION_RECV, "Receive only", "recv" },
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("FsStreamDirection", values);
  }
  return gtype;
}

static void
fs_stream_class_init (FsStreamClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_stream_set_property;
  gobject_class->get_property = fs_stream_get_property;

#if 0
  /**
   * FsStream:source-pads:
   *
   * A #GList of #GstPad of source pads being used by this stream to receive the
   * different codecs.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_SOURCE_PADS,
      g_param_spec_object ("source-pads",
        "A list of source pads being used in this stream",
        "A GList of GstPads representing the source pads being used by this"
        " stream for the different codecs",
        ,
        G_PARAM_READABLE));
#endif

  /**
   * FsStream:remote-codecs:
   *
   * This is the list of remote codecs for this stream. They must be set by the
   * user as soon as they are known using fs_stream_set_remote_codecs()
   * (generally through external signaling). It is a #GList of #FsCodec.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_REMOTE_CODECS,
      g_param_spec_boxed ("remote-codecs",
        "List of remote codecs",
        "A GList of FsCodecs of the remote codecs",
        FS_TYPE_CODEC_LIST,
        G_PARAM_READABLE));

  /**
   * FsStream:current-recv-codec:
   *
   * This is the codec that is currently being received. It is the same as the
   * one emitted in the ::recv-codec-changed signal. User must free the codec
   * using fs_codec_destroy() when done.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_CURRENT_RECV_CODEC,
      g_param_spec_boxed ("current-recv-codec",
        "The codec currently being received",
        "A FsCodec of the codec currently being received",
        FS_TYPE_CODEC,
        G_PARAM_READABLE));

  /**
   * FsStream:direction:
   *
   * The direction of the stream. This property is set initially as a parameter
   * to the fs_session_add_participant() function. It can be changed later if
   * required by setting this property.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_DIRECTION,
      g_param_spec_enum ("direction",
        "The direction of the stream",
        "An enum to set and get the direction of the stream",
        FS_TYPE_STREAM_DIRECTION,
        FS_DIRECTION_NONE,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  /**
   * FsStream:participant:
   *
   * The #FsParticipant for this stream. This property is a construct param and
   * is read-only construction.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_DIRECTION,
      g_param_spec_object ("participant",
        "The participant of the stream",
        "An FsParticipant represented by the stream",
        FS_TYPE_PARTICIPANT,
        G_PARAM_CONSTRUCT | G_PARAM_READABLE));

  /**
   * FsStream:session:
   *
   * The #FsSession for this stream. This property is a construct param and
   * is read-only construction.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_DIRECTION,
      g_param_spec_object ("session",
        "The session of the stream",
        "An FsSession represented by the stream",
        FS_TYPE_PARTICIPANT,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  /**
   * FsStream::error:
   * @self: #FsStream that emitted the signal
   * @errorno: The number of the error
   * @error_msg: Error message to be displayed to user
   * @debug_msg: Debugging error message
   *
   * This signal is emitted in any error condition
   *
   */
  signals[ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      fs_marshal_VOID__OBJECT_INT_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

  /**
   * FsStream::src-pad-added:
   * @self: #FsStream that emitted the signal
   * @pad: #GstPad of the new source pad
   * @codec: #FsCodec of the codec being received on the new source pad
   *
   * This signal is emitted when a new gst source pad has been created for a
   * specific codec being received. There will be a different source pad for
   * each codec that is received. The user must ref the #GstPad if he wants to
   * use it. The user should not modify the #FsCodec and must copy it if he
   * wants to use it outside the callback scope.
   *
   */
  signals[SRC_PAD_ADDED] = g_signal_new ("src-pad-added",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      fs_marshal_VOID__BOXED_BOXED,
      G_TYPE_NONE, 2, GST_TYPE_PAD, FS_TYPE_CODEC);

  /**
   * FsStream::recv-codec-changed:
   * @self: #FsStream that emitted the signal
   * @pad: #GstPad of the current source pad
   * @codec: #FsCodec of the new codec being received
   *
   * This signal is emitted when the currently received codec has changed. This
   * is useful for displaying the current active reception codec or for making
   * changes to the pipeline. The user must ref the #GstPad if he wants to
   * use it. The user should not modify the #FsCodec and must copy it if he
   * wants to use it outside the callback scope.
   *
   */
  signals[RECV_CODEC_CHANGED] = g_signal_new ("recv-codec-changed",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      fs_marshal_VOID__BOXED_BOXED,
      G_TYPE_NONE, 2, GST_TYPE_PAD, FS_TYPE_CODEC);

  /**
   * FsStream::new-active-candidate-pair:
   * @self: #FsStream that emitted the signal
   * @native_candidate: #FsCandidate of the native candidate being used
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
      fs_marshal_VOID__BOXED_BOXED,
      G_TYPE_NONE, 2, FS_TYPE_CANDIDATE, FS_TYPE_CANDIDATE);

  gobject_class->dispose = fs_stream_dispose;
  gobject_class->finalize = fs_stream_finalize;

  g_type_class_add_private (klass, sizeof (FsStreamPrivate));
}

static void
fs_stream_init (FsStream *self)
{
  /* member init */
  self->priv = FS_STREAM_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_stream_dispose (GObject *object)
{
  FsStream *self = FS_STREAM (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_stream_finalize (GObject *object)
{
  parent_class->finalize (object);
}

static void
fs_stream_get_property (GObject *object,
                        guint prop_id,
                        GValue *value,
                        GParamSpec *pspec)
{
}

static void
fs_stream_set_property (GObject *object,
                        guint prop_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
}

/**
 * fs_stream_add_remote_candidate:
 * @stream: an #FsStream
 * @candidate: an #FsCandidate struct representing a remote candidate
 *
 * This function adds the given candidate into the remote candiate list of the
 * stream. It will be used for establishing a connection with the peer. A copy
 * will be made so the user must free the passed candidate using
 * fs_candidate_destroy() when done.
 */
void
fs_stream_add_remote_candidate (FsStream *stream, FsCandidate *candidate)
{
  FsStreamClass *klass = FS_STREAM_GET_CLASS (stream);

  if (klass->add_remote_candidate) {
    klass->add_remote_candidate (stream, candidate);
  } else {
    g_warning ("add_remote_candidate not defined in class");
  }
}

/**
 * fs_stream_preload_recv_codec:
 * @stream: an #FsStream
 * @codec: The #FsCodec to be preloaded
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will preload the codec corresponding to the given codec.
 * This codec must correspond exactly to one of the native-codecs returned by
 * the #FsSession that spawned this #FsStream. Preloading a codec is useful for
 * machines where loading the codec is slow. When preloading, decoding can start
 * as soon as a stream is received.
 *
 * Returns: TRUE of the codec could be preloaded, FALSE if there is an error
 */
gboolean
fs_stream_preload_recv_codec (FsStream *stream, FsCodec *codec, GError **error)
{
  FsStreamClass *klass = FS_STREAM_GET_CLASS (stream);

  *error = NULL;

  if (klass->preload_recv_codec) {
    return klass->preload_recv_codec (stream, codec, error);
  } else {
    g_warning ("preload_recv_codec not defined in class");
  }

  return FALSE;
}

/**
 * fs_stream_set_remote_codecs:
 * @stream: an #FsStream
 * @remote_codecs: a #GList of #FsCodec representing the remote codecs
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will set the list of remote codecs for this stream. If
 * the given remote codecs couldn't be negotiated with the list of native
 * codecs or already negotiated codecs for the corresponding #FsSession, @error
 * will be set and %FALSE will be returned. The @remote_codecs list will be
 * copied so it must be free'd using fs_codec_list_destroy() when done.
 *
 * Returns: %FALSE if the remote codecs couldn't be set.
 */
gboolean
fs_stream_set_remote_codecs (FsStream *stream,
                             GList *remote_codecs, GError **error)
{
  FsStreamClass *klass = FS_STREAM_GET_CLASS (stream);

  *error = NULL;

  if (klass->set_remote_codecs) {
    return klass->set_remote_codecs (stream, remote_codecs, error);
  } else {
    g_warning ("set_remote_codecs not defined in class");
  }

  return FALSE;
}

/*
 * Farsight2 - Farsight Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-stream.c - A Farsight Stream gobject (base implementation)
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
 * SECTION:fs-stream
 * @short_description: A stream in a session in a conference
 *
 * This object is the base implementation of a Farsight Stream. It
 * needs to be derived and implemented by a farsight conference gstreamer
 * element. A Farsight Stream is a media stream originating from a participant
 * inside a session. In fact, a FarsightStream instance is obtained by adding a
 * participant into a session using #fs_session_add_participant.
 *
 *
 * This will communicate asynchronous events to the user through #GstMessage
 * of type #GST_MESSAGE_ELEMENT sent over the #GstBus.
 * </para>
 * <refsect2><title>The "<literal>farsight-new-local-candidate</literal>" message</title>
 * |[
 * "stream"           #FsStream          The stream that emits the message
 * "candidate"        #FsCandidate       The new candidate
 * ]|
 * <para>
 * This message is emitted when a new local candidate is discovered.
 * </para>
 * </refsect2>
 * <refsect2><title>The "<literal>farsight-local-candidates-prepared</literal>" message</title>
 * |[
 * "stream"           #FsStream          The stream that emits the message
 * ]|
 * <para>
 * This signal is emitted when all local candidates have been
 * prepared, an ICE implementation would send its SDP offer or answer.
 * </para>
 * </refsect2>
 * <refsect2><title>The "<literal>farsight-new-active-candidate-pair</literal>" message</title>
 * |[
 * "stream"           #FsStream          The stream that emits the message
 * "local-candidate"  #FsCandidate       Local candidate being used
 * "remote-candidate" #FsCandidate       Remote candidate being used
 * ]|
 * <para>
 * This message is emitted when there is a new active candidate pair that has
 * been established. This is specially useful for ICE where the active
 * candidate pair can change automatically due to network conditions. The user
 * must not modify the candidates and must copy them if he wants to use them
 * outside the callback scope. This message is emitted once per component.
 * </para>
 * </refsect2>
 * <refsect2><title>The "<literal>farsight-recv-codecs-changed</literal>" message</title>
 * |[
 * "stream"           #FsStream          The stream that emits the message
 * "codecs"           #FsCodecGList      A #GList of #FsCodec
 * ]|
 * <para>
 * This message is emitted when the content of the
 * #FsStream:current-recv-codecs property changes. It is normally emitted
 * right after the #FsStream::src-pad-added signal only if that codec was not
 * previously received in this stream, but it can also be emitted if the pad
 * already exists, but the source material that will come to it is different.
 * The list of new recv-codecs is included in the message
 * </para>
 * </refsect2>
 * <refsect2><title>The "<literal>farsight-component-state-changed</literal>" message</title>
 * |[
 * "stream"           #FsStream          The stream that emits the message
 * "component"        #guint             The component whose state changed
 * "state"            #FsStreamState     The new state of the component
 * ]|
 * <para>
 * This message is emitted the state of a component of a stream changes.
 * </para>
 * </refsect2>
 * <para>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-stream.h"

#include "fs-session.h"
#include "fs-marshal.h"
#include "fs-codec.h"
#include "fs-candidate.h"
#include "fs-stream-transmitter.h"
#include "fs-conference-iface.h"
#include "fs-enum-types.h"
#include "fs-private.h"

#include <gst/gst.h>

/* Signals */
enum
{
  ERROR_SIGNAL,
  SRC_PAD_ADDED,
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
  PROP_NEGOTIATED_CODECS,
  PROP_CURRENT_RECV_CODECS,
  PROP_DIRECTION,
  PROP_PARTICIPANT,
  PROP_SESSION,
  PROP_STREAM_TRANSMITTER
};

/*
struct _FsStreamPrivate
{
};

#define FS_STREAM_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_STREAM, FsStreamPrivate))
*/

G_DEFINE_ABSTRACT_TYPE(FsStream, fs_stream, G_TYPE_OBJECT);

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
   * FsStream:negotiated-codecs:
   *
   * This is the list of negotiatied codecs, it is the same list as the list
   * of #FsCodec from the parent #FsSession, except that the codec config data
   * has been replaced with the data from the remote codecs for this stream.
   * This is the list of #FsCodec used to receive data from this stream.
   * It is a #GList of #FsCodec.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_NEGOTIATED_CODECS,
      g_param_spec_boxed ("negotiated-codecs",
        "List of remote codecs",
        "A GList of FsCodecs of the negotiated codecs for this stream",
        FS_TYPE_CODEC_LIST,
        G_PARAM_READABLE));

  /**
   * FsStream:current-recv-codecs:
   *
   * This is the list of codecs that have been received by this stream.
   * The user must free the list if fs_codec_list_destroy().
   * The "farsight-recv-codecs-changed" message is send on the #GstBus
   * when the value of this property changes.
   * It is normally emitted right after #FsStream::src-pad-added
   * only if that codec was not previously received in this stream, but it can
   * also be emitted if the pad already exists, but the source material that
   * will come to it is different.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_CURRENT_RECV_CODECS,
      g_param_spec_boxed ("current-recv-codecs",
          "The codecs currently being received",
          "A GList of FsCodec representing the codecs that have been received",
          FS_TYPE_CODEC_LIST,
          G_PARAM_READABLE));

  /**
   * FsStream:direction:
   *
   * The direction of the stream. This property is set initially as a parameter
   * to the fs_session_new_stream() function. It can be changed later if
   * required by setting this property.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_DIRECTION,
      g_param_spec_flags ("direction",
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
      PROP_PARTICIPANT,
      g_param_spec_object ("participant",
        "The participant of the stream",
        "An FsParticipant represented by the stream",
        FS_TYPE_PARTICIPANT,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  /**
   * FsStream:session:
   *
   * The #FsSession for this stream. This property is a construct param and
   * is read-only construction.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_SESSION,
      g_param_spec_object ("session",
        "The session of the stream",
        "An FsSession represented by the stream",
        FS_TYPE_SESSION,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  /**
   * FsStream:stream-transmitter:
   *
   * The #FsStreamTransmitter for this stream.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_STREAM_TRANSMITTER,
      g_param_spec_object ("stream-transmitter",
        "The transmitter use by the stream",
        "An FsStreamTransmitter used by this stream",
        FS_TYPE_STREAM_TRANSMITTER,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

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
  signals[ERROR_SIGNAL] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_marshal_VOID__ENUM_STRING_STRING,
      G_TYPE_NONE, 3, FS_TYPE_ERROR, G_TYPE_STRING, G_TYPE_STRING);

  /**
   * FsStream::src-pad-added:
   * @self: #FsStream that emitted the signal
   * @pad: #GstPad of the new source pad
   * @codec: #FsCodec of the codec being received on the new source pad
   *
   * This signal is emitted when a new gst source pad has been created for a
   * specific codec being received. There will be a different source pad for
   * each codec that is received. The user must ref the #GstPad if he wants to
   * keep it. The user should not modify the #FsCodec and must copy it if he
   * wants to use it outside the callback scope.
   *
   * This signal is not emitted on the main thread, but on GStreamer's streaming
   * thread!
   *
   */
  signals[SRC_PAD_ADDED] = g_signal_new ("src-pad-added",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_marshal_VOID__BOXED_BOXED,
      G_TYPE_NONE, 2, GST_TYPE_PAD, FS_TYPE_CODEC);

  // g_type_class_add_private (klass, sizeof (FsStreamPrivate));
}

static void
fs_stream_init (FsStream *self)
{
  /* member init */
  // self->priv = FS_STREAM_GET_PRIVATE (self);
}

static void
fs_stream_get_property (GObject *object,
                        guint prop_id,
                        GValue *value,
                        GParamSpec *pspec)
{
  GST_WARNING ("Subclass %s of FsStream does not override the %s property"
      " getter",
      G_OBJECT_TYPE_NAME(object),
      g_param_spec_get_name (pspec));
}

static void
fs_stream_set_property (GObject *object,
                        guint prop_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
  GST_WARNING ("Subclass %s of FsStream does not override the %s property"
      " setter",
      G_OBJECT_TYPE_NAME(object),
      g_param_spec_get_name (pspec));
}

/**
 * fs_stream_set_remote_candidates:
 * @stream: an #FsStream
 * @candidates: an #GList of #FsCandidate representing the remote candidates
 * @error: location of a #GError, or %NULL if no error occured
 *
 * This function sets the list of remote candidates. Any new candidates are
 * added to the list. The candidates will be used to establish a connection
 * with the peer. A copy will be made so the user must free the
 * passed candidate using fs_candidate_destroy() when done.
 *
 * Return value: TRUE if the candidate was valid, FALSE otherwise
 */
gboolean
fs_stream_set_remote_candidates (FsStream *stream,
    GList *candidates,
    GError **error)
{
  FsStreamClass *klass = FS_STREAM_GET_CLASS (stream);

  if (klass->set_remote_candidates) {
    return klass->set_remote_candidates (stream, candidates, error);
  } else {
    g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
      "set_remote_candidate not defined in class");
  }

  return FALSE;
}

/**
 * fs_stream_force_remote_candidates:
 * @stream: a #FsStream
 * @remote_candidates: a #GList of #FsCandidate to force
 * @error: location of a #GError, or %NULL if no error occured
 *
 * This function forces data to be sent immediately to the selected remote
 * candidate, by-passing any connectivity checks. There should be at most
 * one candidate per component.
 *
 * Returns: %TRUE if the candidates could be forced, %FALSE otherwise
 */

gboolean
fs_stream_force_remote_candidates (FsStream *stream,
    GList *remote_candidates,
    GError **error)
{
  FsStreamClass *klass = FS_STREAM_GET_CLASS (stream);

  if (klass->force_remote_candidates) {
    return klass->force_remote_candidates (stream,
        remote_candidates,
        error);
  } else {
    g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
      "force_remote_candidates not defined in class");
  }

  return FALSE;
}

/**
 * fs_stream_set_remote_codecs:
 * @stream: a #FsStream
 * @remote_codecs: a #GList of #FsCodec representing the remote codecs
 * @error: location of a #GError, or %NULL if no error occured
 *
 * This function will set the list of remote codecs for this stream. If
 * the given remote codecs couldn't be negotiated with the list of local
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

  if (klass->set_remote_codecs) {
    return klass->set_remote_codecs (stream, remote_codecs, error);
  } else {
    g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
      "set_remote_codecs not defined in class");
  }

  return FALSE;
}


/**
 * fs_stream_emit_error:
 * @stream: #FsStream on which to emit the error signal
 * @error_no: The number of the error
 * @error_msg: Error message to be displayed to user
 * @debug_msg: Debugging error message
 *
 * This function emits the #FsStream::error" signal, it should only be
 * called by subclasses.
 */
void
fs_stream_emit_error (FsStream *stream,
    gint error_no,
    const gchar *error_msg,
    const gchar *debug_msg)
{
  g_signal_emit (stream, signals[ERROR_SIGNAL], 0, error_no, error_msg,
      debug_msg);
}


/**
 * fs_stream_emit_src_pad_added:
 * @stream: #FsStream on which to emit the src-pad-added signal
 * @pad: the #GstPad that this #FsStream has created
 * @codec: The #FsCodec for this pad
 *
 * Emits the #FsStream::src-pad-added" signal, it should only be
 * called by subclasses.
 */

void
fs_stream_emit_src_pad_added (FsStream *stream,
    GstPad *pad,
    FsCodec *codec)
{
  g_signal_emit (stream, signals[SRC_PAD_ADDED], 0, pad, codec);
}

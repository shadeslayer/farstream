/*
 * Farsight2 - Farsight Session
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-session.c - A Farsight Session gobject (base implementation)
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
 * SECTION:fs-session
 * @short_description: A session in a conference
 *
 * This object is the base implementation of a Farsight Session. It needs to be
 * derived and implemented by a farsight conference gstreamer element. A
 * Farsight session is defined in the same way as an RTP session. It can contain
 * one or more participants but represents only one media stream (i.e. One
 * session for video and one session for audio in an AV conference). Sessions
 * contained in the same conference will be synchronised together during
 * playback.
 *
 *
 * This will communicate asynchronous events to the user through #GstMessage
 * of type #GST_MESSAGE_ELEMENT sent over the #GstBus.
 * </para>
 * <refsect2><title>The "<literal>farsight-send-codec-changed</literal>"
 *   message</title>
 * |[
 * "session"          #FsSession          The session that emits the message
 * "codec"            #FsCodec            The new send codec
 * "secondary-codecs" #GList              A #GList of #FsCodec (to be freed
 *                                        with fs_codec_list_destroy())
 * ]|
 * <para>
 * This message is sent on the bus when the value of the
 * #FsSession:current-send-codec property changes.
 * </para>
 * </refsect2>
 * <refsect2><title>The "<literal>farsight-codecs-changed</literal>"
 *  message</title>
 * |[
 * "session"          #FsSession          The session that emits the message
 * ]|
 * <para>
 * This message is sent on the bus when the value of the
 * #FsSession:codecs or #FsSession:codecs-without-config properties change.
 * If one is using codecs that have configuration data that needs to be
 * transmitted reliably, once should check the value of #FsSession:codecs-ready
 * property to make sure all of the codecs configuration are ready and have been
 * discovered before using the codecs. If its not %TRUE, one should wait for the
 * next "farsight-codecs-changed" message until reading the codecs.
 * </para>
 * </refsect2>
 * <para>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-session.h"

#include <gst/gst.h>

#include "fs-conference-iface.h"
#include "fs-codec.h"
#include "fs-marshal.h"
#include "fs-enumtypes.h"
#include "fs-private.h"

#define GST_CAT_DEFAULT fs_base_conference_debug

/* Signals */
enum
{
  ERROR_SIGNAL,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_MEDIA_TYPE,
  PROP_ID,
  PROP_SINK_PAD,
  PROP_CODEC_PREFERENCES,
  PROP_CODECS,
  PROP_CODECS_WITHOUT_CONFIG,
  PROP_CURRENT_SEND_CODEC,
  PROP_CODECS_READY,
  PROP_TYPE_OF_SERVICE
};

/*
struct _FsSessionPrivate
{
};

#define FS_SESSION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_SESSION, FsSessionPrivate))
*/

G_DEFINE_ABSTRACT_TYPE(FsSession, fs_session, GST_TYPE_OBJECT);

static void fs_session_get_property (GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec);
static void fs_session_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec);

static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_session_class_init (FsSessionClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = fs_session_set_property;
  gobject_class->get_property = fs_session_get_property;

  /**
   * FsSession:media-type:
   *
   * The media-type of the session. This is either Audio, Video or both.
   * This is a constructor parameter that cannot be changed.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_MEDIA_TYPE,
      g_param_spec_enum ("media-type",
        "The media type of the session",
        "An enum that specifies the media type of the session",
        FS_TYPE_MEDIA_TYPE,
        FS_MEDIA_TYPE_AUDIO,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * FsSession:id:
   *
   * The ID of the session, the first number of the pads linked to this session
   * will be this id
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_ID,
      g_param_spec_uint ("id",
        "The ID of the session",
        "This ID is used on pad related to this session",
        0, G_MAXUINT, 0,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * FsSession:sink-pad:
   *
   * The Gstreamer sink pad that must be used to send media data on this
   * session. User must unref this GstPad when done with it.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_SINK_PAD,
      g_param_spec_object ("sink-pad",
        "A gstreamer sink pad for this session",
        "A pad used for sending data on this session",
        GST_TYPE_PAD,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * FsSession:codec-preferences:
   *
   * Type: GLib.List<FsCodec>
   * Transfer: full
   *
   * This is the current preferences list for the local codecs. It is
   * set by the user to specify the codec options and priorities. The user may
   * change its value with fs_session_set_codec_preferences() at any time
   * during a session. It is a #GList of #FsCodec.
   * The user must free this codec list using fs_codec_list_destroy() when done.
   *
   * The payload type may be a valid dynamic PT (96-127), %FS_CODEC_ID_DISABLE
   * or %FS_CODEC_ID_ANY. If the encoding name is "reserve-pt", then the
   * payload type of the codec will be "reserved" and not be used by any
   * dynamically assigned payload type.
   */
  g_object_class_install_property (gobject_class,
      PROP_CODEC_PREFERENCES,
      g_param_spec_boxed ("codec-preferences",
        "List of user preferences for the codecs",
        "A GList of FsCodecs that allows user to set his codec options and"
        " priorities",
        FS_TYPE_CODEC_LIST,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * FsSession:codecs:
   *
   * Type: GLib.List<FsCodec>
   * Transfer: full
   *
   * This is the list of codecs used for this session. It will include the
   * codecs and payload type used to receive media on this session. It will
   * also include any configuration parameter that must be transmitted reliably
   * for the other end to decode the content.
   *
   * It may change when the codec preferences are set, when codecs are set
   * on a #FsStream in this session, when a #FsStream is destroyed or
   * asynchronously when new config data is discovered.
   *
   * You can only assume that the configuration parameters are valid when
   * the #FsSession:codecs-ready property is %TRUE.
   * The "farsight-codecs-changed" message will be emitted whenever the value
   * of this property changes.
   *
   * It is a #GList of #FsCodec. User must free this codec list using
   * fs_codec_list_destroy() when done.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_CODECS,
      g_param_spec_boxed ("codecs",
        "List of codecs",
        "A GList of FsCodecs indicating the codecs for this session",
        FS_TYPE_CODEC_LIST,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * FsSession:codecs-without-config:
   *
   * Type: GLib.List<FsCodec>
   * Transfer: full
   *
   * This is the same list of codecs as #FsSession:codecs without
   * the configuration information that describes the data sent. It is suitable
   * for configurations where a list of codecs is shared by many senders.
   * If one is using codecs such as Theora, Vorbis or H.264 that require
   * such information to be transmitted, the configuration data should be
   * included in the stream and retransmitted regularly.
   *
   * It may change when the codec preferences are set, when codecs are set
   * on a #FsStream in this session, when a #FsStream is destroyed or
   * asynchronously when new config data is discovered.
   *
   * The "farsight-codecs-changed" message will be emitted whenever the value
   * of this property changes.
   *
   * It is a #GList of #FsCodec. User must free this codec list using
   * fs_codec_list_destroy() when done.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_CODECS_WITHOUT_CONFIG,
      g_param_spec_boxed ("codecs-without-config",
          "List of codecs without the configuration data",
          "A GList of FsCodecs indicating the codecs for this session without "
          "any configuration data",
          FS_TYPE_CODEC_LIST,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * FsSession:current-send-codec:
   *
   * Indicates the currently active send codec. A user can change the active
   * send codec by calling fs_session_set_send_codec(). The send codec could
   * also be automatically changed by Farsight. This property is an
   * #FsCodec. User must free the codec using fs_codec_destroy() when done.
   * The "farsight-send-codec-changed" message is emitted on the bus when
   * the value of this property changes.
   */
  g_object_class_install_property (gobject_class,
      PROP_CURRENT_SEND_CODEC,
      g_param_spec_boxed ("current-send-codec",
        "Current active send codec",
        "An FsCodec indicating the currently active send codec",
        FS_TYPE_CODEC,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * FsSession:codecs-ready
   *
   * Some codecs that have configuration data that needs to be sent reliably
   * may need to be initialized from actual data before being ready. If your
   * application uses such codecs, wait until this property is %TRUE before
   * using the #FsSession:codecs
   * property. If the value if not %TRUE, the "farsight-codecs-changed"
   * message will be emitted when it becomes %TRUE. You should re-check
   * the value of this property when you receive the message.
   */
  g_object_class_install_property (gobject_class,
      PROP_CODECS_READY,
      g_param_spec_boolean ("codecs-ready",
          "Indicates if the codecs are ready",
          "Indicates if the codecs are ready or if their configuration is"
          " still being discovered",
          TRUE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * FsSession:tos
   *
   * Sets the IP ToS field (and if possible the IPv6 TCLASS field
   */
  g_object_class_install_property (gobject_class,
      PROP_TYPE_OF_SERVICE,
      g_param_spec_uint ("tos",
          "IP Type of Service",
          "The IP Type of Service to set on sent packets",
          0, 255, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  /**
   * FsSession::error:
   * @self: #FsSession that emitted the signal
   * @object: The #Gobject that emitted the signal
   * @error_no: The number of the error
   * @error_msg: Error message to be displayed to user
   * @debug_msg: Debugging error message
   *
   * This signal is emitted in any error condition, it can be emitted on any
   * thread. Applications should listen to the GstBus for errors.
   *
   */
  signals[ERROR_SIGNAL] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_marshal_VOID__OBJECT_ENUM_STRING_STRING,
      G_TYPE_NONE, 4, G_TYPE_OBJECT, FS_TYPE_ERROR, G_TYPE_STRING,
      G_TYPE_STRING);
}

static void
fs_session_init (FsSession *self)
{
  /* member init */
  // self->priv = FS_SESSION_GET_PRIVATE (self);
}

static void
fs_session_get_property (GObject *object,
                         guint prop_id,
                         GValue *value,
                         GParamSpec *pspec)
{
  switch (prop_id)
  {
    case PROP_CODECS_READY:
      g_value_set_boolean (value, TRUE);
      break;

    default:
      GST_WARNING ("Subclass %s of FsSession does not override the %s property"
          " getter",
          G_OBJECT_TYPE_NAME(object),
          g_param_spec_get_name (pspec));
      break;
  }
}

static void
fs_session_set_property (GObject *object,
                         guint prop_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
  GST_WARNING ("Subclass %s of FsSession does not override the %s property"
      " setter",
      G_OBJECT_TYPE_NAME(object),
      g_param_spec_get_name (pspec));
}

static void
fs_session_error_forward (GObject *signal_src,
                          FsError error_no, gchar *error_msg,
                          FsSession *session)
{
  /* We just need to forward the error signal including a ref to the stream
   * object (signal_src) */
  g_signal_emit (session, signals[ERROR_SIGNAL], 0, signal_src, error_no,
      error_msg, NULL);
}

/**
 * fs_session_new_stream:
 * @session: a #FsSession
 * @participant: #FsParticipant of a participant for the new stream
 * @direction: #FsStreamDirection describing the direction of the new stream that will
 * be created for this participant
 * @transmitter: Name of the type of transmitter to use for this session
 * @stream_transmitter_n_parameters: Number of parametrs passed to the stream
 *  transmitter
 * @stream_transmitter_parameters: (array length=stream_transmitter_n_parameters) (allow-none):
 *   an array of n_parameters #GParameter struct that will be passed
 *   to the newly-create #FsStreamTransmitter
 * @error: location of a #GError, or %NULL if no error occured
 *
 * This function creates a stream for the given participant into the active session.
 *
 * Returns: (transfer full): the new #FsStream that has been created.
 * User must unref the #FsStream when the stream is ended. If an error occured,
 * returns NULL.
 */
FsStream *
fs_session_new_stream (FsSession *session, FsParticipant *participant,
                       FsStreamDirection direction, const gchar *transmitter,
                       guint stream_transmitter_n_parameters,
                       GParameter *stream_transmitter_parameters,
                       GError **error)
{
  FsSessionClass *klass;
  FsStream *new_stream = NULL;

  g_return_val_if_fail (session, NULL);
  g_return_val_if_fail (FS_IS_SESSION (session), NULL);
  klass = FS_SESSION_GET_CLASS (session);
  g_return_val_if_fail (klass->new_stream, NULL);

  new_stream = klass->new_stream (session, participant, direction,
      transmitter, stream_transmitter_n_parameters,
      stream_transmitter_parameters, error);

  if (!new_stream)
    return NULL;

  /* Let's catch all stream errors and forward them */
  g_signal_connect_object (new_stream, "error",
      G_CALLBACK (fs_session_error_forward), session, 0);

  return new_stream;
}

/**
 * fs_session_start_telephony_event:
 * @session: a #FsSession
 * @event: A #FsStreamDTMFEvent or another number defined at
 * http://www.iana.org/assignments/audio-telephone-event-registry
 * @volume: The volume in dBm0 without the negative sign. Should be between
 * 0 and 36. Higher values mean lower volume
 * @method: The method used to send the event
 *
 * This function will start sending a telephony event (such as a DTMF
 * tone) on the #FsSession. You have to call the function
 * fs_session_stop_telephony_event() to stop it.
 * This function will use any available method, if you want to use a specific
 * method only, use fs_session_start_telephony_event_full()
 *
 * Returns: %TRUE if sucessful, it can return %FALSE if the #FsStream
 * does not support this telephony event.
 */
gboolean
fs_session_start_telephony_event (FsSession *session, guint8 event,
                                  guint8 volume, FsDTMFMethod method)
{
  FsSessionClass *klass;

  g_return_val_if_fail (session, FALSE);
  g_return_val_if_fail (FS_IS_SESSION (session), FALSE);
  klass = FS_SESSION_GET_CLASS (session);

  if (klass->start_telephony_event) {
    return klass->start_telephony_event (session, event, volume, method);
  } else {
    GST_WARNING ("start_telephony_event not defined in class");
  }
  return FALSE;
}

/**
 * fs_session_stop_telephony_event:
 * @session: an #FsSession
 * @method: The method used to send the event
 *
 * This function will stop sending a telephony event started by
 * fs_session_start_telephony_event(). If the event was being sent
 * for less than 50ms, it will be sent for 50ms minimum. If the
 * duration was a positive and the event is not over, it will cut it
 * short.
 *
 * Returns: %TRUE if sucessful, it can return %FALSE if the #FsSession
 * does not support telephony events or if no telephony event is being sent
 */
gboolean
fs_session_stop_telephony_event (FsSession *session, FsDTMFMethod method)
{
  FsSessionClass *klass;

  g_return_val_if_fail (session, FALSE);
  g_return_val_if_fail (FS_IS_SESSION (session), FALSE);
  klass = FS_SESSION_GET_CLASS (session);

  if (klass->stop_telephony_event) {
    return klass->stop_telephony_event (session, method);
  } else {
    GST_WARNING ("stop_telephony_event not defined in class");
  }
  return FALSE;
}

/**
 * fs_session_set_send_codec:
 * @session: a #FsSession
 * @send_codec: a #FsCodec representing the codec to send
 * @error: location of a #GError, or %NULL if no error occured
 *
 * This function will set the currently being sent codec for all streams in this
 * session. The given #FsCodec must be taken directly from the #codecs
 * property of the session. If the given codec is not in the codecs
 * list, @error will be set and %FALSE will be returned. The @send_codec will be
 * copied so it must be free'd using fs_codec_destroy() when done.
 *
 * Returns: %FALSE if the send codec couldn't be set.
 */
gboolean
fs_session_set_send_codec (FsSession *session, FsCodec *send_codec,
                           GError **error)
{
  FsSessionClass *klass;

  g_return_val_if_fail (session, FALSE);
  g_return_val_if_fail (FS_IS_SESSION (session), FALSE);
  klass = FS_SESSION_GET_CLASS (session);

  if (klass->set_send_codec) {
    return klass->set_send_codec (session, send_codec, error);
  } else {
    GST_WARNING ("set_send_codec not defined in class");
    g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
      "set_send_codec not defined in class");
  }
  return FALSE;
}

/**
 * fs_session_set_codec_preferences:
 * @session: a #FsSession
 * @codec_preferences: (element-type FsCodec): a #GList of #FsCodec with the
 *   desired configuration
 * @error: location of a #GError, or %NULL if no error occured
 *
 * Set the list of desired codec preferences. The user may
 * change this value during an ongoing session. Note that doing this can cause
 * the codecs to change. Therefore this requires the user to fetch
 * the new codecs and renegotiate them with the peers. It is a #GList
 * of #FsCodec. The changes are immediately effective.
 * The function does not take ownership of the list.
 *
 * The payload type may be a valid dynamic PT (96-127), %FS_CODEC_ID_DISABLE
 * or %FS_CODEC_ID_ANY. If the encoding name is "reserve-pt", then the
 * payload type of the codec will be "reserved" and not be used by any
 * dynamically assigned payload type.
 *
 * If the list of specifications would invalidate all codecs, an error will
 * be returned.
 *
 * Returns: %TRUE on success, %FALSE on error.
 */
gboolean
fs_session_set_codec_preferences (FsSession *session,
    GList *codec_preferences,
    GError **error)
{
  FsSessionClass *klass;

  g_return_val_if_fail (session, FALSE);
  g_return_val_if_fail (FS_IS_SESSION (session), FALSE);
  klass = FS_SESSION_GET_CLASS (session);

  if (klass->set_codec_preferences) {
    return klass->set_codec_preferences (session, codec_preferences, error);
  } else {
    GST_WARNING ("set_send_preferences not defined in class");
    g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
        "set_codec_preferences not defined in class");
  }
  return FALSE;
}

/**
 * fs_session_emit_error:
 * @session: #FsSession on which to emit the error signal
 * @error_no: The number of the error of type #FsError
 * @error_msg: Error message to be displayed to user
 * @debug_msg: Debugging error message
 *
 * This function emit the "error" signal on a #FsSession, it should only be
 * called by subclasses.
 */
void
fs_session_emit_error (FsSession *session,
    gint error_no,
    const gchar *error_msg,
    const gchar *debug_msg)
{
  g_signal_emit (session, signals[ERROR_SIGNAL], 0, session, error_no,
      error_msg, debug_msg);
}

/**
 * fs_session_list_transmitters:
 * @session: A #FsSession
 *
 * Get the list of all available transmitters for this session.
 *
 * Returns: (transfer full): a newly-allocagted %NULL terminated array of
 * named of transmitters or %NULL if no transmitter is needed for this type of
 * session. It should be freed with g_strfreev().
 */

gchar **
fs_session_list_transmitters (FsSession *session)
{
  FsSessionClass *klass;

  g_return_val_if_fail (session, NULL);
  g_return_val_if_fail (FS_IS_SESSION (session), NULL);
  klass = FS_SESSION_GET_CLASS (session);

  if (klass->list_transmitters) {
    return klass->list_transmitters (session);
  } else {
    return NULL;
  }
}


/**
 * fs_session_get_stream_transmitter_type:
 * @session: A #FsSession
 * @transmitter: The name of the transmitter
 *
 * Returns the GType of the stream transmitter, bindings can use it
 * to validate/convert the parameters passed to fs_session_new_stream().
 *
 * Returns: The #GType of the stream transmitter
 */
GType
fs_session_get_stream_transmitter_type (FsSession *session,
    const gchar *transmitter)
{
  FsSessionClass *klass;

  g_return_val_if_fail (session, 0);
  g_return_val_if_fail (FS_IS_SESSION (session), 0);
  klass = FS_SESSION_GET_CLASS (session);

  if (klass->get_stream_transmitter_type)
    return klass->get_stream_transmitter_type (session, transmitter);

  return 0;
}

/**
 * fs_session_codecs_need_resend:
 * @session: a #FsSession
 * @old_codecs: Codecs previously retrieved from the #FsSession:codecs property
 * @new_codecs: Codecs recently retrieved from the #FsSession:codecs property
 *
 * Some codec updates need to be reliably transmitted to the other side
 * because they contain important parameters required to decode the media.
 * Other codec updates, caused by user action, don't.
 *
 * Returns: (element-type FsCodec) (transfer full): A new #GList of
 *  #FsCodec that need to be resent or %NULL if there are none. This
 *  list must be freed with fs_codec_list_destroy().
 */
GList *
fs_session_codecs_need_resend (FsSession *session,
    GList *old_codecs, GList *new_codecs)
{
  FsSessionClass *klass;

  g_return_val_if_fail (session, 0);
  g_return_val_if_fail (FS_IS_SESSION (session), 0);
  klass = FS_SESSION_GET_CLASS (session);

  if (klass->codecs_need_resend)
    return klass->codecs_need_resend (session, old_codecs, new_codecs);

  return NULL;
}

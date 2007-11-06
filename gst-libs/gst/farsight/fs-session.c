/*
 * Farsight2 - Farsight Session
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-session.c - A Farsight Session gobject (base implementation)
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-session.h"
#include "fs-codec.h"
#include "fs-marshal.h"
#include <gst/gst.h>

/* Signals */
enum
{
  ERROR,
  SEND_CODEC_CHANGED,
  NEW_NEGOTIATED_CODECS,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_MEDIA_TYPE,
  PROP_ID,
  PROP_SINK_PAD,
  PROP_NATIVE_CODECS,
  PROP_NATIVE_CODECS_CONFIG,
  PROP_NEGOTIATED_CODECS,
  PROP_CURRENT_SEND_CODEC
};

struct _FsSessionPrivate
{
  /* List of Streams */
  GPtrArray *stream_list;

  gboolean disposed;
};

#define FS_SESSION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_SESSION, FsSessionPrivate))

static void fs_session_class_init (FsSessionClass *klass);
static void fs_session_init (FsSession *self);
static void fs_session_dispose (GObject *object);
static void fs_session_finalize (GObject *object);

static void fs_session_get_property (GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec);
static void fs_session_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

GQuark
fs_session_error_quark (void)
{
  return g_quark_from_static_string ("fs-session-error");
}


GType
fs_session_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsSessionClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_session_class_init,
      NULL,
      NULL,
      sizeof (FsSession),
      0,
      (GInstanceInitFunc) fs_session_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsSession", &info, G_TYPE_FLAG_ABSTRACT);
  }

  return type;
}

static void
fs_session_class_init (FsSessionClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

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
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

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
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

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
        G_PARAM_READABLE));

  /**
   * FsSession:native-codecs:
   *
   * This is the list of native codecs that have been auto-detected based on
   * installed GStreamer plugins. This list is unchanged during the lifecycle of
   * the session unless native-codecs-config is changed by the user. It is a
   * #GList of #FsCodec. User must free this codec list using
   * #fs_codec_list_destroy() when done.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_NATIVE_CODECS,
      g_param_spec_boxed ("native-codecs",
        "List of native codecs",
        "A GList of FsCodecs that can be used for sending",
        FS_TYPE_CODEC_LIST,
        G_PARAM_READABLE));

  /**
   * FsSession:native-codecs-config:
   *
   * This is the current configuration list for the native codecs. It is usually
   * set by the user to specify the codec options and priorities. The user may
   * change this value during an ongoing session. Note that doing this can cause
   * the native-codecs to be changed. Therefore this requires the user to fetch
   * the new native-codecs and renegotiate them with the peers. It is a #GList
   * of #FsCodec. User must free this codec list using #fs_codec_list_destroy()
   * when done.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_NATIVE_CODECS_CONFIG,
      g_param_spec_boxed ("native-codecs-config",
        "List of user configuration for native codecs",
        "A GList of FsCodecs that allows user to set his codec options and"
        " priorities",
        FS_TYPE_CODEC_LIST,
        G_PARAM_READWRITE));

  /**
   * FsSession:negotiated-codecs:
   *
   * This list indicated what codecs have been successfully negotiated with the
   * session participants. This list can change based on participants
   * joining/leaving the session. It is a #GList of #FsCodec. User must free
   * this codec list using #fs_codec_list_destroy() when done.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_NEGOTIATED_CODECS,
      g_param_spec_boxed ("negotiated-codecs",
        "List of negotiated codecs",
        "A GList of FsCodecs indicating the codecs that have been successfully"
        " negotiated",
        FS_TYPE_CODEC_LIST,
        G_PARAM_READABLE));

  /**
   * FsSession:current-send-codec:
   *
   * Indicates the currently active send codec. A user can change the active
   * send codec by calling fs_session_set_send_codec(). The send codec could
   * also be automatically changed by Farsight. In both cases the
   * ::send-codec-changed signal will be emited. This property is an
   * #FsCodec. User must free the codec using fs_codec_destroy() when done.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_CURRENT_SEND_CODEC,
      g_param_spec_boxed ("current-send-codec",
        "Current active send codec",
        "An FsCodec indicating the currently active send codec",
        FS_TYPE_CODEC,
        G_PARAM_READABLE));

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
  signals[ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      fs_marshal_VOID__OBJECT_INT_STRING_STRING,
      G_TYPE_NONE, 4, G_TYPE_OBJECT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

  /**
   * FsSession::send-codec-changed:
   * @self: #FsSession that emitted the signal
   *
   * This signal is emitted when the active send codec has been changed
   * manually by the user or automatically for QoS purposes. The user should
   * look at the #current-send-codec property in the session to determine what
   * the new active codec is
   *
   */
  signals[SEND_CODEC_CHANGED] = g_signal_new ("send-codec-changed",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  /**
   * FsSession::new-negotiated-codecs:
   * @self: #FsSession that emitted the signal
   *
   * This signal is emitted when the negotiated codecs list has changed for this
   * session. This can happen when new remote codecs are added to the session
   * (i.e. When a session is being initialized or a new participant joins an
   * existing session). The user should look at the #negotiated-codecs property
   * to determine what the new negotiated codec list is.
   *
   */
  signals[NEW_NEGOTIATED_CODECS] = g_signal_new ("new-negotiated-codecs",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  gobject_class->dispose = fs_session_dispose;
  gobject_class->finalize = fs_session_finalize;

  g_type_class_add_private (klass, sizeof (FsSessionPrivate));
}

static void
fs_session_init (FsSession *self)
{
  /* member init */
  self->priv = FS_SESSION_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->priv->stream_list = g_ptr_array_new();
}

static void
fs_session_dispose (GObject *object)
{
  FsSession *self = FS_SESSION (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_session_finalize (GObject *object)
{
  FsSession *self = FS_SESSION (object);

  /* Let's check if we have any remaining streams in this
   * session, if we do we need to exit since this is a fatal error by the
   * user because it results in unusable children objects */
  if (self->priv->stream_list->len)
  {
    g_error ("You may not unref your Farsight Session object"
             " without first unrefing all underlying streams! Exiting");
  }

  g_ptr_array_free (self->priv->stream_list, TRUE);

  parent_class->finalize (object);
}

static void
fs_session_get_property (GObject *object,
                         guint prop_id,
                         GValue *value,
                         GParamSpec *pspec)
{
}

static void
fs_session_set_property (GObject *object,
                         guint prop_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
}

void
fs_session_error_forward (GObject *signal_src,
                          gint error_no, gchar *error_msg,
                          gchar *debug_msg, FsSession *session)
{
  /* We just need to forward the error signal including a ref to the stream
   * object (signal_src) */
  g_signal_emit (session, signals[ERROR], 0, signal_src, error_no, error_msg,
      debug_msg);
}

void
_remove_stream_ptr (FsSession *session, FsStream *stream)
{
  if (!g_ptr_array_remove (session->priv->stream_list, stream))
  {
    g_warning ("FsStream not found in stream ptr array");
  }
}

/**
 * fs_session_new_stream:
 * @session: an #FsSession
 * @participant: #FsParticipant of a participant for the new stream
 * @direction: #FsStreamDirection describing the direction of the new stream that will
 * be created for this participant
 * @transmitter: Name of the type of transmitter to use for this session
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function creates a stream for the given participant into the active session.
 *
 * Returns: the new #FsStream that has been created. User must unref the
 * #FsStream when the stream is ended. If an error occured, returns NULL.
 */
FsStream *
fs_session_new_stream (FsSession *session, FsParticipant *participant,
                       FsStreamDirection direction, gchar *transmitter,
                       GError **error)
{
  FsSessionClass *klass = FS_SESSION_GET_CLASS (session);
  FsStream *new_stream = NULL;
  g_return_val_if_fail (g_type_is_a (G_OBJECT_TYPE (session),
              FS_TYPE_SESSION), NULL);

  *error = NULL;

  if (klass->new_stream) {
    new_stream = klass->new_stream (session, participant, direction,
                                    transmitter, error);

    if (!new_stream)
      return NULL;

    /* Let's catch all stream errors and forward them */
    g_signal_connect (new_stream, "error",
        G_CALLBACK (fs_session_error_forward), session);

    /* Let's add a ptr to the new stream into our ptr array */
    g_ptr_array_add (session->priv->stream_list, new_stream);

    /* Let's add a weak reference to our new stream, this way if it gets
     * unrefed we can remove it from our ptr list */
    g_object_weak_ref (G_OBJECT (new_stream), (GWeakNotify)_remove_stream_ptr,
        session);
  } else {
    g_warning ("new_stream not defined for %s",
        G_OBJECT_TYPE_NAME (session));
  }
  return new_stream;
}

/**
 * fs_session_start_telephony_event:
 * @session: an #FsSession
 * @event: A #FsStreamDTMFEvent or another number defined at
 * http://www.iana.org/assignments/audio-telephone-event-registry
 * @volume: The volume in dBm0 without the negative sign. Should be between
 * 0 and 36. Higher values mean lower volume
 * @method: The method used to send the event
 *
 * This function will start sending a telephony event (such as a DTMF
 * tone) on the #FsSession. You have to call the function
 * #fs_session_stop_telephony_event() to stop it. 
 * This function will use any available method, if you want to use a specific
 * method only, use #fs_session_start_telephony_event_full()
 *
 * Returns: %TRUE if sucessful, it can return %FALSE if the #FsStream
 * does not support this telephony event.
 */
gboolean
fs_session_start_telephony_event (FsSession *session, guint8 event,
                                  guint8 volume, FsDTMFMethod method)
{
  FsSessionClass *klass = FS_SESSION_GET_CLASS (session);

  if (klass->start_telephony_event) {
    return klass->start_telephony_event (session, event, volume, method);
  } else {
    g_warning ("start_telephony_event not defined in class");
  }
  return FALSE;
}

/**
 * fs_session_stop_telephony_event:
 * @session: an #FsSession
 * @method: The method used to send the event
 *
 * This function will stop sending a telephony event started by
 * #fs_session_start_telephony_event(). If the event was being sent
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
  FsSessionClass *klass = FS_SESSION_GET_CLASS (session);

  if (klass->stop_telephony_event) {
    return klass->stop_telephony_event (session, method);
  } else {
    g_warning ("stop_telephony_event not defined in class");
  }
  return FALSE;
}

/**
 * fs_session_set_send_codec:
 * @session: an #FsSession
 * @send_codec: an #FsCodec representing the codec to send
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will set the currently being sent codec for all streams in this
 * session. The given #FsCodec must be taken directly from the #negotiated-codecs
 * property of the session. If the given codec is not in the negotiated codecs
 * list, @error will be set and %FALSE will be returned. The @send_codec will be
 * copied so it must be free'd using fs_codec_destroy() when done.
 *
 * Returns: %FALSE if the send codec couldn't be set.
 */
gboolean
fs_session_set_send_codec (FsSession *session, FsCodec *send_codec,
                           GError **error)
{
  FsSessionClass *klass = FS_SESSION_GET_CLASS (session);

  *error = NULL;

  if (klass->set_send_codec) {
    return klass->set_send_codec (session, send_codec, error);
  } else {
    g_warning ("set_send_codec not defined in class");
  }
  return FALSE;
}


void
fs_session_error (FsSession *session, gint error_no, gchar *error_msg,
                  gchar *debug_msg)
{
  g_signal_emit (session, signals[ERROR], 0, session, error_no, error_msg,
                 debug_msg, NULL);
}

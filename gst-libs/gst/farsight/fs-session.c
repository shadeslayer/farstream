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
 * SECTION:FsSession
 * @short_description: A gobject representing a farsight session
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
#include "fs-marshal.h"
#include "fs-codec.h"

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
  PROP_SINK_PAD,
  PROP_NATIVE_CODECS,
  PROP_NATIVE_CODECS_CONFIG,
  PROP_NEGOTIATED_CODECS,
  PROP_CURRENT_SEND_CODEC
};

struct _FsSessionPrivate
{
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
        "FsSession", &info, 0);
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
   * FsStream:media-type:
   *
   * The media-type of the session. This is either Audio or Video.
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
   * FsSession:sink-pad:
   *
   * The Gstreamer sink pad that must be used to send media data on this session
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
   * the session. It is a #GList of #FsCodec.
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
   * set by the user to specify the codec options and priorities. It is a #GList
   * of #FsCodec.
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
   * joining/leaving the session. It is a #GList of #FsCodec.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_NEGOTIATED_CODECS,
      g_param_spec_boxed ("negotiated-codecs",
        "List of negotiated codecs",
        "A GList of FsCodecs indicating the codecs that have been successfully"
        " negotiated",
        FS_TYPE_CODEC_LIST,
        G_PARAM_READWRITE));

  /**
   * FsSession:current-send-codec:
   *
   * Indicates the currently active send codec. A user can change the active
   * send codec by setting this property. The send codec could also be
   * automatically changed for QoS without user intervention. This property is a
   * #FsCodec.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_CURRENT_SEND_CODEC,
      g_param_spec_boxed ("current-send-codec",
        "Current active send codec",
        "An FsCodec indicating the currently active send codec",
        FS_TYPE_CODEC,
        G_PARAM_READWRITE));

  /**
   * FsSession::error:
   * @self: #FsSession that emmitted the signal
   * @errorno: The number of the error 
   * @message: Error message to be displayed to user
   * @message: Debugging error message
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
      fs_marshal_VOID__INT_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

  /**
   * FsSession::send-codec-changed:
   * @self: #FsSession that emmitted the signal
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
      g_cclosure_marshal_VOID,
      G_TYPE_NONE, 0);

  /**
   * FsSession::new-negotiated-codec:
   * @self: #FsSession that emmitted the signal
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
      g_cclosure_marshal_VOID,
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
  g_signal_handlers_destroy (object);

  parent_class->finalize (object);
}

/**
 * fs_session_add_participant
 * @session: #FsSession of a session in a conference
 * @participants: #FsParticipant of a participant in a conference
 * @direction: #FsDirection describing the direction of the new stream that will
 * be created for this participant
 *
 * This function adds a participant into an active session therefore creating
 * a new #FsStream for the given participant in the session
 *
 * Returns: the new #FsStream that has been created
 */
FsStream *
fs_session_add_participant (FsSession *session, FsParticipant *participant,
                            FsDirection direction)
{
  /* TODO make sure to link up to the error signal of the FsStream */
  /* TODO make sure to set the direction as a construtor param */
}

/**
 * farsight_session_start_telephony_event:
 * @session: an #FsSession
 * @ev: A #FarsightStreamDTMFEvent or another number defined at
 * http://www.iana.org/assignments/audio-telephone-event-registry
 * @volume: The volume in dBm0 without the negative sign. Should be between
 * 0 and 36. Higher values mean lower volume
 *
 * This function will start sending a telephony event (such as a DTMF
 * tone) on the #FsSession. You have to call the function
 * #fs_session_stop_telephony_event() to stop it. 
 * This function will use any available method, if you want to use a specific
 * method only, use #fs_session_start_telephony_event_full()
 *
 * Return value: %TRUE if sucessful, it can return %FALSE if the #FarsightStream
 * does not support this telephony event.
 */
gboolean
fs_session_start_telephony_event (FsSession *session, guint8 event,
                                  guint8 volume, FsDTMFMethod method)
{
}

/**
 * fs_session_start_telephony_event_full:
 * @session: a #FsSession
 * @ev: A #FarsightStreamDTMFEvent or another number defined at
 * http://www.iana.org/assignments/audio-telephone-event-registry
 * @volume: The volume in dBm0 without the negative sign. Should be between
 * 0 and 36. Higher values mean lower volume
 * @type: The way the event should be sent
 * @method: The method used to send the event
 *
 * This function will start sending a telephony event (such as a DTMF
 * tone) on the #FsSession, you have to call the function
 * #fs_session_stop_telephony_event_full() to stop it.
 *
 * Return value: %TRUE if sucessful, it can return %FALSE if the #FsSession
 * does not support this telephony event.
 */
gboolean
fs_session_start_telephony_event_full (FsSession *session, guint8 ev,
                                       guint8 volume,
                                       FsDTMFMethod method)
{
}

/**
 * fs_session_stop_telephony_event:
 * @session: an #FsSession
 *
 * This function will stop sending a telephony event started by
 * #fs_session_start_telephony_event(). If the event was being sent
 * for less than 50ms, it will be sent for 50ms minimum. If the
 * duration was a positive and the event is not over, it will cut it
 * short.
 *
 * Return value: %TRUE if sucessful, it can return %FALSE if the #FsSession
 * does not support telephony events or if no telephony event is being sent
 */
gboolean
fs_session_stop_telephony_event (FSession *session, FsDTMFMethod method)
{
}

/**
 * fs_session_stop_telephony_event_full:
 * @session: an #FsSession
 * @method: The method used to send the event, this MUST match the parameter
 * passed to fs_session_start_telephony_event_full().
 *
 * This function will stop sending a telephony event started by
 * fs_session_start_telephony_event_full(). If the event was being sent
 * for less than 50ms, it will be sent for 50ms minimum. If the
 * duration was a positive and the event is not over, it will cut it
 * short. The type parameters has to be the same type that was passed to
 *
 * Return value: %TRUE if sucessful, it can return %FALSE if the #FsSession
 * does not support telephony events or if no telephony event is being sent
 */
gboolean
farsight_stream_stop_telephony_event_full (FsSession *session,
                                           FsDTMFMethod method)
{
}

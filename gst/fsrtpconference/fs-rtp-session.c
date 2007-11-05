/*
 * Farsight2 - Farsight RTP Session
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-session.c - A Farsight RTP Session gobject
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
 * SECTION:fs-rtp-session
 * @short_description: A  RTP session in a #FsRtpConference
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "fs-rtp-session.h"

/* Signals */
enum
{
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

struct _FsRtpSessionPrivate
{
  /* List of Streams */
  GPtrArray *stream_list;

  gboolean disposed;
};

#define FS_RTP_SESSION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_SESSION, FsRtpSessionPrivate))

static void fs_rtp_session_class_init (FsRtpSessionClass *klass);
static void fs_rtp_session_init (FsRtpSession *self);
static void fs_rtp_session_dispose (GObject *object);
static void fs_rtp_session_finalize (GObject *object);

static void fs_rtp_session_get_property (GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec);
static void fs_rtp_session_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec);

static FsStream *fs_rtp_session_new_stream (FsSession *session,
                                            FsParticipant *participant,
                                            FsStreamDirection direction,
                                            gchar *transmitter,
                                            GError **error);
static gboolean fs_rtp_session_start_telephony_event (FsSession *session,
                                                      guint8 event,
                                                      guint8 volume,
                                                      FsDTMFMethod method);
static gboolean fs_rtp_session_stop_telephony_event (FsSession *session,
                                                     FsDTMFMethod method);
static gboolean fs_rtp_session_set_send_codec (FsSession *session,
                                               FsCodec *send_codec,
                                               GError **error);


static GObjectClass *parent_class = NULL;

//static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_rtp_session_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsRtpSessionClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_rtp_session_class_init,
      NULL,
      NULL,
      sizeof (FsRtpSession),
      0,
      (GInstanceInitFunc) fs_rtp_session_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsRtpSession", &info, G_TYPE_FLAG_ABSTRACT);
  }

  return type;
}

static void
fs_rtp_session_class_init (FsRtpSessionClass *klass)
{
  GObjectClass *gobject_class;
  FsSessionClass *session_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);
  session_class = FS_SESSION_CLASS (klass);


  gobject_class->set_property = fs_rtp_session_set_property;
  gobject_class->get_property = fs_rtp_session_get_property;

  session_class->new_stream = fs_rtp_session_new_stream;
  session_class->start_telephony_event = fs_rtp_session_start_telephony_event;
  session_class->stop_telephony_event = fs_rtp_session_stop_telephony_event;
  session_class->set_send_codec = fs_rtp_session_set_send_codec;

  /**
   * FsRtpSession:media-type:
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
   * FsRtpSession:sink-pad:
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
   * FsRtpSession:native-codecs:
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
   * FsRtpSession:native-codecs-config:
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
   * FsRtpSession:negotiated-codecs:
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
   * FsRtpSession:current-send-codec:
   *
   * Indicates the currently active send codec. A user can change the active
   * send codec by calling fs_rtp_session_set_send_codec(). The send codec could
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

  gobject_class->dispose = fs_rtp_session_dispose;
  gobject_class->finalize = fs_rtp_session_finalize;

  g_type_class_add_private (klass, sizeof (FsRtpSessionPrivate));
}

static void
fs_rtp_session_init (FsRtpSession *self)
{
  /* member init */
  self->priv = FS_RTP_SESSION_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_rtp_session_dispose (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rtp_session_finalize (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  parent_class->finalize (object);
}

static void
fs_rtp_session_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
}

static void
fs_rtp_session_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
}

/**
 * fs_rtp_session_new_stream:
 * @session: an #FsRtpSession
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
static FsStream *
fs_rtp_session_new_stream (FsSession *session, FsParticipant *participant,
                           FsStreamDirection direction, gchar *transmitter,
                           GError **error)
{
  FsStream *new_stream = NULL;

  return new_stream; // this shouldn't happen
}

/**
 * fs_rtp_session_start_telephony_event:
 * @session: an #FsRtpSession
 * @event: A #FsStreamDTMFEvent or another number defined at
 * http://www.iana.org/assignments/audio-telephone-event-registry
 * @volume: The volume in dBm0 without the negative sign. Should be between
 * 0 and 36. Higher values mean lower volume
 * @method: The method used to send the event
 *
 * This function will start sending a telephony event (such as a DTMF
 * tone) on the #FsRtpSession. You have to call the function
 * #fs_rtp_session_stop_telephony_event() to stop it. 
 * This function will use any available method, if you want to use a specific
 * method only, use #fs_rtp_session_start_telephony_event_full()
 *
 * Returns: %TRUE if sucessful, it can return %FALSE if the #FsStream
 * does not support this telephony event.
 */
static gboolean
fs_rtp_session_start_telephony_event (FsSession *session, guint8 event,
                                      guint8 volume, FsDTMFMethod method)
{
  return FALSE;
}

/**
 * fs_rtp_session_stop_telephony_event:
 * @session: an #FsRtpSession
 * @method: The method used to send the event
 *
 * This function will stop sending a telephony event started by
 * #fs_rtp_session_start_telephony_event(). If the event was being sent
 * for less than 50ms, it will be sent for 50ms minimum. If the
 * duration was a positive and the event is not over, it will cut it
 * short.
 *
 * Returns: %TRUE if sucessful, it can return %FALSE if the #FsRtpSession
 * does not support telephony events or if no telephony event is being sent
 */
static gboolean
fs_rtp_session_stop_telephony_event (FsSession *session, FsDTMFMethod method)
{
  return FALSE;
}

/**
 * fs_rtp_session_set_send_codec:
 * @session: an #FsRtpSession
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
static gboolean
fs_rtp_session_set_send_codec (FsSession *session, FsCodec *send_codec,
                               GError **error)
{
  return FALSE;
}

FsRtpSession *
fs_rtp_session_new (FsMediaType media_type)
{
  return g_object_new (FS_TYPE_RTP_SESSION, "media-type", media_type, NULL);
}

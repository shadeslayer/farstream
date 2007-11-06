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
#include "fs-rtp-stream.h"
#include "fs-rtp-participant.h"

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
  PROP_ID,
  PROP_SINK_PAD,
  PROP_NATIVE_CODECS,
  PROP_NATIVE_CODECS_CONFIG,
  PROP_NEGOTIATED_CODECS,
  PROP_CURRENT_SEND_CODEC,
  PROP_CONFERENCE
};

struct _FsRtpSessionPrivate
{
  FsMediaType media_type;
  guint id;

  /* We dont need a reference to this one per our reference model
   * This Session object can only exist while its parent conference exists
   */
  FsRtpConference *conference;

  /* We keep references to these elements
   */

  GstElement *media_sink_valve;
  GstElement *transmitter_rtp_tee;
  GstElement *transmitter_rtcp_tee;

  /* We dont keep explicit references to the pads, the Bin does that for us
   * only this element's methods can add/remote it
   */
  GstPad *media_sink_pad;

  /* Request pad to release on dispose */
  GstPad *rtpbin_send_rtp_sink;
  GstPad *rtpbin_send_rtcp_sink;
  GstPad *rtpbin_send_rtcp_src;

  GstPad *rtpbin_recv_rtp_sink;
  GstPad *rtpbin_recv_rtcp_sink;

  GError *construction_error;

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

static void fs_rtp_session_constructed (GObject *object);

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
  gobject_class->constructed = fs_rtp_session_constructed;

  session_class->new_stream = fs_rtp_session_new_stream;
  session_class->start_telephony_event = fs_rtp_session_start_telephony_event;
  session_class->stop_telephony_event = fs_rtp_session_stop_telephony_event;
  session_class->set_send_codec = fs_rtp_session_set_send_codec;

   g_object_class_override_property (gobject_class,
                                    PROP_MEDIA_TYPE,
                                    "media-type");
  g_object_class_override_property (gobject_class,
                                    PROP_ID,
                                    "id");
  g_object_class_override_property (gobject_class,
                                    PROP_SINK_PAD,
                                    "sink-pad");
  g_object_class_override_property (gobject_class,
                                    PROP_NATIVE_CODECS,
                                    "native-codecs");
  g_object_class_override_property (gobject_class,
                                    PROP_NATIVE_CODECS_CONFIG,
                                    "native-codecs-config");
  g_object_class_override_property (gobject_class,
                                    PROP_NEGOTIATED_CODECS,
                                    "negotiated-codecs");
  g_object_class_override_property (gobject_class,
                                    PROP_CURRENT_SEND_CODEC,
                                    "current-send-codec");

    g_object_class_install_property (gobject_class,
      PROP_CONFERENCE,
      g_param_spec_object ("conference",
        "The Conference this stream refers to",
        "This is a convience pointer for the Conference",
        FS_TYPE_RTP_CONFERENCE,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

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
  self->priv->construction_error = NULL;
}

static void
fs_rtp_session_dispose (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  if (self->priv->media_sink_valve) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->media_sink_valve);
    gst_element_set_state (self->priv->media_sink_valve, GST_STATE_NULL);
    gst_object_unref (self->priv->media_sink_valve);
    self->priv->media_sink_valve = NULL;
  }

  if (self->priv->media_sink_pad) {
    gst_pad_set_active (self->priv->media_sink_pad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (self->priv->conference),
      self->priv->media_sink_pad);
    self->priv->media_sink_pad = NULL;
  }

  if (self->priv->transmitter_rtp_tee) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->transmitter_rtp_tee);
    gst_element_set_state (self->priv->transmitter_rtp_tee, GST_STATE_NULL);
    gst_object_unref (self->priv->transmitter_rtp_tee);
    self->priv->transmitter_rtp_tee = NULL;
  }

  if (self->priv->transmitter_rtcp_tee) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->transmitter_rtcp_tee);
    gst_element_set_state (self->priv->transmitter_rtcp_tee, GST_STATE_NULL);
    gst_object_unref (self->priv->transmitter_rtcp_tee);
    self->priv->transmitter_rtcp_tee = NULL;
  }

  if (self->priv->rtpbin_send_rtcp_src) {
    gst_pad_set_active (self->priv->rtpbin_send_rtcp_src, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_send_rtcp_src);
    self->priv->rtpbin_send_rtcp_src = NULL;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rtp_session_finalize (GObject *object)
{
  parent_class->finalize (object);
}

static void
fs_rtp_session_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  switch (prop_id) {
    case PROP_MEDIA_TYPE:
      g_value_set_enum (value, self->priv->media_type);
      break;
    case PROP_ID:
      g_value_set_uint (value, self->priv->id);
      break;
    case PROP_SINK_PAD:
      g_value_set_object (value, self->priv->media_sink_pad);
      break;
    case PROP_CONFERENCE:
      g_value_take_object (value, self->priv->conference);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rtp_session_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  switch (prop_id) {
    case PROP_MEDIA_TYPE:
      self->priv->media_type = g_value_get_enum (value);
      break;
    case PROP_ID:
      self->priv->id = g_value_get_uint (value);
      break;
    case PROP_CONFERENCE:
      self->priv->conference = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rtp_session_constructed (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION_CAST (object);
  GstElement *valve = NULL;
  GstElement *tee = NULL;
  GstPad *valve_sink_pad = NULL;
  gchar *tmp;

  G_OBJECT_CLASS (parent_class)->constructed (object);

  if (self->priv->id == 0) {
    g_error ("You can no instantiate this element directly, you MUST"
      " call fs_rtp_session_new()");
    return;
  }

  tmp = g_strdup_printf ("valve_send_%d", self->priv->id);
  valve = gst_element_factory_make ("fsvalve", tmp);
  g_free (tmp);

  if (!valve) {
    self->priv->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not create the fsvalve element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), valve)) {
    self->priv->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the valve element to the FsRtpConference");
    gst_object_unref (valve);
    return;
  }

  g_object_set (G_OBJECT (valve), "drop", TRUE, NULL);
  gst_element_set_state (valve, GST_STATE_PLAYING);

  self->priv->media_sink_valve = gst_object_ref (valve);

  valve_sink_pad = gst_element_get_static_pad (valve, "sink");

  tmp = g_strdup_printf ("sink_%u", self->priv->id);
  self->priv->media_sink_pad = gst_ghost_pad_new (tmp, valve_sink_pad);
  g_free (tmp);

  gst_pad_set_active (self->priv->media_sink_pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (self->priv->conference),
    self->priv->media_sink_pad);

  gst_object_unref (valve_sink_pad);


  /* Now create the transmitter RTP tee */

  tmp = g_strdup_printf ("send_rtp_tee_%d", self->priv->id);
  tee = gst_element_factory_make ("tee", tmp);
  g_free (tmp);

  if (!tee) {
    self->priv->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not create the rtp tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), tee)) {
    self->priv->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the rtp tee element to the FsRtpConference");
    gst_object_unref (tee);
    return;
  }

  gst_element_set_state (tee, GST_STATE_PLAYING);

  self->priv->transmitter_rtp_tee = gst_object_ref (tee);


  /* Now create the transmitter RTCP tee */

  tmp = g_strdup_printf ("send_rtcp_tee_%d", self->priv->id);
  tee = gst_element_factory_make ("tee", tmp);
  g_free (tmp);

  if (!tee) {
    self->priv->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not create the rtcp tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), tee)) {
    self->priv->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the rtcp tee element to the FsRtpConference");
    gst_object_unref (tee);
    return;
  }

  gst_element_set_state (tee, GST_STATE_PLAYING);

  self->priv->transmitter_rtcp_tee = gst_object_ref (tee);

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
  FsRtpSession *self = FS_RTP_SESSION (session);
  FsRtpParticipant *rtpparticipant = NULL;
  FsStream *new_stream = NULL;

  if (!FS_IS_RTP_PARTICIPANT (participant)) {
    // *error = g_error_new ();
    return NULL;
  }
  rtpparticipant = FS_RTP_PARTICIPANT (participant);

  new_stream = FS_STREAM_CAST (fs_rtp_stream_new (self, rtpparticipant,
                                                  direction, NULL));

  return new_stream;
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
fs_rtp_session_new (FsMediaType media_type, FsRtpConference *conference,
                    guint id, GError **error)
{
  FsRtpSession *session = g_object_new (FS_TYPE_RTP_SESSION,
    "media-type", media_type,
    "conference", conference,
    "id", id,
    NULL);

  if (session->priv->construction_error) {
    *error = session->priv->construction_error;
    g_object_unref (session);
    return NULL;
  }

  return session;
}


GstCaps *
fs_rtp_session_request_pt_map (FsRtpSession *session, guint pt)
{
  return NULL;
}

FsStream *
fs_rtp_session_get_stream_by_id (FsRtpSession *session, guint stream_id)
{
  return NULL;
}

void
fs_rtp_session_link_network_sink (FsRtpSession *session, GstPad *src_pad)
{
  GstPad *transmitter_rtp_tee_sink_pad;
  GstPad *transmitter_rtcp_tee_sink_pad;
  GstPadLinkReturn ret;
  gchar *tmp;

  transmitter_rtp_tee_sink_pad =
    gst_element_get_static_pad (session->priv->transmitter_rtp_tee, "sink");
  g_assert (transmitter_rtp_tee_sink_pad);

  ret = gst_pad_link (src_pad, transmitter_rtp_tee_sink_pad);

  if (GST_PAD_LINK_FAILED (ret)) {
    tmp = g_strdup_printf ("Could not link pad %s (%p) with pad %s (%p)",
      GST_PAD_NAME (src_pad), GST_PAD_CAPS (src_pad),
      GST_PAD_NAME (transmitter_rtp_tee_sink_pad),
      GST_PAD_CAPS (transmitter_rtp_tee_sink_pad));
    fs_session_error (FS_SESSION (session), FS_SESSION_ERROR_CONSTRUCTION,
      "Could not link rtpbin network src to tee", tmp);
    g_free (tmp);

    gst_object_unref (transmitter_rtp_tee_sink_pad);
    return;
  }

  gst_object_unref (transmitter_rtp_tee_sink_pad);


  transmitter_rtcp_tee_sink_pad =
    gst_element_get_static_pad (session->priv->transmitter_rtcp_tee, "sink");
  g_assert (transmitter_rtcp_tee_sink_pad);

  tmp = g_strdup_printf ("send_rtcp_src_%u", session->priv->id);
  session->priv->rtpbin_send_rtcp_src =
    gst_element_get_request_pad (session->priv->conference->gstrtpbin, tmp);

  ret = gst_pad_link (session->priv->rtpbin_send_rtcp_src,
    transmitter_rtcp_tee_sink_pad);

  if (GST_PAD_LINK_FAILED (ret)) {
    tmp = g_strdup_printf ("Could not link pad %s (%p) with pad %s (%p)",
      GST_PAD_NAME (session->priv->rtpbin_send_rtcp_src),
      GST_PAD_CAPS (session->priv->rtpbin_send_rtcp_src),
      GST_PAD_NAME (transmitter_rtcp_tee_sink_pad),
      GST_PAD_CAPS (transmitter_rtcp_tee_sink_pad));
    fs_session_error (FS_SESSION (session), FS_SESSION_ERROR_CONSTRUCTION,
      "Could not link rtpbin network rtcp src to tee", tmp);
    g_free (tmp);

    gst_object_unref (transmitter_rtcp_tee_sink_pad);
    return;
  }

  gst_object_unref (transmitter_rtcp_tee_sink_pad);

}

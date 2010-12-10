/*
 * Farsight2 - Farsight Raw Session
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * fs-raw-session.c - A Farsight Raw Session gobject
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
 * SECTION:fs-raw-session
 * @short_description: A  Raw session in a #FsRawConference
 *
 * The transmitter parameters to the fs_session_new_stream() function are
 * used to set the initial value of the construct properties of the stream
 * object.
 *
 * The codecs preferences can not be modified. The codec should have the
 * encoding_name property set to the value returned by gst_caps_to_string.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-raw-session.h"

#include <string.h>

#include <gst/gst.h>
#include <gst/farsight/fs-transmitter.h>

#include "fs-raw-stream.h"
#include "fs-raw-participant.h"

#define GST_CAT_DEFAULT fsrawconference_debug

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
  PROP_CODEC_PREFERENCES,
  PROP_CODECS,
  PROP_CODECS_WITHOUT_CONFIG,
  PROP_CURRENT_SEND_CODEC,
  PROP_CODECS_READY,
  PROP_CONFERENCE,
  PROP_TOS
};



struct _FsRawSessionPrivate
{
  FsMediaType media_type;

  FsRawConference *conference;
  FsRawStream *stream;

  GError *construction_error;

  GstPad *media_sink_pad;
  GstElement *capsfilter;
  GList *codecs;
  FsCodec *send_codec;

  FsTransmitter *transmitter;

  guint tos; /* Protected by conf lock */

  GMutex *mutex; /* protects the conference */
};

G_DEFINE_TYPE (FsRawSession, fs_raw_session, FS_TYPE_SESSION);

#define FS_RAW_SESSION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAW_SESSION, FsRawSessionPrivate))

static void fs_raw_session_dispose (GObject *object);
static void fs_raw_session_finalize (GObject *object);

static void fs_raw_session_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_raw_session_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);

static void fs_raw_session_constructed (GObject *object);

static FsStream *fs_raw_session_new_stream (FsSession *session,
    FsParticipant *participant,
    FsStreamDirection direction,
    const gchar *transmitter,
    guint n_parameters,
    GParameter *parameters,
    GError **error);

static gchar **fs_raw_session_list_transmitters (FsSession *session);

static GType
fs_raw_session_get_stream_transmitter_type (FsSession *session,
    const gchar *transmitter);

static void _remove_stream (gpointer user_data,
                            GObject *where_the_object_was);

static void
fs_raw_session_class_init (FsRawSessionClass *klass)
{
  GObjectClass *gobject_class;
  FsSessionClass *session_class;

  gobject_class = (GObjectClass *) klass;
  session_class = FS_SESSION_CLASS (klass);

  gobject_class->set_property = fs_raw_session_set_property;
  gobject_class->get_property = fs_raw_session_get_property;
  gobject_class->constructed = fs_raw_session_constructed;

  session_class->new_stream = fs_raw_session_new_stream;
  session_class->list_transmitters = fs_raw_session_list_transmitters;
  session_class->get_stream_transmitter_type =
    fs_raw_session_get_stream_transmitter_type;

  g_object_class_override_property (gobject_class,
      PROP_MEDIA_TYPE, "media-type");
  g_object_class_override_property (gobject_class,
      PROP_ID, "id");
  g_object_class_override_property (gobject_class,
      PROP_SINK_PAD, "sink-pad");

  g_object_class_override_property (gobject_class,
    PROP_CODEC_PREFERENCES, "codec-preferences");
  g_object_class_override_property (gobject_class,
    PROP_CODECS, "codecs");
  g_object_class_override_property (gobject_class,
    PROP_CODECS_WITHOUT_CONFIG, "codecs-without-config");
  g_object_class_override_property (gobject_class,
    PROP_CURRENT_SEND_CODEC, "current-send-codec");
  g_object_class_override_property (gobject_class,
    PROP_CODECS_READY, "codecs-ready");
  g_object_class_override_property (gobject_class,
    PROP_TOS, "tos");

  g_object_class_install_property (gobject_class,
      PROP_CONFERENCE,
      g_param_spec_object ("conference",
          "The Conference this stream refers to",
          "This is a convience pointer for the Conference",
          FS_TYPE_RAW_CONFERENCE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->dispose = fs_raw_session_dispose;
  gobject_class->finalize = fs_raw_session_finalize;

  g_type_class_add_private (klass, sizeof (FsRawSessionPrivate));
}

static void
fs_raw_session_init (FsRawSession *self)
{
  /* member init */
  self->priv = FS_RAW_SESSION_GET_PRIVATE (self);
  self->priv->construction_error = NULL;

  self->priv->mutex = g_mutex_new ();

  self->priv->media_type = FS_MEDIA_TYPE_LAST + 1;
}


static FsRawConference *
fs_raw_session_get_conference (FsRawSession *self, GError **error)
{
  FsRawConference *conference;

  g_mutex_lock (self->priv->mutex);
  conference = self->priv->conference;
  if (conference)
    g_object_ref (conference);
  g_mutex_unlock (self->priv->mutex);

  if (!conference)
    g_set_error (error, FS_ERROR, FS_ERROR_DISPOSED,
        "Called function after session has been disposed");

  return conference;
}


static void
fs_raw_session_dispose (GObject *object)
{
  FsRawSession *self = FS_RAW_SESSION (object);
  GstBin *conferencebin = NULL;
  FsRawConference *conference = NULL;
  GstElement *valve = NULL;
  GstElement *capsfilter = NULL;
  FsTransmitter *transmitter = NULL;
  GstPad *media_sink_pad = NULL;

  g_mutex_lock (self->priv->mutex);
  conference = self->priv->conference;
  self->priv->conference = NULL;
  g_mutex_unlock (self->priv->mutex);

  if (!conference)
    goto out;

  conferencebin = GST_BIN (conference);

  if (!conferencebin)
    goto out;

  GST_OBJECT_LOCK (conference);
  valve = self->valve;
  self->valve = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (valve)
  {
    gst_element_set_locked_state (valve, TRUE);
    gst_bin_remove (conferencebin, valve);
    gst_element_set_state (valve, GST_STATE_NULL);
    gst_object_unref (valve);
  }

  GST_OBJECT_LOCK (conference);
  capsfilter = self->priv->capsfilter;
  self->priv->capsfilter = capsfilter;
  GST_OBJECT_UNLOCK (conference);

  if (capsfilter)
  {
    gst_element_set_locked_state (capsfilter, TRUE);
    gst_bin_remove (conferencebin, capsfilter);
    gst_element_set_state (capsfilter, GST_STATE_NULL);
    gst_object_unref (capsfilter);
  }

  GST_OBJECT_LOCK (conference);
  transmitter = self->priv->transmitter;
  self->priv->transmitter = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (transmitter)
  {
    g_object_unref (transmitter);
  }

  GST_OBJECT_LOCK (conference);
  media_sink_pad = self->priv->media_sink_pad;
  self->priv->media_sink_pad = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (media_sink_pad)
  {
    gst_element_remove_pad (GST_ELEMENT (conference), media_sink_pad);
    gst_pad_set_active (media_sink_pad, FALSE);
    gst_object_unref (media_sink_pad);
  }

  gst_object_unref (conference);

 out:

  G_OBJECT_CLASS (fs_raw_session_parent_class)->dispose (object);
}

static void
fs_raw_session_finalize (GObject *object)
{
  FsRawSession *self = FS_RAW_SESSION (object);

  if (self->priv->codecs)
    fs_codec_list_destroy (self->priv->codecs);

  if (self->priv->send_codec)
    fs_codec_destroy (self->priv->send_codec);

  g_mutex_free (self->priv->mutex);

  G_OBJECT_CLASS (fs_raw_session_parent_class)->finalize (object);
}

static void
fs_raw_session_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsRawSession *self = FS_RAW_SESSION (object);
  FsRawConference *conference = fs_raw_session_get_conference (self, NULL);

  if (!conference)
    return;

  GST_OBJECT_LOCK (conference);

  switch (prop_id)
  {
    case PROP_MEDIA_TYPE:
      g_value_set_enum (value, self->priv->media_type);
      break;
    case PROP_ID:
      g_value_set_uint (value, self->id);
      break;
    case PROP_CONFERENCE:
      g_value_set_object (value, self->priv->conference);
      break;
    case PROP_SINK_PAD:
      g_value_set_object (value, self->priv->media_sink_pad);
      break;
    case PROP_CODECS_READY:
      g_value_set_boolean (value, TRUE);
      break;
    case PROP_CODEC_PREFERENCES:
      /* There are no preferences, so return NULL */
      break;
    case PROP_CODECS:
    case PROP_CODECS_WITHOUT_CONFIG:
      g_value_set_boxed (value, self->priv->codecs);
      break;
    case PROP_CURRENT_SEND_CODEC:
      g_value_take_boxed (value, self->priv->send_codec);
      break;
    case PROP_TOS:
      g_value_set_uint (value, self->priv->tos);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (conference);
  gst_object_unref (conference);
}

static void
fs_raw_session_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  FsRawSession *self = FS_RAW_SESSION (object);
  FsRawConference *conference = fs_raw_session_get_conference (self, NULL);

  if (!conference && !(pspec->flags & G_PARAM_CONSTRUCT_ONLY))
    return;

  if (conference)
    GST_OBJECT_LOCK (conference);

  switch (prop_id)
  {
    case PROP_MEDIA_TYPE:
      self->priv->media_type = g_value_get_enum (value);
      break;
    case PROP_ID:
      self->id = g_value_get_uint (value);
      break;
    case PROP_CONFERENCE:
      self->priv->conference = FS_RAW_CONFERENCE (g_value_dup_object (value));
      break;
    case PROP_TOS:
      self->priv->tos = g_value_get_uint (value);
      if (self->priv->transmitter)
        g_object_set (self->priv->transmitter, "tos", self->priv->tos, NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (conference)
  {
    GST_OBJECT_UNLOCK (conference);
    gst_object_unref (conference);
  }
}

static void
fs_raw_session_constructed (GObject *object)
{
  FsRawSession *self = FS_RAW_SESSION (object);
  GstPad *pad;
  gchar *tmp;

  if (self->id == 0)
  {
    g_error ("You can not instantiate this element directly, you MUST"
      " call fs_raw_session_new ()");
    return;
  }

  g_assert (self->priv->conference);


  tmp = g_strdup_printf ("send_capsfilter_%u", self->id);
  self->priv->capsfilter = gst_element_factory_make ("capsfilter", tmp);
  g_free (tmp);

  if (!self->priv->capsfilter)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make send capsfilter");
    return;
  }

  gst_object_ref_sink (self->priv->capsfilter);

  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->capsfilter))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not add capsfilter to conference");
    gst_object_unref (self->priv->capsfilter);
    return;
  }

  if (!gst_element_sync_state_with_parent (self->priv->capsfilter))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not sync the send capsfilter's state with its parent");
    gst_bin_remove (GST_BIN (self->priv->conference), self->priv->capsfilter);
    return;
  }


  tmp = g_strdup_printf ("send_valve_%u", self->id);
  self->valve = gst_element_factory_make ("valve", tmp);
  g_free (tmp);

  if (!self->valve)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not make send valve");
    return;
  }

  gst_object_ref_sink (self->valve);

  if (!gst_bin_add (GST_BIN (self->priv->conference), self->valve))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not add valve to conference");
    gst_object_unref (self->valve);
    return;
  }

  g_object_set (G_OBJECT (self->valve), "drop", TRUE, NULL);

  if (!gst_element_sync_state_with_parent (self->valve))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not sync the send valve's state with its parent");
    gst_bin_remove (GST_BIN (self->priv->conference), self->valve);
    return;
  }

  if (!gst_element_link_pads (self->valve, "src",
          self->priv->capsfilter, "sink"))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link send valve and capsfilter");
    return;
  }


  pad = gst_element_get_static_pad (self->valve, "sink");
  tmp = g_strdup_printf ("sink_%u", self->id);
  self->priv->media_sink_pad = gst_ghost_pad_new (tmp, pad);
  g_free (tmp);
  gst_object_unref (pad);

  if (!self->priv->media_sink_pad)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not create sink ghost pad");
    return;
  }

  gst_object_ref_sink (self->priv->media_sink_pad);

  gst_pad_set_active (self->priv->media_sink_pad, TRUE);
  if (!gst_element_add_pad (GST_ELEMENT (self->priv->conference),
          self->priv->media_sink_pad))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not add sink pad to conference");
    gst_object_unref (self->priv->media_sink_pad);
    self->priv->media_sink_pad = NULL;
    return;
  }

  if (G_OBJECT_CLASS (fs_raw_session_parent_class)->constructed)
    G_OBJECT_CLASS (fs_raw_session_parent_class)->constructed (object);
}

static gboolean
_stream_new_remote_codecs (FsRawStream *stream,
    GList *codecs, GError **error, gpointer user_data)
{
  FsRawSession *self = FS_RAW_SESSION_CAST (user_data);
  FsRawConference *conference = fs_raw_session_get_conference (self, NULL);
  GstCaps *caps;
  FsCodec *codec = NULL;

  if (g_list_length (codecs) == 2)
    codec = codecs->next->data;
  else if (codecs && codecs->data)
    codec = codecs->data;

  if (!codec || !codec->encoding_name)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid codecs");
    return FALSE;
  }

  caps = fs_raw_codec_to_gst_caps (codec);

  if (!caps)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Codec has invalid caps");
    return FALSE;
  }

  if (self->priv->capsfilter)
    g_object_set (self->priv->capsfilter, "caps", caps, NULL);

  GST_OBJECT_LOCK (conference);
  if (!fs_codec_are_equal (self->priv->send_codec, codec))
  {
    if (self->priv->send_codec)
      fs_codec_destroy (self->priv->send_codec);

    self->priv->send_codec = fs_codec_copy (codec);

    GST_OBJECT_UNLOCK (conference);
    g_object_notify (G_OBJECT (self), "current-send-codec");
    gst_element_post_message (GST_ELEMENT (self->priv->conference),
        gst_message_new_element (GST_OBJECT (self->priv->conference),
            gst_structure_new ("farsight-send-codec-changed",
                "session", FS_TYPE_SESSION, self,
                "codec", FS_TYPE_CODEC, codec,
                "secondary-codecs", FS_TYPE_CODEC_LIST, NULL,
                NULL)));
    GST_OBJECT_LOCK (conference);
  }

  if (self->priv->codecs)
    fs_codec_list_destroy (self->priv->codecs);
  self->priv->codecs = fs_codec_list_copy (codecs);
  GST_OBJECT_UNLOCK (conference);

  g_object_notify (G_OBJECT (self), "codecs");

  gst_caps_unref (caps);
  gst_object_unref (conference);
  return TRUE;
}

static void
_remove_stream (gpointer user_data,
                GObject *where_the_object_was)
{
  FsRawSession *self = FS_RAW_SESSION (user_data);
  FsRawConference *conference = fs_raw_session_get_conference (self, NULL);
  FsTransmitter *transmitter = NULL;
  GstElement *src = NULL;
  GstElement *sink = NULL;

  if (!conference)
    return;

  GST_OBJECT_LOCK (conference);
  if (self->priv->stream == (FsRawStream *) where_the_object_was)
  {
    self->priv->stream = NULL;
    transmitter = self->priv->transmitter;
    self->priv->transmitter = NULL;
  }
  GST_OBJECT_UNLOCK (conference);

  g_object_get (transmitter,
      "gst-src", &src,
      "gst-sink", &sink,
      NULL);
  
  gst_element_set_locked_state (src, TRUE);
  gst_element_set_state (src, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self->priv->conference), src);

  gst_element_set_locked_state (sink, TRUE);
  gst_element_set_state (sink, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self->priv->conference), sink);

  gst_object_unref (src);
  gst_object_unref (sink);
  g_object_unref (transmitter);
  gst_object_unref (conference);
}

/**
 * fs_raw_session_new_stream:
 * @session: an #FsRawSession
 * @participant: #FsParticipant of a participant for the new stream
 * @direction: #FsStreamDirection describing the direction of the new stream
 * that will be created for this participant
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function creates a stream for the given participant into the active
 * session.
 *
 * Returns: the new #FsStream that has been created. User must unref the
 * #FsStream when the stream is ended. If an error occured, returns NULL.
 */
static FsStream *
fs_raw_session_new_stream (FsSession *session,
                           FsParticipant *participant,
                           FsStreamDirection direction,
                           const gchar *transmitter,
                           guint n_parameters,
                           GParameter *parameters,
                           GError **error)
{
  FsRawSession *self = FS_RAW_SESSION (session);
  FsRawParticipant *rawparticipant = NULL;
  FsStream *new_stream = NULL;
  FsRawConference *conference;
  FsTransmitter *fstransmitter;
  FsStreamTransmitter *stream_transmitter;
  GstElement *transmitter_sink = NULL;
  GstElement *transmitter_src = NULL;
  GstPad *transmitter_pad;

  if (!FS_IS_RAW_PARTICIPANT (participant))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "You have to provide a participant of type RAW");
    return NULL;
  }

  conference = fs_raw_session_get_conference (self, error);
  if (!conference)
    return NULL;

  GST_OBJECT_LOCK (conference);
  if (self->priv->stream)
    goto already_have_stream;
  GST_OBJECT_UNLOCK (conference);

  fstransmitter = fs_transmitter_new (transmitter, 1, 0, error);

  if (!fstransmitter)
  {
    gst_object_unref (conference);
    return NULL;
  }

  stream_transmitter = fs_transmitter_new_stream_transmitter (fstransmitter,
      participant, n_parameters, parameters, error);

  if (!stream_transmitter)
  {
    g_object_unref (fstransmitter);
    gst_object_unref (conference);
    return NULL;
  }

  g_object_get (fstransmitter, "gst-sink", &transmitter_sink, NULL);

  if (!transmitter_sink)
  {  
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Unable to get the sink element from the FsTransmitter");
    g_object_unref (fstransmitter);
    gst_object_unref (conference);
    return NULL;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), transmitter_sink))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the transmitter's source element"
        " for session %d to the conference bin", self->id);
    gst_object_unref (transmitter_sink);
    g_object_unref (fstransmitter);
    gst_object_unref (conference);
    return NULL;
  }

  if (!gst_element_link (self->priv->capsfilter, transmitter_sink))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the capsfilter and transmitter's"
        " sink element for session %d", self->id);
    g_object_unref (fstransmitter);
    gst_object_unref (conference);
    return NULL;
  }

  g_object_get (fstransmitter, "gst-src", &transmitter_src, NULL);

  if (!transmitter_src)
  {  
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Unable to get the source element from the FsTransmitter");
    g_object_unref (fstransmitter);
    gst_object_unref (conference);
    return NULL;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), transmitter_src))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the transmitter's source element"
        " for session %d to the conference bin", self->id);
    gst_object_unref (transmitter_src);
    g_object_unref (fstransmitter);
    gst_object_unref (conference);
    return NULL;
  }

  transmitter_pad = gst_element_get_static_pad (transmitter_src, "src1");

  if (!transmitter_pad)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Unable to get the srcpad from the FsTransmitter's gst-src");
    g_object_unref (fstransmitter);
    gst_object_unref (conference);
    return NULL;
  }

  rawparticipant = FS_RAW_PARTICIPANT (participant);

  new_stream = FS_STREAM_CAST (fs_raw_stream_new (self, rawparticipant,
      direction, conference, stream_transmitter, transmitter_pad,
      _stream_new_remote_codecs, self, error));

  if (new_stream)
  {
    GST_OBJECT_LOCK (conference);
    if (self->priv->stream)
    {
      g_object_unref (new_stream);
      g_object_unref (fstransmitter);
      goto already_have_stream;
    }
    self->priv->stream = (FsRawStream *) new_stream;
    g_object_weak_ref (G_OBJECT (new_stream), _remove_stream, self);

    if (self->priv->tos)
      g_object_set (fstransmitter, "tos", self->priv->tos, NULL);

    self->priv->transmitter = fstransmitter;

    GST_OBJECT_UNLOCK (conference);
  }
  else
  {
    g_object_unref (stream_transmitter);
    g_object_unref (fstransmitter);
  }
  gst_object_unref (conference);


  return new_stream;

 already_have_stream:
  GST_OBJECT_UNLOCK (conference);
  gst_object_unref (conference);

  g_set_error (error, FS_ERROR, FS_ERROR_ALREADY_EXISTS,
        "There already is a stream in this session");
  return NULL;
}

FsRawSession *
fs_raw_session_new (FsMediaType media_type,
    FsRawConference *conference,
    guint id,
    GError **error)
{
  FsRawSession *session = g_object_new (FS_TYPE_RAW_SESSION,
      "media-type", media_type,
      "conference", conference,
      "id", id,
      NULL);

  if (!session)
  {
    *error = g_error_new (FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create object");
  }
  else if (session->priv->construction_error)
  {
    g_propagate_error (error, session->priv->construction_error);
    g_object_unref (session);
    return NULL;
  }

  return session;
}

static gchar **
fs_raw_session_list_transmitters (FsSession *session)
{
  return fs_transmitter_list_available ();
}

static GType
fs_raw_session_get_stream_transmitter_type (FsSession *session,
    const gchar *transmitter)
{
  FsTransmitter *fstransmitter;
  GType transmitter_type;

  fstransmitter = fs_transmitter_new (transmitter, 1, 0, NULL);

  if (!fstransmitter)
    return G_TYPE_NONE;

  transmitter_type = fs_transmitter_get_stream_transmitter_type (fstransmitter);

  g_object_unref (fstransmitter);
  return transmitter_type;
}

/*
 * Farstream - Farstream Raw Session
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *  @author: Mike Ruprecht <mike.ruprecht@collabora.co.uk>
 *
 * fs-raw-session.c - A Farstream Raw Session gobject
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
#include <gst/farstream/fs-transmitter.h>

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
  GstElement *send_capsfilter;
  GList *codecs;
  FsCodec *send_codec;
  gboolean transmitter_sink_added;

  GstElement *send_tee;
  GstPad *send_tee_pad;

  GstElement *transform_bin;
  GstElement *fakesink;

  GstElement *send_valve;

  GstElement *recv_capsfilter;
  GstElement *recv_valve;
  gulong transmitter_recv_probe_id;
  GstPad *transmitter_src_pad;
  GstPad *src_ghost_pad;

  FsTransmitter *transmitter;

  guint tos; /* Protected by conf lock */

  GMutex *mutex; /* protects the conference */

#ifdef DEBUG_MUTEXES
  guint count;
#endif
};

G_DEFINE_TYPE (FsRawSession, fs_raw_session, FS_TYPE_SESSION);

#define FS_RAW_SESSION_GET_PRIVATE(o)                                   \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAW_SESSION, FsRawSessionPrivate))

#ifdef DEBUG_MUTEXES

#define FS_RAW_SESSION_LOCK(session)                            \
  do {                                                          \
    g_mutex_lock (FS_RAW_SESSION (session)->priv->mutex);       \
    g_assert (FS_RAW_SESSION (session)->priv->count == 0);      \
    FS_RAW_SESSION (session)->priv->count++;                    \
  } while (0);
#define FS_RAW_SESSION_UNLOCK(session)                          \
  do {                                                          \
    g_assert (FS_RAW_SESSION (session)->priv->count == 1);      \
    FS_RAW_SESSION (session)->priv->count--;                    \
    g_mutex_unlock (FS_RAW_SESSION (session)->priv->mutex);     \
  } while (0);
#define FS_RAW_SESSION_GET_LOCK(session)        \
  (FS_RAW_SESSION (session)->priv->mutex)
#else
#define FS_RAW_SESSION_LOCK(session)            \
  g_mutex_lock ((session)->priv->mutex)
#define FS_RAW_SESSION_UNLOCK(session)          \
  g_mutex_unlock ((session)->priv->mutex)
#define FS_RAW_SESSION_GET_LOCK(session)        \
  ((session)->priv->mutex)
#endif

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
    GError **error);

static gchar **fs_raw_session_list_transmitters (FsSession *session);

static GType fs_raw_session_get_stream_transmitter_type (FsSession *session,
    const gchar *transmitter);


static GstElement *_create_transform_bin (FsRawSession *self, GError **error);

static FsStreamTransmitter *_stream_get_stream_transmitter (FsRawStream *stream,
    const gchar *transmitter_name,
    FsParticipant *participant,
    GParameter *parameters,
    guint n_parameters,
    GError **error,
    gpointer user_data);


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

  FS_RAW_SESSION_LOCK (self);
  conference = self->priv->conference;
  if (conference)
    g_object_ref (conference);
  FS_RAW_SESSION_UNLOCK (self);

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
  GstElement *send_valve = NULL;
  GstElement *send_capsfilter = NULL;
  GstElement *transform = NULL;
  GstElement *send_tee = NULL;
  GstElement *fakesink = NULL;
  GstPad *send_tee_pad = NULL;
  FsTransmitter *transmitter = NULL;
  GstPad *media_sink_pad = NULL;

  FS_RAW_SESSION_LOCK (self);
  conference = self->priv->conference;
  self->priv->conference = NULL;
  FS_RAW_SESSION_UNLOCK (self);

  if (!conference)
    goto out;

  conferencebin = GST_BIN (conference);

  if (!conferencebin)
    goto out;

  GST_OBJECT_LOCK (conference);
  send_valve = self->priv->send_valve;
  self->priv->send_valve = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (send_valve)
  {
    gst_element_set_locked_state (send_valve, TRUE);
    gst_bin_remove (conferencebin, send_valve);
    gst_element_set_state (send_valve, GST_STATE_NULL);
    gst_object_unref (send_valve);
  }

  GST_OBJECT_LOCK (conference);
  send_capsfilter = self->priv->send_capsfilter;
  self->priv->send_capsfilter = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (send_capsfilter)
  {
    gst_element_set_locked_state (send_capsfilter, TRUE);
    gst_bin_remove (conferencebin, send_capsfilter);
    gst_element_set_state (send_capsfilter, GST_STATE_NULL);
    gst_object_unref (send_capsfilter);
  }

  if (self->priv->stream)
  {
    FsStream *stream = FS_STREAM (self->priv->stream);
    fs_raw_session_remove_stream(self, stream);
    fs_stream_destroy (stream);
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

  GST_OBJECT_LOCK (conference);
  transform = self->priv->transform_bin;
  self->priv->transform_bin = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (transform)
  {
    gst_element_set_locked_state (transform, TRUE);
    gst_bin_remove (conferencebin, transform);
    gst_element_set_state (transform, GST_STATE_NULL);
    gst_object_unref (transform);
  }

  GST_OBJECT_LOCK (conference);
  fakesink = self->priv->fakesink;
  self->priv->fakesink = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (fakesink)
  {
    gst_element_set_locked_state (fakesink, TRUE);
    gst_bin_remove (conferencebin, fakesink);
    gst_element_set_state (fakesink, GST_STATE_NULL);
    gst_object_unref (fakesink);
  }

  GST_OBJECT_LOCK (conference);
  send_tee = self->priv->send_tee;
  self->priv->send_tee = NULL;

  send_tee_pad = self->priv->send_tee_pad;
  self->priv->send_tee_pad = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (send_tee)
  {
    gst_element_set_locked_state (send_tee, TRUE);
    gst_bin_remove (conferencebin, send_tee);
    gst_element_set_state (send_tee, GST_STATE_NULL);
    gst_object_unref (send_tee);
  }

  if (send_tee_pad != NULL)
    gst_object_unref (send_tee_pad);


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
    case PROP_CODEC_PREFERENCES:
      /* There are no preferences, so return NULL */
      break;
    case PROP_CODECS:
    case PROP_CODECS_WITHOUT_CONFIG:
      g_value_set_boxed (value, self->priv->codecs);
      break;
    case PROP_CURRENT_SEND_CODEC:
      g_value_set_boxed (value, self->priv->send_codec);
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
  self->priv->send_capsfilter = gst_element_factory_make ("capsfilter", tmp);
  g_free (tmp);

  if (!self->priv->send_capsfilter)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make send capsfilter");
    return;
  }

  gst_object_ref_sink (self->priv->send_capsfilter);

  if (!gst_bin_add (GST_BIN (self->priv->conference),
          self->priv->send_capsfilter))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not add capsfilter to conference");
    gst_object_unref (self->priv->send_capsfilter);
    self->priv->send_capsfilter = NULL;
    return;
  }

  if (!gst_element_sync_state_with_parent (self->priv->send_capsfilter))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not sync the send capsfilter's state with its parent");
    return;
  }

  self->priv->transform_bin = _create_transform_bin (self, NULL);

  if (self->priv->transform_bin == NULL)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not create transform bin");
    return;
  }

  gst_object_ref_sink (self->priv->transform_bin);
  if (!gst_bin_add (GST_BIN (self->priv->conference),
      self->priv->transform_bin))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not add transform bin to conference");
    gst_object_unref (self->priv->transform_bin);
    self->priv->transform_bin = NULL;
    return;
  }

  if (!gst_element_link_pads (self->priv->transform_bin, "src",
          self->priv->send_capsfilter, "sink"))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link send transformbin and capsfilter");
    return;
  }

  if (!gst_element_sync_state_with_parent (self->priv->transform_bin))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not sync the send transformbin's state with its parent");
    return;
  }

  tmp = g_strdup_printf ("send_tee_%u", self->id);
  self->priv->send_tee = gst_element_factory_make ("tee", tmp);
  g_free (tmp);

  if (!self->priv->send_tee)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not make send tee");
    return;
  }

  gst_object_ref_sink (self->priv->send_tee);

  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->send_tee))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not add send tee to conference");
    gst_object_unref (self->priv->send_tee);
    self->priv->send_tee = NULL;
    return;
  }

  self->priv->send_tee_pad = gst_element_get_request_pad (self->priv->send_tee,
    "src%d");

  if (self->priv->send_tee_pad == NULL)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not create send tee pad");
    return;
  }

  pad = gst_element_get_static_pad (self->priv->transform_bin, "sink");
  if (pad == NULL)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not get transformbin sink pad");
    return;
  }

  if (GST_PAD_LINK_FAILED (gst_pad_link (self->priv->send_tee_pad, pad)))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link send tee and transformbin");
    gst_object_unref (pad);
    return;
  }

  gst_object_unref (pad);

  self->priv->fakesink = gst_element_factory_make ("fakesink", NULL);
  if (!self->priv->fakesink)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not make fakesink");
    return;
  }

  g_object_set (self->priv->fakesink,
      "sync", FALSE,
      "async", FALSE,
      NULL);

  gst_object_ref_sink (self->priv->fakesink);
  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->fakesink))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not add fakesink to conference");
    gst_object_unref (self->priv->fakesink);
    self->priv->fakesink = NULL;
    return;
  }

  if (!gst_element_link_pads (self->priv->send_tee, "src%d",
          self->priv->fakesink, "sink"))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link send send tee and fakesink");
    return;
  }

  if (!gst_element_sync_state_with_parent (self->priv->fakesink))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not sync the fakesinks state with its parent");
    return;
  }

  if (!gst_element_sync_state_with_parent (self->priv->send_tee))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not sync the send valve's state with its parent");
    gst_bin_remove (GST_BIN (self->priv->conference), self->priv->send_tee);
    return;
  }

  tmp = g_strdup_printf ("send_valve_%u", self->id);
  self->priv->send_valve = gst_element_factory_make ("valve", tmp);
  g_free (tmp);

  if (!self->priv->send_valve)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not make send valve");
    return;
  }

  gst_object_ref_sink (self->priv->send_valve);

  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->send_valve))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not add valve to conference");
    gst_object_unref (self->priv->send_valve);
    self->priv->send_valve = NULL;
    return;
  }

  g_object_set (G_OBJECT (self->priv->send_valve), "drop", TRUE, NULL);

  if (!gst_element_sync_state_with_parent (self->priv->send_valve))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not sync the send valve's state with its parent");
    return;
  }

  if (!gst_element_link_pads (self->priv->send_valve, "src",
          self->priv->send_tee, "sink"))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link send valve and transformbin");
    return;
  }

  pad = gst_element_get_static_pad (self->priv->send_valve, "sink");
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

static GstElement *
_create_transform_bin (FsRawSession *self, GError **error)
{
  FsMediaType mtype = self->priv->media_type;

  if (mtype == FS_MEDIA_TYPE_AUDIO)
    return gst_parse_bin_from_description_full (
      "audioconvert ! audioresample ! audioconvert", TRUE, NULL,
      GST_PARSE_FLAG_NONE, error);
  else if (mtype == FS_MEDIA_TYPE_VIDEO)
    return gst_parse_bin_from_description_full ("ffmpegcolorspace ! videoscale",
        TRUE, NULL, GST_PARSE_FLAG_NONE, error);

  g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
    "No transform bin for this media type");
  return NULL;
}

static void
_stream_remote_codecs_changed (FsRawStream *stream, GParamSpec *pspec,
    FsRawSession *self)
{
  GList *codecs;
  GstCaps *caps;
  FsCodec *codec = NULL;
  GstPad *transform_pad;
  GError *error = NULL;
  GstElement *transform = NULL;
  FsRawConference *conference = fs_raw_session_get_conference (self, &error);
  gboolean changed;
  FsStreamDirection direction;

  if (conference == NULL)
    goto error;

  g_object_get (stream, "remote-codecs", &codecs, "direction", &direction,
      NULL);

  if (!codecs)
    return;

  if (g_list_length (codecs) == 2)
    codec = codecs->next->data;
  else
    codec = codecs->data;

  GST_OBJECT_LOCK (conference);
  transform = self->priv->transform_bin;
  self->priv->transform_bin = NULL;
  GST_OBJECT_UNLOCK (conference);

  /* Replace the transform bin */
  if (transform != NULL)
  {
    gst_element_set_locked_state (transform, TRUE);
    gst_element_set_state (transform, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (conference), transform);
    g_object_unref (transform);
  }

  transform = _create_transform_bin (self, &error);

  if (transform == NULL)
    goto error;
  gst_object_ref_sink (transform);

  if (!gst_bin_add (GST_BIN (conference), transform))
    goto error;

  caps = fs_raw_codec_to_gst_caps (codec);
  if (self->priv->send_capsfilter)
    g_object_set (self->priv->send_capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  if (!gst_element_link_pads (transform, "src",
          self->priv->send_capsfilter, "sink"))
    goto error;

  if (!gst_element_sync_state_with_parent (transform))
    goto error;

  transform_pad = gst_element_get_static_pad (transform, "sink");
  if (transform_pad == NULL)
    goto error;

  if (GST_PAD_LINK_FAILED (gst_pad_link (self->priv->send_tee_pad,
      transform_pad)))
    goto error;

  GST_OBJECT_LOCK (conference);
  self->priv->transform_bin = transform;
  transform = NULL;

  if (self->priv->codecs)
    fs_codec_list_destroy (self->priv->codecs);
  self->priv->codecs = codecs;

  if ((changed = !fs_codec_are_equal (self->priv->send_codec, codec)))
  {
    if (self->priv->send_codec)
      fs_codec_destroy (self->priv->send_codec);

    self->priv->send_codec = fs_codec_copy (codec);
  }

  codec = codecs->data;

  if (self->priv->recv_capsfilter)
  {
    GstElement *capsfilter = gst_object_ref (self->priv->recv_capsfilter);
    GstCaps *recv_caps;

    recv_caps = fs_raw_codec_to_gst_caps (codec);
    GST_OBJECT_UNLOCK (conference);
    g_object_set (capsfilter, "caps", recv_caps, NULL);
    gst_object_unref (capsfilter);
    GST_OBJECT_LOCK (conference);
    gst_caps_unref (recv_caps);
  }

  GST_OBJECT_UNLOCK (conference);

  fs_raw_session_update_direction (self, direction);

  if (changed)
  {
    g_object_notify (G_OBJECT (self), "current-send-codec");
    gst_element_post_message (GST_ELEMENT (self->priv->conference),
        gst_message_new_element (GST_OBJECT (self->priv->conference),
            gst_structure_new ("farstream-send-codec-changed",
                "session", FS_TYPE_SESSION, self,
                "codec", FS_TYPE_CODEC, codec,
                "secondary-codecs", FS_TYPE_CODEC_LIST, NULL,
                NULL)));
  }

  g_object_notify (G_OBJECT (self), "codecs");

  gst_object_unref (conference);
  return;

error:
  if (error != NULL)
    fs_session_emit_error (FS_SESSION (self), error->code, error->message);
  else
    fs_session_emit_error (FS_SESSION (self), FS_ERROR_INTERNAL,
        "Unable to change transform bin");

  if (conference != NULL)
    gst_object_unref (conference);

  if (transform != NULL)
    gst_object_unref (transform);
}

void
fs_raw_session_remove_stream (FsRawSession *self,
    FsStream *stream)
{
  FsRawConference *conference = fs_raw_session_get_conference (self, NULL);
  FsTransmitter *transmitter = NULL;
  GstElement *src = NULL;
  GstElement *sink = NULL;

  if (!conference)
    return;

  g_object_set (G_OBJECT (self->priv->send_valve), "drop", TRUE, NULL);

  GST_OBJECT_LOCK (conference);
  if (self->priv->stream == (FsRawStream *) stream)
  {
    self->priv->stream = NULL;
  }
  transmitter = self->priv->transmitter;
  self->priv->transmitter = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (!transmitter)
    return;
  g_object_get (transmitter,
      "gst-src", &src,
      "gst-sink", &sink,
      NULL);


  if (self->priv->transmitter_recv_probe_id)
  {
    if (self->priv->transmitter_src_pad)
      gst_pad_remove_data_probe (self->priv->transmitter_src_pad,
          self->priv->transmitter_recv_probe_id);
    self->priv->transmitter_recv_probe_id = 0;
  }


  gst_element_set_locked_state (src, TRUE);
  gst_element_set_state (src, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (conference), src);

  if (gst_object_has_ancestor (GST_OBJECT (sink), GST_OBJECT (conference)))
  {
    gst_element_set_locked_state (sink, TRUE);
    gst_element_set_state (sink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (conference), sink);
  }

  if (self->priv->transmitter_src_pad)
  {
    gst_object_unref (self->priv->transmitter_src_pad);
    self->priv->transmitter_src_pad = NULL;
  }

  if (self->priv->recv_valve)
  {
    gst_element_set_locked_state (self->priv->recv_valve, TRUE);
    gst_bin_remove (GST_BIN (conference), self->priv->recv_valve);
    gst_element_set_state (self->priv->recv_valve, GST_STATE_NULL);
    gst_object_unref (self->priv->recv_valve);
    self->priv->recv_valve = NULL;
  }

  if (self->priv->recv_capsfilter)
  {
    gst_element_set_locked_state (self->priv->recv_capsfilter, TRUE);
    gst_bin_remove (GST_BIN (conference), self->priv->recv_capsfilter);
    gst_element_set_state (self->priv->recv_capsfilter, GST_STATE_NULL);
    gst_object_unref (self->priv->recv_capsfilter);
    self->priv->recv_capsfilter = NULL;
  }

  if (self->priv->src_ghost_pad)
  {
    gst_element_remove_pad (GST_ELEMENT (conference),
        self->priv->src_ghost_pad);
    gst_pad_set_active (self->priv->src_ghost_pad, FALSE);
    gst_object_unref (self->priv->src_ghost_pad);
    self->priv->src_ghost_pad = NULL;
  }


  gst_object_unref (src);
  gst_object_unref (sink);
  g_object_unref (transmitter);
  gst_object_unref (conference);
}

static gboolean
_add_transmitter_sink (FsRawSession *self,
    GstElement *transmitter_sink,
    GError **error)
{
  if (!gst_bin_add (GST_BIN (self->priv->conference), transmitter_sink))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the transmitter's sink element"
        " for session %d to the conference bin", self->id);
    goto error;
  }

  if (!gst_element_sync_state_with_parent (transmitter_sink))
  {
    gst_bin_remove (GST_BIN (self->priv->conference), transmitter_sink);
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not sync the transmitter's sink element"
        " with its parent for session %d", self->id);
    goto error;
  }

  if (!gst_element_link (self->priv->send_capsfilter, transmitter_sink))
  {
    gst_bin_remove (GST_BIN (self->priv->conference), transmitter_sink);
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the capsfilter and transmitter's"
        " sink element for session %d", self->id);
    goto error;
  }

  return TRUE;

error:
  return FALSE;
}

void
fs_raw_session_update_direction (FsRawSession *self,
  FsStreamDirection direction)
{
  GError *error = NULL;
  FsRawConference *conference;

  conference = fs_raw_session_get_conference (self, &error);

  if (!conference)
  {
    fs_session_emit_error (FS_SESSION (self), error->code, error->message);
    g_clear_error (&error);
    return;
  }

  GST_OBJECT_LOCK (conference);

  /* Don't start sending before we have codecs */
  if (!self->priv->codecs)
  {
    GST_OBJECT_UNLOCK (conference);
    goto out;
  }

  if (self->priv->transmitter &&
      !self->priv->transmitter_sink_added &&
      direction & FS_DIRECTION_SEND)  {
    GstElement *transmitter_sink;

    GST_OBJECT_UNLOCK (conference);

    g_object_get (self->priv->transmitter, "gst-sink", &transmitter_sink, NULL);
    if (!transmitter_sink)
    {
      fs_session_emit_error (FS_SESSION (self), FS_ERROR_CONSTRUCTION,
          "Unable to get the sink element from the FsTransmitter");
      goto out;
    }

    if (!_add_transmitter_sink (self, transmitter_sink, &error))
    {
      gst_object_unref (transmitter_sink);
      fs_session_emit_error (FS_SESSION (self), error->code, error->message);
      g_clear_error (&error);
      goto out;
    }

    gst_object_unref (transmitter_sink);

    GST_OBJECT_LOCK (conference);
    self->priv->transmitter_sink_added = TRUE;
  }

  if (self->priv->recv_valve)
  {
    GstElement *valve = g_object_ref (self->priv->recv_valve);

    GST_OBJECT_UNLOCK (conference);
    g_object_set (valve,
        "drop", ! (direction & FS_DIRECTION_RECV), NULL);
    g_object_unref (valve);
    GST_OBJECT_LOCK (conference);
  }

  GST_OBJECT_UNLOCK (conference);

  if (direction & FS_DIRECTION_SEND)
    g_object_set (self->priv->send_valve, "drop", FALSE, NULL);
  else
    g_object_set (self->priv->send_valve, "drop", TRUE, NULL);

out:
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
    GError **error)
{
  FsRawSession *self = FS_RAW_SESSION (session);
  FsStream *new_stream = NULL;
  FsRawConference *conference;

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

  new_stream = FS_STREAM_CAST (fs_raw_stream_new (self,
          FS_RAW_PARTICIPANT (participant),
          direction, conference,
          _stream_get_stream_transmitter, self));

  GST_OBJECT_LOCK (conference);
  if (self->priv->stream)
    goto already_have_stream;


  self->priv->stream = (FsRawStream *) new_stream;

  GST_OBJECT_UNLOCK (conference);

  g_signal_connect_object (new_stream, "notify::remote-codecs",
      G_CALLBACK (_stream_remote_codecs_changed), self, 0);


done:
  gst_object_unref (conference);
  return new_stream;

already_have_stream:
  GST_OBJECT_UNLOCK (conference);
  g_set_error (error, FS_ERROR, FS_ERROR_ALREADY_EXISTS,
      "There already is a stream in this session");
  goto done;
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

static gboolean
_transmitter_pad_have_data_callback (GstPad *pad, GstBuffer *buffer,
    gpointer user_data)
{
  FsRawSession *self = FS_RAW_SESSION (user_data);
  FsRawConference *conference = fs_raw_session_get_conference (self, NULL);
  FsRawStream *stream;
  GstElement *recv_capsfilter = NULL;
  GstPad *ghostpad;
  GstPad *srcpad;
  gchar *padname;
  FsCodec *codec;

  if (!conference)
    return FALSE;

  GST_OBJECT_LOCK (conference);
  if (!self->priv->codecs ||
      !self->priv->recv_capsfilter ||
      !self->priv->transmitter_recv_probe_id)
  {
    GST_OBJECT_UNLOCK (conference);
    gst_object_unref (conference);
    return FALSE;
  }

  recv_capsfilter = gst_object_ref (self->priv->recv_capsfilter);
  gst_pad_remove_data_probe (pad, self->priv->transmitter_recv_probe_id);
  self->priv->transmitter_recv_probe_id = 0;
  codec = fs_codec_copy (self->priv->codecs->data);
  GST_OBJECT_UNLOCK (conference);

  srcpad = gst_element_get_static_pad (recv_capsfilter, "src");

  if (!srcpad)
  {
    GST_WARNING ("Unable to get recv_capsfilter (%p) srcpad", recv_capsfilter);
    goto error;
  }

  padname = g_strdup_printf ("src_%d", self->id);
  ghostpad = gst_ghost_pad_new_from_template (padname, srcpad,
      gst_element_class_get_pad_template (
        GST_ELEMENT_GET_CLASS (self->priv->conference),
        "src_%d"));
  g_free (padname);
  gst_object_unref (srcpad);

  gst_object_ref (ghostpad);

  if (!gst_pad_set_active (ghostpad, TRUE))
    GST_WARNING ("Unable to set ghost pad active");


  if (!gst_element_add_pad (GST_ELEMENT (self->priv->conference), ghostpad))
  {
    GST_WARNING ("Unable to add ghost pad to conference");

    gst_object_unref (ghostpad);
    gst_object_unref (ghostpad);
    goto error;
  }

  GST_OBJECT_LOCK (conference);
  self->priv->src_ghost_pad = ghostpad;
  stream = g_object_ref (self->priv->stream);
  GST_OBJECT_UNLOCK (conference);

  fs_stream_emit_src_pad_added (FS_STREAM (stream), ghostpad, codec);

  fs_codec_destroy (codec);
  g_object_unref (stream);
  gst_object_unref (conference);
  gst_object_unref (recv_capsfilter);

  return TRUE;

error:
  fs_codec_destroy (codec);
  gst_object_unref (conference);
  gst_object_unref (recv_capsfilter);

  return FALSE;
}




static FsStreamTransmitter *_stream_get_stream_transmitter (FsRawStream *stream,
    const gchar *transmitter_name,
    FsParticipant *participant,
    GParameter *parameters,
    guint n_parameters,
    GError **error,
    gpointer user_data)
{
  FsRawSession *self = user_data;
  FsTransmitter *fstransmitter = NULL;
  FsStreamTransmitter *stream_transmitter = NULL;
  GstElement *transmitter_src = NULL;
  FsRawConference *conference;
  gchar *tmp;
  GstElement *capsfilter;
  GstElement *valve;
  GstPad *transmitter_src_pad;

  conference = fs_raw_session_get_conference (self, error);
  if (!conference)
    return NULL;

  fstransmitter = fs_transmitter_new (transmitter_name, 1, 0, error);

  if (!fstransmitter)
    goto error;

  g_object_set (fstransmitter, "tos", self->priv->tos, NULL);

  stream_transmitter = fs_transmitter_new_stream_transmitter (fstransmitter,
      participant, n_parameters, parameters, error);

  if (!stream_transmitter)
    goto error;

  g_object_get (fstransmitter, "gst-src", &transmitter_src, NULL);
  g_assert (transmitter_src);

  if (!gst_bin_add (GST_BIN (conference), transmitter_src))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the transmitter's source element"
        " for session %d to the conference bin", self->id);
    gst_object_unref (transmitter_src);
    transmitter_src = NULL;
    goto error;
  }

  tmp = g_strdup_printf ("recv_capsfilter_%d", self->id);
  capsfilter = gst_element_factory_make ("capsfilter", tmp);
  g_free (tmp);

  if (!capsfilter)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create a capsfilter element for session %d", self->id);
    g_object_unref (capsfilter);
    goto error;
  }

  gst_object_ref (capsfilter);

  if (!gst_bin_add (GST_BIN (conference), capsfilter))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the capsfilter element for session %d", self->id);
    gst_object_unref (capsfilter);
    gst_object_unref (capsfilter);
    goto error;
  }
  self->priv->recv_capsfilter = capsfilter;

  if (gst_element_set_state (self->priv->recv_capsfilter, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not set the capsfilter element for session %d", self->id);
    goto error;
  }

  tmp = g_strdup_printf ("recv_valve_%d", self->id);
  valve = gst_element_factory_make ("valve", tmp);
  g_free (tmp);

  if (!valve) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create a valve element for session %d", self->id);
    goto error;
  }

  gst_object_ref (valve);

  if (!gst_bin_add (GST_BIN (conference), valve))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the valve element for session %d"
        " to the conference bin", self->id);
    gst_object_unref (valve);
    goto error;
  }

  g_object_set (valve, "drop", TRUE, NULL);

  self->priv->recv_valve = valve;

  if (gst_element_set_state (self->priv->recv_valve, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not set the valve element for session %d to the playing state",
        self->id);
    goto error;
  }

  if (!gst_element_link (self->priv->recv_valve, self->priv->recv_capsfilter))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the recv valve and the capsfilter");
    goto error;
  }

  if (!gst_element_link_pads (transmitter_src, "src1",
          valve, "sink"))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the recv_valve to the codec bin");
    goto error;
  }

  transmitter_src_pad = gst_element_get_static_pad (transmitter_src, "src1");

  GST_OBJECT_LOCK (conference);
  self->priv->transmitter = fstransmitter;
  self->priv->transmitter_src_pad = transmitter_src_pad;
  GST_OBJECT_UNLOCK (conference);

  self->priv->transmitter_recv_probe_id = gst_pad_add_data_probe (
      self->priv->transmitter_src_pad,
      G_CALLBACK (_transmitter_pad_have_data_callback), self);

  if (!gst_element_sync_state_with_parent (transmitter_src))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not sync the transmitter's source element"
        " with its parent for session %d", self->id);
    goto error;
  }

  gst_object_unref (transmitter_src);
  gst_object_unref (conference);

  return stream_transmitter;

error:
  if (self->priv->recv_valve)
  {
    gst_bin_remove (GST_BIN (conference), self->priv->recv_valve);
    self->priv->recv_valve = NULL;
  }

  if (self->priv->recv_capsfilter)
  {
    gst_bin_remove (GST_BIN (conference), self->priv->recv_capsfilter);
    self->priv->recv_capsfilter = NULL;
  }

  if (transmitter_src)
    gst_bin_remove (GST_BIN (conference), transmitter_src);

  if (stream_transmitter)
  {
    fs_stream_transmitter_stop (stream_transmitter);
    g_object_unref (stream_transmitter);
  }

  GST_OBJECT_LOCK (conference);
  fstransmitter = self->priv->transmitter;
  self->priv->transmitter = NULL;
  GST_OBJECT_UNLOCK (conference);

  if (fstransmitter)
    g_object_unref (fstransmitter);

  gst_object_unref (conference);

  return NULL;

}

/*
 * Farsight2 - Farsight Raw Stream
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * fs-raw-stream.c - A Farsight Raw Stream gobject
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
 * SECTION:fs-raw-stream
 * @short_description: A raw stream in a #FsRawSession in a #FsRawConference
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-raw-stream.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

#include <gst/gst.h>


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
  PROP_DIRECTION,
  PROP_PARTICIPANT,
  PROP_SESSION,
  PROP_CONFERENCE,
  PROP_STREAM_TRANSMITTER,
  PROP_REMOTE_CODECS,
  PROP_TRANSMITTER_PAD,
};



struct _FsRawStreamPrivate
{
  FsRawConference *conference;
  FsRawSession *session;
  FsRawParticipant *participant;
  FsStreamDirection direction;
  FsStreamTransmitter *stream_transmitter;
  GstElement *codecbin;
  GstElement *capsfilter;
  GstElement *recv_valve;
  GstPad *transmitter_pad;
  GstPad *src_pad;

  GList *remote_codecs;

  gulong blocking_id;

  GError *construction_error;

  stream_new_remote_codecs_cb new_remote_codecs_cb;
  gpointer user_data_for_cb;

  gulong local_candidates_prepared_handler_id;
  gulong new_active_candidate_pair_handler_id;
  gulong new_local_candidate_handler_id;
  gulong error_handler_id;
  gulong state_changed_handler_id;

  GMutex *mutex; /* protects the conference */
};


G_DEFINE_TYPE(FsRawStream, fs_raw_stream, FS_TYPE_STREAM);

#define FS_RAW_STREAM_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAW_STREAM, FsRawStreamPrivate))

static void fs_raw_stream_dispose (GObject *object);
static void fs_raw_stream_finalize (GObject *object);

static void fs_raw_stream_get_property (GObject *object,
                                        guint prop_id,
                                        GValue *value,
                                        GParamSpec *pspec);
static void fs_raw_stream_set_property (GObject *object,
                                        guint prop_id,
                                        const GValue *value,
                                        GParamSpec *pspec);

static void fs_raw_stream_constructed (GObject *object);

static void _local_candidates_prepared (
    FsStreamTransmitter *stream_transmitter,
    gpointer user_data);
static void _new_active_candidate_pair (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *candidate1,
    FsCandidate *candidate2,
    gpointer user_data);
static void _new_local_candidate (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *candidate,
    gpointer user_data);
static void _transmitter_error (
    FsStreamTransmitter *stream_transmitter,
    gint errorno,
    gchar *error_msg,
    gchar *debug_msg,
    gpointer user_data);
static void _state_changed (FsStreamTransmitter *stream_transmitter,
    guint component,
    FsStreamState state,
    gpointer user_data);

static gboolean fs_raw_stream_set_remote_candidates (FsStream *stream,
    GList *candidates,
    GError **error);
static gboolean fs_raw_stream_set_remote_codecs (FsStream *stream,
    GList *remote_codecs,
    GError **error);

static void
fs_raw_stream_class_init (FsRawStreamClass *klass)
{
  GObjectClass *gobject_class;
  FsStreamClass *stream_class = FS_STREAM_CLASS (klass);

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = fs_raw_stream_set_property;
  gobject_class->get_property = fs_raw_stream_get_property;
  gobject_class->constructed = fs_raw_stream_constructed;
  gobject_class->dispose = fs_raw_stream_dispose;
  gobject_class->finalize = fs_raw_stream_finalize;

  stream_class->set_remote_candidates = fs_raw_stream_set_remote_candidates;
  stream_class->set_remote_codecs = fs_raw_stream_set_remote_codecs;


  g_type_class_add_private (klass, sizeof (FsRawStreamPrivate));

  g_object_class_override_property (gobject_class,
      PROP_DIRECTION,
      "direction");
  g_object_class_override_property (gobject_class,
      PROP_PARTICIPANT,
      "participant");
  g_object_class_override_property (gobject_class,
      PROP_SESSION,
      "session");
  g_object_class_override_property (gobject_class,
      PROP_REMOTE_CODECS,
      "remote-codecs");

  g_object_class_install_property (gobject_class,
      PROP_CONFERENCE,
      g_param_spec_object ("conference",
          "The Conference this stream refers to",
          "This is a conveniance pointer for the Conference",
          FS_TYPE_RAW_CONFERENCE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
    PROP_TRANSMITTER_PAD,
    g_param_spec_object ("transmitter-pad",
      "The GstPad this stream is linked to",
      "This is the pad on which this stream will attach itself",
      GST_TYPE_PAD,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * FsRawStream:stream-transmitter:
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
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}

static void
fs_raw_stream_init (FsRawStream *self)
{
  /* member init */
  self->priv = FS_RAW_STREAM_GET_PRIVATE (self);

  self->priv->session = NULL;
  self->priv->participant = NULL;

  self->priv->direction = FS_DIRECTION_NONE;

  self->priv->mutex = g_mutex_new ();
}


static FsRawConference *
fs_raw_stream_get_conference (FsRawStream *self, GError **error)
{
  FsRawConference *conference;

  g_mutex_lock (self->priv->mutex);
  conference = self->priv->conference;
  if (conference)
    g_object_ref (conference);
  g_mutex_unlock (self->priv->mutex);

  if (!conference)
    g_set_error (error, FS_ERROR, FS_ERROR_DISPOSED,
        "Called function after stream has been disposed");

  return conference;
}

static void
fs_raw_stream_dispose (GObject *object)
{
  FsRawStream *self = FS_RAW_STREAM (object);
  FsRawConference *conference = fs_raw_stream_get_conference (self, NULL);
  FsStreamTransmitter *st;

  if (!conference)
    return;

  g_mutex_lock (self->priv->mutex);
  self->priv->conference = NULL;
  g_mutex_unlock (self->priv->mutex);

  if (self->priv->src_pad)
  {
    gst_pad_set_active (self->priv->src_pad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (conference), self->priv->src_pad);
    self->priv->src_pad = NULL;
  }


  if (self->priv->recv_valve)
  {
    gst_element_set_locked_state (self->priv->recv_valve, TRUE);
    gst_element_set_state (self->priv->recv_valve, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (conference), self->priv->recv_valve);
    self->priv->recv_valve = NULL;
  }

  if (self->priv->capsfilter)
  {
    gst_element_set_locked_state (self->priv->capsfilter, TRUE);
    gst_element_set_state (self->priv->capsfilter, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (conference), self->priv->capsfilter);
    self->priv->capsfilter = NULL;
  }

  if (self->priv->codecbin)
  {
    gst_element_set_locked_state (self->priv->codecbin, TRUE);
    gst_element_set_state (self->priv->codecbin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (conference), self->priv->codecbin);
    self->priv->codecbin = NULL;
  }

  if (self->priv->blocking_id)
  {
    if (self->priv->transmitter_pad)
      gst_pad_remove_data_probe (self->priv->transmitter_pad,
          self->priv->blocking_id);
    self->priv->blocking_id = 0;
  }

  if (self->priv->transmitter_pad)
  {
    gst_object_unref (self->priv->transmitter_pad);
    self->priv->transmitter_pad = NULL;
  }

  st = self->priv->stream_transmitter;
  self->priv->stream_transmitter = NULL;

  if (st)
  {
    g_signal_handler_disconnect (st,
        self->priv->local_candidates_prepared_handler_id);
    g_signal_handler_disconnect (st,
        self->priv->new_active_candidate_pair_handler_id);
    g_signal_handler_disconnect (st,
        self->priv->new_local_candidate_handler_id);
    g_signal_handler_disconnect (st,
        self->priv->error_handler_id);
    g_signal_handler_disconnect (st,
        self->priv->state_changed_handler_id);

    fs_stream_transmitter_stop (st);
    g_object_unref (st);
  }

  if (self->priv->participant)
  {
    g_object_unref (self->priv->participant);
    self->priv->participant = NULL;
  }

  if (self->priv->session)
  {
    g_object_unref (self->priv->session);
    self->priv->session = NULL;
  }

  gst_object_unref (conference);
  gst_object_unref (conference);

  G_OBJECT_CLASS (fs_raw_stream_parent_class)->dispose (object);
}

static void
fs_raw_stream_finalize (GObject *object)
{
  FsRawStream *self = FS_RAW_STREAM (object);

  fs_codec_list_destroy (self->priv->remote_codecs);

  g_mutex_free (self->priv->mutex);

  G_OBJECT_CLASS (fs_raw_stream_parent_class)->finalize (object);
}


static void
fs_raw_stream_get_property (GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
  FsRawStream *self = FS_RAW_STREAM (object);
  FsRawConference *conference = fs_raw_stream_get_conference (self, NULL);

  if (!conference &&
      !(pspec->flags & (G_PARAM_CONSTRUCT_ONLY | G_PARAM_CONSTRUCT)))
    return;

  if (conference)
    GST_OBJECT_LOCK (conference);

  switch (prop_id)
  {
    case PROP_SESSION:
      g_value_set_object (value, self->priv->session);
      break;
    case PROP_PARTICIPANT:
      g_value_set_object (value, self->priv->participant);
      break;
    case PROP_DIRECTION:
      g_value_set_flags (value, self->priv->direction);
      break;
    case PROP_CONFERENCE:
      g_value_set_object (value, self->priv->conference);
      break;
    case PROP_REMOTE_CODECS:
      g_value_set_boxed (value, self->priv->remote_codecs);
      break;
    case PROP_TRANSMITTER_PAD:
      g_value_set_object (value, self->priv->transmitter_pad);
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
fs_raw_stream_set_property (GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
  FsRawStream *self = FS_RAW_STREAM (object);
  FsRawConference *conference = fs_raw_stream_get_conference (self, NULL);

  if (!conference &&
      !(pspec->flags & (G_PARAM_CONSTRUCT_ONLY | G_PARAM_CONSTRUCT)))
    return;

  if (conference)
    GST_OBJECT_LOCK (conference);

  switch (prop_id)
  {
    case PROP_SESSION:
      self->priv->session = FS_RAW_SESSION (g_value_dup_object (value));
      break;
    case PROP_PARTICIPANT:
      self->priv->participant = FS_RAW_PARTICIPANT (g_value_dup_object (value));
      break;
    case PROP_DIRECTION:
      if (g_value_get_flags (value) != self->priv->direction)
      {
        GstElement *recv_valve = NULL;
        GstElement *session_valve = NULL;
        FsStreamTransmitter *st = NULL;

        if (!conference ||
            !self->priv->recv_valve ||
            !self->priv->session)
        {
          self->priv->direction = g_value_get_flags (value);
          break;
        }

        if (self->priv->recv_valve)
          recv_valve = gst_object_ref (self->priv->recv_valve);
        if (self->priv->session->valve)
          session_valve = gst_object_ref (self->priv->session->valve);
        if (self->priv->stream_transmitter)
          st = g_object_ref (self->priv->stream_transmitter);

        self->priv->direction = g_value_get_flags (value);

        GST_OBJECT_UNLOCK (conference);
        if (recv_valve)
          g_object_set (recv_valve, "drop",
              (self->priv->direction & FS_DIRECTION_RECV) ? FALSE : TRUE, NULL);
        if (session_valve)
          g_object_set (session_valve, "drop",
              (self->priv->direction & FS_DIRECTION_SEND) ? FALSE : TRUE, NULL);
        if (st)
          g_object_set (st, "sending",
              (self->priv->direction & FS_DIRECTION_SEND) ? TRUE : FALSE, NULL);
        GST_OBJECT_LOCK (conference);

        if (session_valve)
          gst_object_unref (session_valve);
        if (recv_valve)
          gst_object_unref (recv_valve);
        if (st)
          g_object_unref (st);
      }
      break;
    case PROP_CONFERENCE:
      self->priv->conference = FS_RAW_CONFERENCE (g_value_dup_object (value));
      break;
    case PROP_STREAM_TRANSMITTER:
      self->priv->stream_transmitter = g_value_get_object (value);
      break;
    case PROP_TRANSMITTER_PAD:
      self->priv->transmitter_pad = GST_PAD (g_value_dup_object (value));
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

static gboolean
_transmitter_pad_have_data_callback (GstPad *pad, GstMiniObject *miniobj,
    gpointer user_data)
{
  FsRawStream *self = FS_RAW_STREAM_CAST (user_data);
  gboolean ret = TRUE;
  gboolean remove = FALSE;

  if (!self->priv->remote_codecs || !self->priv->capsfilter)
  {
    ret = FALSE;
  }
  else if (GST_IS_BUFFER (miniobj))
  {
    GstCaps *caps;
    FsCodec *codec = self->priv->remote_codecs->data;
    caps = fs_codec_to_gst_caps (codec);

    if (!GST_IS_CAPS (caps))
      ret = FALSE;
    else
      remove = TRUE;

    gst_caps_unref (caps);
  }

  if (remove && self->priv->blocking_id)
  {
    GstPad *ghostpad;
    GstPad *srcpad;
    gchar *padname;

    gst_pad_remove_data_probe (pad, self->priv->blocking_id);
    self->priv->blocking_id = 0;

    srcpad = gst_element_get_static_pad (self->priv->capsfilter, "src");

    if (!srcpad)
    {
      GST_WARNING ("Unable to get capsfilter (%p) srcpad",
          self->priv->capsfilter);
      return FALSE;
    }

    padname = g_strdup_printf ("src_%d", self->priv->session->id);
    ghostpad = gst_ghost_pad_new_from_template (padname, srcpad,
        gst_element_class_get_pad_template (
            GST_ELEMENT_GET_CLASS (self->priv->conference),
            "src_%d"));
    g_free (padname);

    /* XXX Should this really be needed? */
    if (!gst_pad_set_active (ghostpad, TRUE))
      GST_WARNING ("Unable to set ghost pad active");

    
    if (!gst_element_add_pad (GST_ELEMENT (self->priv->conference), ghostpad))
    {
      GST_WARNING ("Unable to add ghost pad to conference");
      return FALSE;
    }

    fs_stream_emit_src_pad_added (FS_STREAM (self), ghostpad,
        self->priv->remote_codecs->data);
  }

  return ret;
}

static void
fs_raw_stream_constructed (GObject *object)
{
  FsRawStream *self = FS_RAW_STREAM_CAST (object);
  GstPad *valve_sink_pad = NULL;
  GstPadLinkReturn linkret;
  gchar *tmp;

  if (!self->priv->conference) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_INVALID_ARGUMENTS, "A Stream needs a conference object");
    return;
  }

  tmp = g_strdup_printf ("recv_capsfilter_%d", self->priv->session->id);
  self->priv->capsfilter = gst_element_factory_make ("capsfilter", tmp);
  g_free (tmp);

  if (!self->priv->capsfilter) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not create a capsfilter element for"
      " session %d", self->priv->session->id);
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->capsfilter)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not add the capsfilter element for"
      " session %d", self->priv->session->id);
    gst_object_unref (self->priv->capsfilter);
    return;
  }

  if (gst_element_set_state (self->priv->capsfilter, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not set the capsfilter element for"
      " session %d", self->priv->session->id);
    return;
  }


  tmp = g_strdup_printf ("recv_valve_%d", self->priv->session->id);
  self->priv->recv_valve = gst_element_factory_make ("valve", tmp);
  g_free (tmp);

  if (!self->priv->recv_valve) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not create a valve element for"
      " session %d", self->priv->session->id);
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->recv_valve))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not add the valve element for session"
      " %d to the conference bin", self->priv->session->id);
    gst_object_unref (self->priv->recv_valve);
    return;
  }

  if (gst_element_set_state (self->priv->recv_valve, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not set the valve element for session"
      " %d to the playing state", self->priv->session->id);
    return;
  }

  if (!gst_element_link (self->priv->recv_valve, self->priv->capsfilter))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION, "Could not link the recv valve"
        " and the capsfilter");
    return;
  }

  valve_sink_pad = gst_element_get_static_pad (self->priv->recv_valve, "sink");
  if (!valve_sink_pad)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not get the valve's sink pad");
    return;
  }

  linkret = gst_pad_link (self->priv->transmitter_pad, valve_sink_pad);

  gst_object_unref (valve_sink_pad);

  if (GST_PAD_LINK_FAILED (linkret))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link the recv_valve to the codec bin (%d)", linkret);
    return;
  }

  if (!self->priv->blocking_id)
    self->priv->blocking_id = gst_pad_add_data_probe (
        self->priv->transmitter_pad,
        G_CALLBACK (_transmitter_pad_have_data_callback), self);

  if (!self->priv->stream_transmitter) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "The Stream Transmitter has not been set");
    return;
  }

  g_object_set (self->priv->stream_transmitter, "sending",
      (self->priv->direction & FS_DIRECTION_SEND) ? TRUE : FALSE, NULL);

  self->priv->local_candidates_prepared_handler_id =
    g_signal_connect_object (self->priv->stream_transmitter,
        "local-candidates-prepared",
        G_CALLBACK (_local_candidates_prepared),
        self, 0);
  self->priv->new_active_candidate_pair_handler_id =
    g_signal_connect_object (self->priv->stream_transmitter,
        "new-active-candidate-pair",
        G_CALLBACK (_new_active_candidate_pair),
        self, 0);
  self->priv->new_local_candidate_handler_id =
    g_signal_connect_object (self->priv->stream_transmitter,
        "new-local-candidate",
        G_CALLBACK (_new_local_candidate),
        self, 0);
  self->priv->error_handler_id =
    g_signal_connect_object (self->priv->stream_transmitter,
        "error",
        G_CALLBACK (_transmitter_error),
        self, 0);
  self->priv->state_changed_handler_id =
    g_signal_connect_object (self->priv->stream_transmitter,
        "state-changed",
        G_CALLBACK (_state_changed),
        self, 0);

  if (!fs_stream_transmitter_gather_local_candidates (
          self->priv->stream_transmitter,
          &self->priv->construction_error))
  {
    if (!self->priv->construction_error)
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_INTERNAL,
          "Unknown error while gathering local candidates");
    return;
  }

  if (self->priv->recv_valve)
    g_object_set (self->priv->recv_valve, "drop",
        (self->priv->direction & FS_DIRECTION_RECV) ? FALSE : TRUE, NULL);
  if (self->priv->session->valve)
    g_object_set (self->priv->session->valve, "drop",
        (self->priv->direction & FS_DIRECTION_SEND) ? FALSE : TRUE, NULL);

  if (G_OBJECT_CLASS (fs_raw_stream_parent_class)->constructed)
    G_OBJECT_CLASS (fs_raw_stream_parent_class)->constructed (object);
}


static void
_local_candidates_prepared (FsStreamTransmitter *stream_transmitter,
    gpointer user_data)
{
  FsRawStream *self = FS_RAW_STREAM (user_data);
  GstElement *conf = GST_ELEMENT (fs_raw_stream_get_conference (self, NULL));

  if (!conf)
    return;

  gst_element_post_message (conf,
      gst_message_new_element (GST_OBJECT (conf),
          gst_structure_new ("farsight-local-candidates-prepared",
              "stream", FS_TYPE_STREAM, self,
              NULL)));

  gst_object_unref (conf);
}


static void
_new_active_candidate_pair (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *local_candidate,
    FsCandidate *remote_candidate,
    gpointer user_data)
{
  FsRawStream *self = FS_RAW_STREAM (user_data);
  GstElement *conf = GST_ELEMENT (fs_raw_stream_get_conference (self, NULL));

  if (!conf)
    return;

  gst_element_post_message (conf,
      gst_message_new_element (GST_OBJECT (conf),
          gst_structure_new ("farsight-new-active-candidate-pair",
              "stream", FS_TYPE_STREAM, self,
              "local-candidate", FS_TYPE_CANDIDATE, local_candidate,
              "remote-candidate", FS_TYPE_CANDIDATE, remote_candidate,
              NULL)));

  gst_object_unref (conf);
}


static void
_new_local_candidate (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *candidate,
    gpointer user_data)
{
  FsRawStream *self = FS_RAW_STREAM (user_data);
  GstElement *conf = GST_ELEMENT (fs_raw_stream_get_conference (self, NULL));

  if (!conf)
    return;

  gst_element_post_message (conf,
      gst_message_new_element (GST_OBJECT (conf),
          gst_structure_new ("farsight-new-local-candidate",
              "stream", FS_TYPE_STREAM, self,
              "candidate", FS_TYPE_CANDIDATE, candidate,
              NULL)));

  gst_object_unref (conf);
}

static void
_transmitter_error (
    FsStreamTransmitter *stream_transmitter,
    gint errorno,
    gchar *error_msg,
    gchar *debug_msg,
    gpointer user_data)
{
  FsStream *stream = FS_STREAM (user_data);

  fs_stream_emit_error (stream, errorno, error_msg, debug_msg);
}

static void
_state_changed (FsStreamTransmitter *stream_transmitter,
    guint component,
    FsStreamState state,
    gpointer user_data)
{
  FsRawStream *self = FS_RAW_STREAM (user_data);
  GstElement *conf = GST_ELEMENT (fs_raw_stream_get_conference (self, NULL));

  if (!conf)
    return;

  gst_element_post_message (conf,
      gst_message_new_element (GST_OBJECT (conf),
          gst_structure_new ("farsight-component-state-changed",
              "stream", FS_TYPE_STREAM, self,
              "component", G_TYPE_UINT, component,
              "state", FS_TYPE_STREAM_STATE, state,
              NULL)));

  gst_object_unref (conf);

  if (component == 1 && state == FS_STREAM_STATE_FAILED)
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONNECTION_FAILED,
        "Could not establish connection", "Could not establish connection");
}

/**
 * fs_raw_stream_set_remote_candidate:
 */
static gboolean
fs_raw_stream_set_remote_candidates (FsStream *stream, GList *candidates,
                                     GError **error)
{
  FsRawStream *self = FS_RAW_STREAM (stream);
  FsRawConference *conference = fs_raw_stream_get_conference (self, error);
  FsStreamTransmitter *st = NULL;
  gboolean ret = FALSE;

  if (!conference)
    return FALSE;

  GST_OBJECT_LOCK (conference);
  if (self->priv->stream_transmitter)
    st = g_object_ref (self->priv->stream_transmitter);
  GST_OBJECT_UNLOCK (conference);

  if (st)
  {
    ret = fs_stream_transmitter_set_remote_candidates (st, candidates, error);
    g_object_unref (st);
  }

  gst_object_unref (conference);

  return ret;
}


/**
 * fs_raw_stream_set_remote_codecs:
 * @stream: an #FsStream
 * @remote_codecs: a #GList of #FsCodec representing the remote codecs
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will set the list of remote codecs for this stream. This list
 * should contain one or two codecs. The first codec in this list represents
 * the codec the remote side will be sending. The second codec, if given,
 * represents what should be sent to the remote side. If only one codec is
 * passed, and the codec to send to the remote side hasn't yet been chosen,
 * it will use the first and only codec in the list. If the list isn't in this
 * format, @error will be set and %FALSE will be returned. The @remote_codecs
 * list will be copied so it must be free'd using fs_codec_list_destroy()
 * when done.
 *
 * Returns: %FALSE if the remote codecs couldn't be set.
 */
static gboolean
fs_raw_stream_set_remote_codecs (FsStream *stream,
    GList *remote_codecs,
    GError **error)
{
  FsRawStream *self = FS_RAW_STREAM (stream);
  GList *item = NULL;
  FsMediaType media_type;
  FsRawSession *session;
  GList *remote_codecs_copy;
  FsRawConference *conf = fs_raw_stream_get_conference (self, error);

  if (!conf)
    return FALSE;

  GST_OBJECT_LOCK (conf);
  session = self->priv->session;
  if (session)
    g_object_ref (session);
  GST_OBJECT_UNLOCK (conf);

  if (!session)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_DISPOSED,
          "Called function after stream has been disposed");
      return FALSE;
    }

  if (remote_codecs == NULL) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "You can not set NULL remote codecs");
    goto error;
  }

  if (g_list_length (remote_codecs) > 2) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Too many codecs passed");
    goto error;
  }

  g_object_get (session, "media-type", &media_type, NULL);

  for (item = g_list_first (remote_codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    GstCaps *caps;

    if (!codec->encoding_name)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The codec must have an encoding name");
      goto error;
    }
    if (codec->id < 0 || codec->id > 128)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The codec id must be between 0 and 128 for %s",
          codec->encoding_name);
      goto error;
    }
    if (codec->media_type != media_type)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The media type for codec %s is not %s", codec->encoding_name,
          fs_media_type_to_string (media_type));
      goto error;
    }

    caps = fs_codec_to_gst_caps (codec);
    if (!caps)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The encoding name for codec %s is not valid GstCaps",
          codec->encoding_name);
      goto error;
    }
    gst_caps_unref (caps);
  }

  remote_codecs_copy = fs_codec_list_copy (remote_codecs);

  if (self->priv->new_remote_codecs_cb (self, remote_codecs_copy, error,
          self->priv->user_data_for_cb))
  {
    gboolean is_new = TRUE;

    GST_OBJECT_LOCK (conf);
    if (self->priv->remote_codecs)
    {
      is_new = !fs_codec_list_are_equal (self->priv->remote_codecs,
          remote_codecs);
      fs_codec_list_destroy (self->priv->remote_codecs);
    }
    self->priv->remote_codecs = remote_codecs_copy;
    GST_OBJECT_UNLOCK (conf);

    if (is_new)
    {
      FsCodec *codec;
      GstCaps *caps;

      codec = self->priv->remote_codecs->data;

      caps = fs_codec_to_gst_caps (codec);
      g_object_set (self->priv->capsfilter, "caps", caps, NULL);
      gst_caps_unref (caps);

      g_object_notify (G_OBJECT (stream), "remote-codecs");
    }
  } else {
    fs_codec_list_destroy (remote_codecs_copy);
    goto error;
  }

  g_object_unref (session);
  g_object_unref (conf);
  return TRUE;

 error:

  g_object_unref (session);
  g_object_unref (conf);
  return FALSE;
}


/**
 * fs_raw_stream_new:
 * @session: The #FsRawSession this stream is a child of
 * @participant: The #FsRawParticipant this stream is for
 * @direction: the initial #FsDirection for this stream
 *
 *
 * This function create a new stream
 *
 * Returns: the newly created string or NULL on error
 */

FsRawStream *
fs_raw_stream_new (FsRawSession *session,
    FsRawParticipant *participant,
    FsStreamDirection direction,
    FsRawConference *conference,
    FsStreamTransmitter *stream_transmitter,
    GstPad *transmitter_pad,
    stream_new_remote_codecs_cb new_remote_codecs_cb,
    gpointer user_data_for_cb,
    GError **error)
{
  FsRawStream *self;

  g_return_val_if_fail (session, NULL);
  g_return_val_if_fail (participant, NULL);
  g_return_val_if_fail (stream_transmitter, NULL);
  g_return_val_if_fail (new_remote_codecs_cb, NULL);

  self = g_object_new (FS_TYPE_RAW_STREAM,
      "session", session,
      "participant", participant,
      "direction", direction,
      "conference", conference,
      "stream-transmitter", stream_transmitter,
      "transmitter-pad", transmitter_pad,
      NULL);

  self->priv->new_remote_codecs_cb = new_remote_codecs_cb;
  self->priv->user_data_for_cb = user_data_for_cb;

  if (!self)
  {
    *error = g_error_new (FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create object");
    return NULL;
  }
  else if (self->priv->construction_error)
  {
    g_propagate_error (error, self->priv->construction_error);
    g_object_unref (self);
    return NULL;
  }

  return self;
}


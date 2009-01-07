/*
 * Farsight2 - Farsight RTP Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-stream.c - A Farsight RTP Stream gobject
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
 * SECTION:fs-rtp-stream
 * @short_description: A RTP stream in a #FsRtpSession in a #FsRtpConference
 *
 * This is the conjunction of a #FsRtpParticipant and a #FsRtpSession,
 * it is created by calling fs_session_new_stream() on a
 * #FsRtpSession.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtp-stream.h"

#include <gst/gst.h>

/* Signals */
enum
{
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

struct _FsRtpStreamPrivate
{
  FsRtpSession *session;
  FsStreamTransmitter *stream_transmitter;

  FsStreamDirection direction;

  /* Protected by the session mutex */
  guint recv_codecs_changed_idle_id;

  GError *construction_error;

  stream_new_remote_codecs_cb new_remote_codecs_cb;
  stream_known_source_packet_receive_cb known_source_packet_received_cb;
  gpointer user_data_for_cb;

  gboolean disposed;
};


G_DEFINE_TYPE(FsRtpStream, fs_rtp_stream, FS_TYPE_STREAM);

#define FS_RTP_STREAM_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_STREAM, FsRtpStreamPrivate))

static void fs_rtp_stream_dispose (GObject *object);
static void fs_rtp_stream_finalize (GObject *object);

static void fs_rtp_stream_get_property (GObject *object,
                                    guint prop_id,
                                    GValue *value,
                                    GParamSpec *pspec);
static void fs_rtp_stream_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec);
static void fs_rtp_stream_constructed (GObject *object);


static gboolean fs_rtp_stream_set_remote_candidates (FsStream *stream,
                                                     GList *candidates,
                                                     GError **error);
static gboolean fs_rtp_stream_force_remote_candidates (FsStream *stream,
    GList *remote_candidates,
    GError **error);

static gboolean fs_rtp_stream_set_remote_codecs (FsStream *stream,
                                                 GList *remote_codecs,
                                                 GError **error);

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
static void
_known_source_packet_received (FsStreamTransmitter *st,
    guint component,
    GstBuffer *buffer,
    FsRtpStream *self);
static void _transmitter_error (
    FsStreamTransmitter *stream_transmitter,
    gint errorno,
    gchar *error_msg,
    gchar *debug_msg,
    gpointer user_data);
static void _substream_codec_changed (FsRtpSubStream *substream,
    FsRtpStream *stream);
static void _state_changed (FsStreamTransmitter *stream_transmitter,
    guint component,
    FsStreamState state,
    gpointer user_data);


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_rtp_stream_class_init (FsRtpStreamClass *klass)
{
  GObjectClass *gobject_class;
  FsStreamClass *stream_class = FS_STREAM_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_rtp_stream_set_property;
  gobject_class->get_property = fs_rtp_stream_get_property;
  gobject_class->constructed = fs_rtp_stream_constructed;
  gobject_class->dispose = fs_rtp_stream_dispose;
  gobject_class->finalize = fs_rtp_stream_finalize;

  stream_class->set_remote_candidates = fs_rtp_stream_set_remote_candidates;
  stream_class->set_remote_codecs = fs_rtp_stream_set_remote_codecs;
  stream_class->force_remote_candidates = fs_rtp_stream_force_remote_candidates;


  g_type_class_add_private (klass, sizeof (FsRtpStreamPrivate));

  g_object_class_override_property (gobject_class,
                                    PROP_REMOTE_CODECS,
                                    "remote-codecs");
  g_object_class_override_property (gobject_class,
                                    PROP_NEGOTIATED_CODECS,
                                    "negotiated-codecs");
  g_object_class_override_property (gobject_class,
                                    PROP_CURRENT_RECV_CODECS,
                                    "current-recv-codecs");
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
                                    PROP_STREAM_TRANSMITTER,
                                   "stream-transmitter");
}

static void
fs_rtp_stream_init (FsRtpStream *self)
{
  /* member init */
  self->priv = FS_RTP_STREAM_GET_PRIVATE (self);

  self->priv->disposed = FALSE;
  self->priv->session = NULL;
  self->participant = NULL;
  self->priv->stream_transmitter = NULL;

  self->priv->direction = FS_DIRECTION_NONE;
}

static void
fs_rtp_stream_dispose (GObject *object)
{
  FsRtpStream *self = FS_RTP_STREAM (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  if (self->priv->stream_transmitter) {
    fs_stream_transmitter_stop (self->priv->stream_transmitter);
    g_object_unref (self->priv->stream_transmitter);
    self->priv->stream_transmitter = NULL;
  }

  if (self->priv->recv_codecs_changed_idle_id)
  {
    g_source_remove (self->priv->recv_codecs_changed_idle_id);
    self->priv->recv_codecs_changed_idle_id = 0;
  }

  if (self->substreams) {
    g_list_foreach (self->substreams, (GFunc) g_object_unref, NULL);
    g_list_free (self->substreams);
    self->substreams = NULL;
  }

  if (self->participant) {
    g_object_unref (self->participant);
    self->participant = NULL;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  if (self->priv->session)
  {
    g_object_unref (self->priv->session);
    self->priv->session = NULL;
  }

  parent_class->dispose (object);
}

static void
fs_rtp_stream_finalize (GObject *object)
{
  FsRtpStream *self = FS_RTP_STREAM (object);

  fs_codec_list_destroy (self->remote_codecs);
  fs_codec_list_destroy (self->negotiated_codecs);

  parent_class->finalize (object);
}

static gboolean
_codec_list_has_codec (GList *list, FsCodec *codec)
{
  for (; list; list = g_list_next (list))
  {
    FsCodec *listcodec = list->data;
    if (fs_codec_are_equal (codec, listcodec))
      return TRUE;
  }

  return FALSE;
}

static void
fs_rtp_stream_get_property (GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
  FsRtpStream *self = FS_RTP_STREAM (object);

  switch (prop_id) {
    case PROP_REMOTE_CODECS:
      FS_RTP_SESSION_LOCK (self->priv->session);
      g_value_set_boxed (value, self->remote_codecs);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    case PROP_NEGOTIATED_CODECS:
      FS_RTP_SESSION_LOCK (self->priv->session);
      g_value_set_boxed (value, self->negotiated_codecs);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    case PROP_SESSION:
      g_value_set_object (value, self->priv->session);
      break;
    case PROP_PARTICIPANT:
      g_value_set_object (value, self->participant);
      break;
    case PROP_STREAM_TRANSMITTER:
      g_value_set_object (value, self->priv->stream_transmitter);
      break;
    case PROP_DIRECTION:
      g_value_set_flags (value, self->priv->direction);
      break;
    case PROP_CURRENT_RECV_CODECS:
      {
        GList *codeclist = NULL;
        GList *substream_item;

        FS_RTP_SESSION_LOCK (self->priv->session);
        for (substream_item = g_list_first (self->substreams);
             substream_item;
             substream_item = g_list_next (substream_item))
        {
          FsRtpSubStream *substream = substream_item->data;

          if (substream->codec)
          {
            if (!_codec_list_has_codec (codeclist, substream->codec))
              codeclist = g_list_append (codeclist,
                  fs_codec_copy (substream->codec));
          }
        }

        g_value_take_boxed (value, codeclist);
        FS_RTP_SESSION_UNLOCK (self->priv->session);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rtp_stream_set_property (GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
  FsRtpStream *self = FS_RTP_STREAM (object);
  GList *item;

  switch (prop_id) {
    case PROP_SESSION:
      self->priv->session = FS_RTP_SESSION (g_value_dup_object (value));
      break;
    case PROP_PARTICIPANT:
      self->participant = FS_RTP_PARTICIPANT (g_value_dup_object (value));
      break;
    case PROP_STREAM_TRANSMITTER:
      self->priv->stream_transmitter =
        FS_STREAM_TRANSMITTER (g_value_get_object (value));
      break;
    case PROP_DIRECTION:
      self->priv->direction = g_value_get_flags (value);
      if (self->priv->stream_transmitter)
        g_object_set (self->priv->stream_transmitter, "sending",
            self->priv->direction & FS_DIRECTION_SEND, NULL);
      FS_RTP_SESSION_LOCK (self->priv->session);
      for (item = g_list_first (self->substreams);
           item;
           item = g_list_next (item))
        g_object_set (G_OBJECT (item->data),
            "receiving", ((self->priv->direction & FS_DIRECTION_RECV) != 0),
            NULL);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rtp_stream_constructed (GObject *object)
{
  FsRtpStream *self = FS_RTP_STREAM_CAST (object);

  if (!self->priv->stream_transmitter) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "The Stream Transmitter has not been set");
    return;
  }


  g_object_set (self->priv->stream_transmitter, "sending",
    self->priv->direction & FS_DIRECTION_SEND, NULL);

  g_signal_connect (self->priv->stream_transmitter,
      "local-candidates-prepared",
      G_CALLBACK (_local_candidates_prepared),
      self);
  g_signal_connect (self->priv->stream_transmitter,
      "new-active-candidate-pair",
      G_CALLBACK (_new_active_candidate_pair),
      self);
  g_signal_connect (self->priv->stream_transmitter,
      "new-local-candidate",
      G_CALLBACK (_new_local_candidate),
      self);
  g_signal_connect (self->priv->stream_transmitter,
      "error",
      G_CALLBACK (_transmitter_error),
      self);
  g_signal_connect (self->priv->stream_transmitter,
      "known-source-packet-received",
      G_CALLBACK (_known_source_packet_received),
      self);
  g_signal_connect (self->priv->stream_transmitter,
      "state-changed",
      G_CALLBACK (_state_changed),
      self);

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

  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
}


/**
 * fs_rtp_stream_set_remote_candidate:
 */
static gboolean
fs_rtp_stream_set_remote_candidates (FsStream *stream, GList *candidates,
                                     GError **error)
{
  FsRtpStream *self = FS_RTP_STREAM (stream);

  return fs_stream_transmitter_set_remote_candidates (
      self->priv->stream_transmitter, candidates, error);
}

/**
 * fs_rtp_stream_force_remote_candidates
 *
 * Implement FsStream -> force_remote_candidates
 * by calling the same function in the stream transmittrer
 */

static gboolean
fs_rtp_stream_force_remote_candidates (FsStream *stream,
    GList *remote_candidates,
    GError **error)
{
  FsRtpStream *self = FS_RTP_STREAM (stream);

  return fs_stream_transmitter_force_remote_candidates (
      self->priv->stream_transmitter, remote_candidates,
      error);
}


/**
 * fs_rtp_stream_set_remote_codecs:
 * @stream: an #FsStream
 * @remote_codecs: a #GList of #FsCodec representing the remote codecs
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will set the list of remote codecs for this stream. If
 * the given remote codecs couldn't be negotiated with the list of local
 * codecs or already negotiated codecs for the corresponding #FsSession, @error
 * will be set and %FALSE will be returned. The @remote_codecs list will be
 * copied so it must be free'd using fs_codec_list_destroy() when done.
 *
 * Returns: %FALSE if the remote codecs couldn't be set.
 */
static gboolean
fs_rtp_stream_set_remote_codecs (FsStream *stream,
                                 GList *remote_codecs, GError **error)
{
  FsRtpStream *self = FS_RTP_STREAM (stream);
  GList *item = NULL;
  FsMediaType media_type;

  if (remote_codecs == NULL) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "You can not set NULL remote codecs");
    goto error;
  }

  g_object_get (self->priv->session, "media-type", &media_type, NULL);

  for (item = g_list_first (remote_codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;

    if (!codec->encoding_name)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The codec must have an encoding name");
      goto error;
    }
    if (codec->id < 0 || codec->id > 128)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The codec id must be between 0 ans 128 for %s",
          codec->encoding_name);
      goto error;
    }
    if (codec->clock_rate == 0)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The codec %s must have a non-0 clock rate", codec->encoding_name);
      goto error;
    }
    if (codec->media_type != media_type)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The media type for codec %s is not %s", codec->encoding_name,
          fs_media_type_to_string (media_type));
      goto error;
    }
  }

  if (self->priv->new_remote_codecs_cb (self, remote_codecs, error,
          self->priv->user_data_for_cb))
  {
    FS_RTP_SESSION_LOCK (self->priv->session);
    if (self->remote_codecs)
      fs_codec_list_destroy (self->remote_codecs);
    self->remote_codecs = fs_codec_list_copy (remote_codecs);
    FS_RTP_SESSION_UNLOCK (self->priv->session);
  } else {
    goto error;
  }

  return TRUE;

 error:
  return FALSE;
}

/**
 * fs_rtp_stream_new:
 * @session: The #FsRtpSession this stream is a child of
 * @participant: The #FsRtpParticipant this stream is for
 * @direction: the initial #FsDirection for this stream
 * @stream_transmitter: the #FsStreamTransmitter for this stream, one
 *   reference to it will be eaten
 * @new_remote_codecs_cb: Callback called when the remote codecs change
 * (ie when fs_rtp_stream_set_remote_codecs() is called). One must hold
 * the session lock across calls.
 * @known_source_packet_received: Callback called when a packet from a
 * known source is receive.
 * @user_data: User data for the callbacks.
 * This function create a new stream
 *
 * Returns: the newly created string or NULL on error
 */

FsRtpStream *
fs_rtp_stream_new (FsRtpSession *session,
    FsRtpParticipant *participant,
    FsStreamDirection direction,
    FsStreamTransmitter *stream_transmitter,
    stream_new_remote_codecs_cb new_remote_codecs_cb,
    stream_known_source_packet_receive_cb known_source_packet_received_cb,
    gpointer user_data_for_cb,
    GError **error)
{
  FsRtpStream *self;

  g_return_val_if_fail (session, NULL);
  g_return_val_if_fail (participant, NULL);
  g_return_val_if_fail (stream_transmitter, NULL);
  g_return_val_if_fail (new_remote_codecs_cb, NULL);
  g_return_val_if_fail (known_source_packet_received_cb, NULL);

  self = g_object_new (FS_TYPE_RTP_STREAM,
    "session", session,
    "participant", participant,
    "direction", direction,
    "stream-transmitter", stream_transmitter,
    NULL);

  self->priv->new_remote_codecs_cb = new_remote_codecs_cb;
  self->priv->known_source_packet_received_cb = known_source_packet_received_cb;
  self->priv->user_data_for_cb = user_data_for_cb;

  if (self->priv->construction_error) {
    g_propagate_error (error, self->priv->construction_error);
    g_object_unref (self);
    return NULL;
  }

  return self;
}


static void
_local_candidates_prepared (FsStreamTransmitter *stream_transmitter,
    gpointer user_data)
{
  FsRtpStream *self = FS_RTP_STREAM (user_data);
  GstElement *conf = NULL;

  g_object_get (self->priv->session, "conference", &conf, NULL);

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
  FsRtpStream *self = FS_RTP_STREAM (user_data);
  GstElement *conf = NULL;

  g_object_get (self->priv->session, "conference", &conf, NULL);

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
  FsRtpStream *self = FS_RTP_STREAM (user_data);
  GstElement *conf = NULL;

  g_object_get (self->priv->session, "conference", &conf, NULL);

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
_known_source_packet_received (FsStreamTransmitter *st,
    guint component,
    GstBuffer *buffer,
    FsRtpStream *self)
{
  self->priv->known_source_packet_received_cb (self, component, buffer,
      self->priv->user_data_for_cb);
}

static void
_state_changed (FsStreamTransmitter *stream_transmitter,
    guint component,
    FsStreamState state,
    gpointer user_data)
{
  FsRtpStream *self = FS_RTP_STREAM (user_data);
  GstElement *conf = NULL;

  g_object_get (self->priv->session, "conference", &conf, NULL);

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
        "Could not establish connection", "Could not establish connection"
        " on the RTP component");
}

static void
_substream_src_pad_added (FsRtpSubStream *substream, GstPad *pad,
                          FsCodec *codec, gpointer user_data)
{
  FsStream *stream = FS_STREAM (user_data);

  fs_stream_emit_src_pad_added (stream, pad, codec);
}

static void
_substream_error (FsRtpSubStream *substream,
    gint errorno,
    gchar *error_msg,
    gchar *debug_msg,
    gpointer user_data)
{
  FsStream *stream = FS_STREAM (user_data);

  fs_stream_emit_error (stream, errorno, error_msg, debug_msg);
}

/**
 * fs_rtp_stream_add_substream_unlock:
 * @stream: a #FsRtpStream
 * @substream: the #FsRtpSubStream to associate with this stream
 *
 * This functions associates a substream with this stream
 *
 * You must enter this function with the session lock held and it will release
 * it.
 *
 * Returns: TRUE on success, FALSE on failure
 */
gboolean
fs_rtp_stream_add_substream_unlock (FsRtpStream *stream,
    FsRtpSubStream *substream,
    GError **error)
{
  gboolean ret = TRUE;

  stream->substreams = g_list_prepend (stream->substreams,
      substream);
  g_object_set (substream,
      "stream", stream,
      "receiving", ((stream->priv->direction & FS_DIRECTION_RECV) != 0),
      NULL);

  g_signal_connect (substream, "src-pad-added",
                    G_CALLBACK (_substream_src_pad_added), stream);
  g_signal_connect (substream, "codec-changed",
                    G_CALLBACK (_substream_codec_changed), stream);
  g_signal_connect (substream, "error",
                    G_CALLBACK (_substream_error), stream);

  /* Only announce a pad if it has a codec attached to it */
  if (substream->codec)
    ret = fs_rtp_sub_stream_add_output_ghostpad_unlock (substream, error);
  else
    FS_RTP_SESSION_UNLOCK (stream->priv->session);

  return ret;
}

/**
 *  _substream_codec_changed
 * @substream: The #FsRtpSubStream that may have a new receive codec
 * @stream: a #FsRtpStream
 *
 * This function checks if the specified substream introduces a new codec
 * not present in another substream and if it does, it emits a GstMessage
 * and the notify signal
 */

static void
_substream_codec_changed (FsRtpSubStream *substream,
    FsRtpStream *stream)
{
  GList *substream_item = NULL;
  GList *codeclist = NULL;

  FS_RTP_SESSION_LOCK (stream->priv->session);

  if (!substream->codec)
  {
    FS_RTP_SESSION_UNLOCK (stream->priv->session);
    return;
  }

  codeclist = g_list_prepend (NULL, fs_codec_copy (substream->codec));

  for (substream_item = stream->substreams;
       substream_item;
       substream_item = g_list_next (substream_item))
  {
    FsRtpSubStream *othersubstream = substream_item->data;

    if (othersubstream != substream)
    {
      if (othersubstream->codec)
      {
        if (fs_codec_are_equal (substream->codec, othersubstream->codec))
          break;

        if (!_codec_list_has_codec (codeclist, othersubstream->codec))
          codeclist = g_list_append (codeclist,
              fs_codec_copy (othersubstream->codec));
      }
    }
  }

  FS_RTP_SESSION_UNLOCK (stream->priv->session);

  if (substream_item == NULL)
  {
    GstElement *conf = NULL;

    g_object_notify (G_OBJECT (stream), "current-recv-codecs");

    g_object_get (stream->priv->session, "conference", &conf, NULL);

    gst_element_post_message (conf,
        gst_message_new_element (GST_OBJECT (conf),
            gst_structure_new ("farsight-recv-codecs-changed",
                "stream", FS_TYPE_STREAM, stream,
                "codecs", FS_TYPE_CODEC_LIST, codeclist,
                NULL)));

    gst_object_unref (conf);
  }

  fs_codec_list_destroy (codeclist);
}

/**
 * fs_rtp_stream_set_negotiated_codecs_unlock
 * @stream: a #FsRtpStream
 * @codecs: The #GList of #FsCodec to set for the negotiated-codecs property
 *
 * This function sets the value of the FsStream:negotiated-codecs property.
 * Unlike most other functions in this element, it TAKES the reference to the
 * codecs, so you have to give it its own copy.
 *
 * You must enter this function with the session lock held and it will release
 * it.
 */
void
fs_rtp_stream_set_negotiated_codecs_unlock (FsRtpStream *stream,
    GList *codecs)
{
  if (fs_codec_list_are_equal (stream->negotiated_codecs, codecs))
  {
    fs_codec_list_destroy (codecs);
    FS_RTP_SESSION_UNLOCK (stream->priv->session);
    return;
  }

  if (stream->negotiated_codecs)
    fs_codec_list_destroy (stream->negotiated_codecs);

  stream->negotiated_codecs = codecs;

  FS_RTP_SESSION_UNLOCK (stream->priv->session);

  g_object_notify (G_OBJECT (stream), "negotiated-codecs");
}

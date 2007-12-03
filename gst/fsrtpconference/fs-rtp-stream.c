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
  PROP_CURRENT_RECV_CODEC,
  PROP_DIRECTION,
  PROP_PARTICIPANT,
  PROP_SESSION,
  PROP_STREAM_TRANSMITTER
};

struct _FsRtpStreamPrivate
{
  FsRtpSession *session;
  FsRtpParticipant *participant;
  FsStreamTransmitter *stream_transmitter;

  FsStreamDirection direction;

  guint id;

  GList *remote_codecs;

  /* Protected by the session mutex */
  GList *substreams;

  GError *construction_error;

  gboolean disposed;
};

#define FS_RTP_STREAM_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_STREAM, FsRtpStreamPrivate))

static void fs_rtp_stream_class_init (FsRtpStreamClass *klass);
static void fs_rtp_stream_init (FsRtpStream *self);
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


static gboolean fs_rtp_stream_add_remote_candidate (FsStream *stream,
                                                    FsCandidate *candidate,
                                                    GError **error);
static void fs_rtp_stream_remote_candidates_added (FsStream *stream);
static gboolean fs_rtp_stream_select_candidate_pair (FsStream *stream,
                                                     gchar *lfoundation,
                                                     gchar *rfoundatihon,
                                                     GError **error);

static gboolean fs_rtp_stream_set_remote_codecs (FsStream *stream,
                                                 GList *remote_codecs,
                                                 GError **error);

static void fs_rtp_stream_local_candidates_prepared (
    FsStreamTransmitter *stream_transmitter,
    gpointer user_data);
static void fs_rtp_stream_new_active_candidate_pair (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *candidate1,
    FsCandidate *candidate2,
    gpointer user_data);
static void fs_rtp_stream_new_local_candidate (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *candidate,
    gpointer user_data);
static void fs_rtp_stream_transmitter_error (
    FsStreamTransmitter *stream_transmitter,
    gint errorno,
    gchar *error_msg,
    gchar *debug_msg,
    gpointer user_data);



static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_rtp_stream_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsRtpStreamClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_rtp_stream_class_init,
      NULL,
      NULL,
      sizeof (FsRtpStream),
      0,
      (GInstanceInitFunc) fs_rtp_stream_init
    };

    type = g_type_register_static (FS_TYPE_STREAM, "FsRtpStream", &info, 0);
  }

  return type;
}


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

  stream_class->add_remote_candidate = fs_rtp_stream_add_remote_candidate;
  stream_class->set_remote_codecs = fs_rtp_stream_set_remote_codecs;
  stream_class->remote_candidates_added = fs_rtp_stream_remote_candidates_added;
  stream_class->select_candidate_pair = fs_rtp_stream_select_candidate_pair;

#if 0
  g_object_class_override_property (gobject_class,
                                    PROP_SOURCE_PADS,
                                    "source-pads");
#endif

  g_object_class_override_property (gobject_class,
                                    PROP_REMOTE_CODECS,
                                    "remote-codecs");
  g_object_class_override_property (gobject_class,
                                    PROP_CURRENT_RECV_CODEC,
                                    "current-recv-codec");
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


  gobject_class->dispose = fs_rtp_stream_dispose;
  gobject_class->finalize = fs_rtp_stream_finalize;

  g_type_class_add_private (klass, sizeof (FsRtpStreamPrivate));
}

static void
fs_rtp_stream_init (FsRtpStream *self)
{
  /* member init */
  self->priv = FS_RTP_STREAM_GET_PRIVATE (self);

  self->priv->disposed = FALSE;
  self->priv->session = NULL;
  self->priv->participant = NULL;
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

  FS_RTP_SESSION_LOCK (self->priv->session);
  if (self->priv->substreams) {
    g_list_foreach (self->priv->substreams, (GFunc) gst_object_unref, NULL);
    g_list_free (self->priv->substreams);
    self->priv->substreams = NULL;
  }
  FS_RTP_SESSION_UNLOCK (self->priv->session);

  if (self->priv->participant) {
    g_object_unref (self->priv->participant);
    self->priv->participant = NULL;
  }

  if (self->priv->stream_transmitter) {
    g_object_unref (self->priv->stream_transmitter);
    self->priv->stream_transmitter = NULL;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rtp_stream_finalize (GObject *object)
{
  FsRtpStream *self = FS_RTP_STREAM (object);

  if (self->priv->remote_codecs)
    fs_codec_list_destroy (self->priv->remote_codecs);

  parent_class->finalize (object);
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
      g_value_set_boxed (value, self->priv->remote_codecs);
      break;
    case PROP_SESSION:
      g_value_set_object (value, self->priv->session);
      break;
    case PROP_PARTICIPANT:
      g_value_set_object (value, self->priv->participant);
      break;
    case PROP_STREAM_TRANSMITTER:
      g_value_set_object (value, self->priv->stream_transmitter);
      break;
    case PROP_DIRECTION:
      g_value_set_enum (value, self->priv->direction);
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

  switch (prop_id) {
    case PROP_SESSION:
      self->priv->session = FS_RTP_SESSION (g_value_get_object (value));
      break;
    case PROP_PARTICIPANT:
      self->priv->participant = FS_RTP_PARTICIPANT (g_value_dup_object (value));
      break;
    case PROP_STREAM_TRANSMITTER:
      self->priv->stream_transmitter =
        FS_STREAM_TRANSMITTER (g_value_dup_object (value));
      break;
    case PROP_DIRECTION:
      self->priv->direction = g_value_get_enum (value);
      if (self->priv->stream_transmitter) {
        g_object_set (self->priv->stream_transmitter, "sending",
          self->priv->direction & FS_DIRECTION_SEND, NULL);
      }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
fs_rtp_stream_constructed (GObject *object)
{
  FsRtpStream *self = FS_RTP_STREAM_CAST (object);

  G_OBJECT_CLASS (parent_class)->constructed (object);

  if (!self->priv->stream_transmitter) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "The Stream Transmitter has not been set");
    return;
  }


  g_object_set (self->priv->stream_transmitter, "sending",
    self->priv->direction & FS_DIRECTION_SEND, NULL);

  g_signal_connect (self->priv->stream_transmitter,
    "local-candidates-prepared",
    G_CALLBACK (fs_rtp_stream_local_candidates_prepared),
    self);
  g_signal_connect (self->priv->stream_transmitter,
    "new-active-candidate-pair",
    G_CALLBACK (fs_rtp_stream_new_active_candidate_pair),
    self);
  g_signal_connect (self->priv->stream_transmitter,
    "new-local-candidate",
    G_CALLBACK (fs_rtp_stream_new_local_candidate),
    self);
  g_signal_connect (self->priv->stream_transmitter,
    "error",
    G_CALLBACK (fs_rtp_stream_transmitter_error),
    self);

}


/**
 * fs_rtp_stream_add_remote_candidate:
 * @stream: an #FsStream
 * @candidate: an #FsCandidate struct representing a remote candidate
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function adds the given candidate into the remote candiate list of the
 * stream. It will be used for establishing a connection with the peer. A copy
 * will be made so the user must free the passed candidate using
 * fs_candidate_destroy() when done.
 */
static gboolean
fs_rtp_stream_add_remote_candidate (FsStream *stream, FsCandidate *candidate,
                                    GError **error)
{
  FsRtpStream *self = FS_RTP_STREAM (stream);

  return fs_stream_transmitter_add_remote_candidate (
      self->priv->stream_transmitter, candidate, error);
}


/**
 * fs_rtp_stream_remote_candidates_added:
 * @stream: a #FsStream
 *
 * Call this function when the remotes candidates have been set and the
 * checks can start. More candidates can be added afterwards
 */

static void
fs_rtp_stream_remote_candidates_added (FsStream *stream)
{
  FsRtpStream *self = FS_RTP_STREAM (stream);

  fs_stream_transmitter_remote_candidates_added (
      self->priv->stream_transmitter);
}

/**
 * fs_rtp_stream_select_candidate_pair:
 * @stream: a #FsStream
 * @lfoundation: The foundation of the local candidate to be selected
 * @rfoundation: The foundation of the remote candidate to be selected
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function selects one pair of candidates to be selected to start
 * sending media on.
 *
 * Returns: TRUE if the candidate pair could be selected, FALSE otherwise
 */

static gboolean
fs_rtp_stream_select_candidate_pair (FsStream *stream, gchar *lfoundation,
                                     gchar *rfoundation, GError **error)
{
  FsRtpStream *self = FS_RTP_STREAM (stream);

  return fs_stream_transmitter_select_candidate_pair (
      self->priv->stream_transmitter, lfoundation, rfoundation, error);
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

  if (remote_codecs == NULL) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "You can not set NULL remote codecs");
    return FALSE;
  }

  if (fs_rtp_session_negotiate_codecs (self->priv->session, remote_codecs,
      error)) {
    self->priv->remote_codecs = fs_codec_list_copy (remote_codecs);
    return TRUE;
  } else {
    return FALSE;
  }
}


FsRtpStream *
fs_rtp_stream_new (FsRtpSession *session,
                   FsRtpParticipant *participant,
                   FsStreamDirection direction,
                   FsStreamTransmitter *stream_transmitter,
                   GError **error)
{
  FsRtpStream *self = g_object_new (FS_TYPE_RTP_STREAM,
    "session", session,
    "participant", participant,
    "direction", direction,
    "stream-transmitter", stream_transmitter,
    NULL);

  if (self->priv->construction_error) {
    g_propagate_error (error, self->priv->construction_error);
    g_object_unref (self);
    return NULL;
  }

  return self;
}


static void
fs_rtp_stream_local_candidates_prepared (
    FsStreamTransmitter *stream_transmitter, gpointer user_data)
{
  FsRtpStream *self = FS_RTP_STREAM (user_data);

  g_signal_emit_by_name (self, "local-candidates-prepared", 0);
}


static void
fs_rtp_stream_new_active_candidate_pair (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *candidate1,
    FsCandidate *candidate2,
    gpointer user_data)
{
  FsRtpStream *self = FS_RTP_STREAM (user_data);

  g_signal_emit_by_name (self, "new-active-candidate-pair", 0,
    candidate1, candidate2);
}


static void
fs_rtp_stream_new_local_candidate (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *candidate,
    gpointer user_data)
{
  FsRtpStream *self = FS_RTP_STREAM (user_data);

  g_signal_emit_by_name (self, "new-local-candidate", 0, candidate);
}

static void
fs_rtp_stream_transmitter_error (
    FsStreamTransmitter *stream_transmitter,
    gint errorno,
    gchar *error_msg,
    gchar *debug_msg,
    gpointer user_data)
{
  FsRtpStream *self = FS_RTP_STREAM (user_data);

  g_signal_emit_by_name (self, "error", 0, errorno, error_msg, debug_msg);
}


/**
 * fs_rtp_stream_add_substream:
 * @stream: a #FsRtpStream
 * @substream: the #FsRtpSubStream to associate with this stream
 *
 * This functions associates a substream with this stream
 *
 * Returns: TRUE on success, FALSE on failure
 */
gboolean
fs_rtp_stream_add_substream (FsRtpStream *stream,
    FsRtpSubStream *substream,
    GError **error)
{
  GstPad *ghostpad;
  FsCodec *codec;

  ghostpad = fs_rtp_sub_stream_get_output_ghostpad (substream, error);

  if (!ghostpad)
    return FALSE;

  g_object_get (substream, "codec", &codec, NULL);

  FS_RTP_SESSION_LOCK (stream->priv->session);
  stream->priv->substreams = g_list_prepend (stream->priv->substreams,
      substream);
  FS_RTP_SESSION_UNLOCK (stream->priv->session);

  g_signal_emit_by_name (stream, "src-pad-added", ghostpad, codec);

  fs_codec_destroy (codec);
  gst_object_unref (ghostpad);

  return TRUE;
}

gboolean
fs_rtp_stream_knows_ssrc_locked (FsRtpStream *stream, guint32 ssrc)
{
  GList *elem;

  for (elem = g_list_first (stream->priv->substreams);
       elem;
       elem = g_list_next (elem))
  {
    guint32 substream_ssrc;

    g_object_get (elem->data, "ssrc", &substream_ssrc, NULL);
    if (substream_ssrc == ssrc)
      return TRUE;
  }

  return FALSE;
}

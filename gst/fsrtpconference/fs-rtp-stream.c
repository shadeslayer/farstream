/*
 * Farsight2 - Farsight RTP Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-stream.c - A Farsight RTP Stream gobject
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


static gboolean fs_rtp_stream_preload_recv_codec (FsStream *stream,
                                                  FsCodec *codec,
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
static void
fs_rtp_stream_transmitter_error (
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
  stream_class->preload_recv_codec = fs_rtp_stream_preload_recv_codec;
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

  if (!self->priv->stream_transmitter) {
    /* FIXME
    g_error_new (FS_STREAM_ERROR,
      FS_STREAM_ERROR_CONSTRUCTION,
      "The Stream Transmitter has not been set");
    */
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
 * fs_rtp_stream_preload_recv_codec:
 * @stream: an #FsStream
 * @codec: The #FsCodec to be preloaded
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will preload the codec corresponding to the given codec.
 * This codec must correspond exactly to one of the local-codecs returned by
 * the #FsSession that spawned this #FsStream. Preloading a codec is useful for
 * machines where loading the codec is slow. When preloading, decoding can start
 * as soon as a stream is received.
 *
 * Returns: TRUE of the codec could be preloaded, FALSE if there is an error
 */
static gboolean
fs_rtp_stream_preload_recv_codec (FsStream *stream, FsCodec *codec,
                                  GError **error)
{
  return FALSE;
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
  return FALSE;
}


FsRtpStream *
fs_rtp_stream_new (FsRtpSession *session,
                   FsRtpParticipant *participant,
                   FsStreamDirection direction,
                   FsStreamTransmitter *stream_transmitter)
{
  return g_object_new (FS_TYPE_RTP_STREAM,
                       "session", session,
                       "participant", participant,
                       "direction", direction,
                       "stream-transmitter", stream_transmitter,
                       NULL);
}


void
fs_rtp_stream_new_recv_pad (FsRtpStream *stream, GstPad *pad, guint pt)
{
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

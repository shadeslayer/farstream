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
static void fs_rtp_stream_add_remote_candidate (FsStream *stream,
                                                FsCandidate *candidate);
static gboolean fs_rtp_stream_preload_recv_codec (FsStream *stream,
                                                  FsCodec *codec,
                                                  GError **error);

static gboolean fs_rtp_stream_set_remote_codecs (FsStream *stream,
                                                 GList *remote_codecs,
                                                 GError **error);


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

  stream_class->add_remote_candidate = fs_rtp_stream_add_remote_candidate;
  stream_class->preload_recv_codec = fs_rtp_stream_preload_recv_codec;
  stream_class->set_remote_codecs = fs_rtp_stream_set_remote_codecs;

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

/**
 * fs_rtp_stream_add_remote_candidate:
 * @stream: an #FsStream
 * @candidate: an #FsCandidate struct representing a remote candidate
 *
 * This function adds the given candidate into the remote candiate list of the
 * stream. It will be used for establishing a connection with the peer. A copy
 * will be made so the user must free the passed candidate using
 * fs_candidate_destroy() when done.
 */
static void
fs_rtp_stream_add_remote_candidate (FsStream *stream, FsCandidate *candidate)
{
}

/**
 * fs_rtp_stream_preload_recv_codec:
 * @stream: an #FsStream
 * @codec: The #FsCodec to be preloaded
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will preload the codec corresponding to the given codec.
 * This codec must correspond exactly to one of the native-codecs returned by
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
 * the given remote codecs couldn't be negotiated with the list of native
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

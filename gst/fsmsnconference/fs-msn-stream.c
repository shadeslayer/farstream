/*
 * Farsight2 - Farsight MSN Stream
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-stream.c - A Farsight MSN Stream gobject
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
 * SECTION:fs-msn-stream
 * @short_description: A MSN stream in a #FsMsnSession in a #FsMsnConference
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-msn-stream.h"
#include "fs-msn-connection.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
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
  PROP_DIRECTION,
  PROP_PARTICIPANT,
  PROP_SESSION,
  PROP_CONFERENCE
};



struct _FsMsnStreamPrivate
{
  FsMsnSession *session;
  FsMsnParticipant *participant;
  FsStreamDirection direction;
  FsMsnConference *conference;
  GstElement *media_fd_src;
  GstElement *media_fd_sink;
  GstElement *session_valve;
  GstPad *sink_pad,*src_pad;
  FsMsnConnection *connection;

  GError *construction_error;

  gboolean disposed;
};


G_DEFINE_TYPE(FsMsnStream, fs_msn_stream, FS_TYPE_STREAM);

#define FS_MSN_STREAM_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_MSN_STREAM, FsMsnStreamPrivate))

static void fs_msn_stream_dispose (GObject *object);
static void fs_msn_stream_finalize (GObject *object);

static void fs_msn_stream_get_property (GObject *object,
                                        guint prop_id,
                                        GValue *value,
                                        GParamSpec *pspec);
static void fs_msn_stream_set_property (GObject *object,
                                        guint prop_id,
                                        const GValue *value,
                                        GParamSpec *pspec);

static void fs_msn_stream_constructed (GObject *object);

static gboolean fs_msn_stream_set_remote_candidates (FsStream *stream,
    GList *candidates,
    GError **error);

static void _local_candidates_prepared (FsMsnConnection *connection,
    gpointer user_data);

static void _new_local_candidate (
    FsMsnConnection *connection,
    FsCandidate *candidate,
    gpointer user_data);




static GObjectClass *parent_class = NULL;

static void
fs_msn_stream_class_init (FsMsnStreamClass *klass)
{
  GObjectClass *gobject_class;
  FsStreamClass *stream_class = FS_STREAM_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_msn_stream_set_property;
  gobject_class->get_property = fs_msn_stream_get_property;
  gobject_class->constructed = fs_msn_stream_constructed;
  gobject_class->dispose = fs_msn_stream_dispose;
  gobject_class->finalize = fs_msn_stream_finalize;


  stream_class->set_remote_candidates = fs_msn_stream_set_remote_candidates;


  g_type_class_add_private (klass, sizeof (FsMsnStreamPrivate));

  g_object_class_override_property (gobject_class,
      PROP_DIRECTION,
      "direction");
  g_object_class_override_property (gobject_class,
      PROP_PARTICIPANT,
      "participant");
  g_object_class_override_property (gobject_class,
      PROP_SESSION,
      "session");

  g_object_class_install_property (gobject_class,
      PROP_CONFERENCE,
      g_param_spec_object ("conference",
          "The Conference this stream refers to",
          "This is a conveniance pointer for the Conference",
          FS_TYPE_MSN_CONFERENCE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static void
fs_msn_stream_init (FsMsnStream *self)
{
  /* member init */
  self->priv = FS_MSN_STREAM_GET_PRIVATE (self);

  self->priv->disposed = FALSE;
  self->priv->session = NULL;
  self->priv->participant = NULL;

  self->priv->direction = FS_DIRECTION_NONE;

}

static void
fs_msn_stream_dispose (GObject *object)
{
  FsMsnStream *self = FS_MSN_STREAM (object);

  if (self->priv->disposed)
  {
    /* If dispose did already run, return. */
    return;
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

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_msn_stream_finalize (GObject *object)
{
  parent_class->finalize (object);
}


static void
fs_msn_stream_get_property (GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
  FsMsnStream *self = FS_MSN_STREAM (object);

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_msn_stream_set_property (GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
  FsMsnStream *self = FS_MSN_STREAM (object);

  switch (prop_id)
  {
    case PROP_SESSION:
      self->priv->session = FS_MSN_SESSION (g_value_dup_object (value));
      break;
    case PROP_PARTICIPANT:
      self->priv->participant = FS_MSN_PARTICIPANT (g_value_dup_object (value));
      break;
    case PROP_DIRECTION:
      self->priv->direction = g_value_get_flags (value);
      break;
    case PROP_CONFERENCE:
      self->priv->conference = FS_MSN_CONFERENCE (g_value_dup_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_msn_stream_constructed (GObject *object)
{

  FsMsnStream *self = FS_MSN_STREAM_CAST (object);

  if (self->priv->direction == FS_DIRECTION_SEND)
  {
    GstElement *mimenc;
    GstElement *ffmpegcolorspace;
    GstElement *queue;

    ffmpegcolorspace = gst_element_factory_make ("ffmpegcolorspace", NULL);

    if (!ffmpegcolorspace)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the ffmpegcolorspace element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), ffmpegcolorspace))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the ffmpegcolorspace element to the FsMsnConference");
      gst_object_unref (ffmpegcolorspace);
      return;
    }

    if (!gst_element_sync_state_with_parent (ffmpegcolorspace))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not sync state for ffmpegcolorspace element");
      return;
    }


    queue = gst_element_factory_make ("queue", NULL);

    if (!queue)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the queue element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), queue))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the queue element to the FsMsnConference");
      gst_object_unref (queue);
      return;
    }

    if (!gst_element_sync_state_with_parent (queue))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not sync state for queue element");
      return;
    }

    mimenc = gst_element_factory_make ("mimenc", "send_mim_enc");

    if (!mimenc)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the mimenc element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), mimenc))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the mimenc element to the FsMsnConference");
      gst_object_unref (mimenc);
      return;
    }

    if (!gst_element_sync_state_with_parent (mimenc))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not set state for mimenc element");
      return;
    }

    self->priv->media_fd_sink = gst_element_factory_make ("fdsink",
        "send_fd_sink");

    if (!self->priv->media_fd_sink)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the media_fd_sink element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference),
            self->priv->media_fd_sink))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the media_fd_sink element to the FsMsnConference");
      gst_object_unref (self->priv->media_fd_sink);
      return;
    }

    if (!gst_element_set_locked_state(self->priv->media_fd_sink,TRUE))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not lock state of media_fd_sink element");
      return;
    }


    gst_element_link_many(queue, ffmpegcolorspace, mimenc,
        self->priv->media_fd_sink, NULL);

    self->priv->sink_pad = gst_element_get_static_pad (ffmpegcolorspace, "sink");

  }
  else if (self->priv->direction == FS_DIRECTION_RECV)
  {

    GstElement *mimdec;
    GstElement *queue;
    GstElement *ffmpegcolorspace;
    FsCodec *mimic_codec = fs_codec_new (FS_CODEC_ID_ANY, "mimic",
        FS_MEDIA_TYPE_VIDEO, 0);

    self->priv->media_fd_src = gst_element_factory_make ("fdsrc",
        "recv_fd_src");

    if (!self->priv->media_fd_src)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the media_fd_src element");
      return;
    }

    g_object_set (G_OBJECT(self->priv->media_fd_src), "blocksize", 512, NULL);
    if (!gst_bin_add (GST_BIN (self->priv->conference),
            self->priv->media_fd_src))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the media_fd_src element to the FsMsnConference");
      gst_object_unref (self->priv->media_fd_src);
      return;
    }

    if (!gst_element_set_locked_state(self->priv->media_fd_src,TRUE))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not lock state of media_fd_src element");
      return;
    }


    mimdec = gst_element_factory_make ("mimdec", "recv_mim_dec");

    if (!mimdec)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the mimdec element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), mimdec))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the mimdec element to the FsMsnConference");
      gst_object_unref (mimdec);
      return;
    }
    if (!gst_element_sync_state_with_parent  (mimdec))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not sync state with parent for mimdec element");
      return;
    }

    queue = gst_element_factory_make ("queue", NULL);

    if (!queue)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the queue element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), queue))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the queue element to the FsMsnConference");
      gst_object_unref (queue);
      return;
    }

    if (!gst_element_sync_state_with_parent  (queue))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not sync state with parent for queue element");
      return;
    }

    ffmpegcolorspace = gst_element_factory_make ("ffmpegcolorspace", NULL);

    if (!ffmpegcolorspace)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the ffmpegcolorspace element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), ffmpegcolorspace))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the ffmpegcolorspace element to the FsMsnConference");
      gst_object_unref (ffmpegcolorspace);
      return;
    }

    if (!gst_element_sync_state_with_parent  (ffmpegcolorspace))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not sync state with parent for ffmpegcolorspace element");
      return;
    }

    GstPad *tmp_src_pad = gst_element_get_static_pad (ffmpegcolorspace, "src");
    self->priv->src_pad = gst_ghost_pad_new ("src", tmp_src_pad);
    gst_object_unref (tmp_src_pad);

    gst_pad_set_active(self->priv->src_pad, TRUE);

    if (!gst_element_add_pad (GST_ELEMENT (self->priv->conference),
            self->priv->src_pad))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION, "Could not add sink pad to conference");
      gst_object_unref (self->priv->src_pad);
      self->priv->src_pad = NULL;
      return;
    }

    gst_element_link_many(self->priv->media_fd_src, mimdec, queue,
        ffmpegcolorspace, NULL);

    fs_stream_emit_src_pad_added (FS_STREAM (self), self->priv->src_pad,
        mimic_codec);
    fs_codec_destroy (mimic_codec);
  }
  else
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_INVALID_ARGUMENTS,
        "Direction must be sending OR receiving");
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
}


static void
_local_candidates_prepared (FsMsnConnection *connection,
    gpointer user_data)
{
  FsMsnStream *self = FS_MSN_STREAM (user_data);

  gst_element_post_message (GST_ELEMENT (self->priv->conference),
      gst_message_new_element (GST_OBJECT (self->priv->conference),
          gst_structure_new ("farsight-local-candidates-prepared",
              "stream", FS_TYPE_STREAM, self,
              NULL)));

}

static void
_new_local_candidate (
    FsMsnConnection *connection,
    FsCandidate *candidate,
    gpointer user_data)
{
  FsMsnStream *self = FS_MSN_STREAM (user_data);

  gst_element_post_message (GST_ELEMENT (self->priv->conference),
      gst_message_new_element (GST_OBJECT (self->priv->conference),
          gst_structure_new ("farsight-new-local-candidate",
              "stream", FS_TYPE_STREAM, self,
              "candidate", FS_TYPE_CANDIDATE, candidate,
              NULL)));
}


static void
_connected (
    FsMsnConnection *connection,
    guint fd,
    gpointer user_data)
{
  FsMsnStream *self = FS_MSN_STREAM (user_data);

  g_debug ("******** CONNECTED %d**********", fd);
  if (self->priv->media_fd_src) {
    g_object_set (G_OBJECT (self->priv->media_fd_src), "fd", fd, NULL);
    gst_element_set_locked_state(self->priv->media_fd_src, FALSE);
    gst_element_sync_state_with_parent (self->priv->media_fd_src);
  }
  else if (self->priv->media_fd_sink)
  {
    g_object_set (G_OBJECT (self->priv->media_fd_sink), "fd", fd, NULL);
    gst_element_set_locked_state(self->priv->media_fd_sink,FALSE);
    gst_element_sync_state_with_parent (self->priv->media_fd_sink);
    g_object_set (G_OBJECT (self->priv->session_valve), "drop", FALSE, NULL);
  }
  else
  {
    g_debug ("no media fd src/sink...");
  }
}

/**
 * fs_msn_stream_set_remote_candidate:
 */
static gboolean
fs_msn_stream_set_remote_candidates (FsStream *stream, GList *candidates,
                                     GError **error)
{
  FsMsnStream *self = FS_MSN_STREAM (stream);

  return fs_msn_connection_set_remote_candidates (self->priv->connection,
      candidates, error);
}


/**
 * fs_msn_stream_new:
 * @session: The #FsMsnSession this stream is a child of
 * @participant: The #FsMsnParticipant this stream is for
 * @direction: the initial #FsDirection for this stream
 *
 *
 * This function create a new stream
 *
 * Returns: the newly created string or NULL on error
 */

FsMsnStream *
fs_msn_stream_new (FsMsnSession *session,
    FsMsnParticipant *participant,
    FsStreamDirection direction,
    FsMsnConference *conference,
    GstElement *session_valve,
    guint session_id,
    guint initial_port,
    GError **error)
{
  FsMsnStream *self = g_object_new (FS_TYPE_MSN_STREAM,
      "session", session,
      "participant", participant,
      "direction", direction,
      "conference",conference,
      NULL);

  if (!self)
  {
    *error = g_error_new (FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create object");
  }
  else if (self->priv->construction_error)
  {
    g_propagate_error (error, self->priv->construction_error);
    g_object_unref (self);
    return NULL;
  }

  if (self)
  {

    if (self->priv->sink_pad)
    {
      gst_pad_link (gst_element_get_static_pad (session_valve, "src"),
          self->priv->sink_pad);
    }
    self->priv->session_valve = session_valve;

    self->priv->connection = fs_msn_connection_new (session_id, initial_port);

    g_signal_connect (self->priv->connection,
        "new-local-candidate",
        G_CALLBACK (_new_local_candidate), self);
    g_signal_connect (self->priv->connection,
        "local-candidates-prepared",
        G_CALLBACK (_local_candidates_prepared), self);

    g_signal_connect (self->priv->connection,
        "connected",
        G_CALLBACK (_connected), self);

    fs_msn_connection_gather_local_candidates (self->priv->connection);
  }

  return self;
}


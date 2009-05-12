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

#define GST_CAT_DEFAULT fsmsnconference_debug

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
  FsMsnConference *conference;
  FsMsnSession *session;
  FsMsnParticipant *participant;
  FsStreamDirection orig_direction;
  FsStreamDirection direction;
  GstElement *codecbin;
  GstElement *recv_valve;
  GstPad *src_pad;
  FsMsnConnection *connection;

  GError *construction_error;

  GMutex *mutex; /* protects the conference */
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

  self->priv->session = NULL;
  self->priv->participant = NULL;

  self->priv->direction = FS_DIRECTION_NONE;
  self->priv->orig_direction = FS_DIRECTION_NONE;

  self->priv->mutex = g_mutex_new ();
}


static FsMsnConference *
fs_msn_stream_get_conference (FsMsnStream *self, GError **error)
{
  FsMsnConference *conference;

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
fs_msn_stream_dispose (GObject *object)
{
  FsMsnStream *self = FS_MSN_STREAM (object);
  FsMsnConference *conference = fs_msn_stream_get_conference (self, NULL);

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

  if (self->priv->codecbin)
  {
    gst_element_set_locked_state (self->priv->codecbin, TRUE);
    gst_element_set_state (self->priv->codecbin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (conference), self->priv->codecbin);
    self->priv->codecbin = NULL;
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

  if (self->priv->connection)
  {
    g_object_unref (self->priv->connection);
    self->priv->connection = NULL;
  }

  gst_object_unref (conference);
  gst_object_unref (conference);

  parent_class->dispose (object);
}

static void
fs_msn_stream_finalize (GObject *object)
{
  FsMsnStream *self = FS_MSN_STREAM (object);

  g_mutex_free (self->priv->mutex);

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
      if (self->priv->orig_direction == FS_DIRECTION_NONE)
      {
        self->priv->orig_direction = g_value_get_flags (value);
        self->priv->direction = g_value_get_flags (value);
        break;
      }

      if (g_value_get_flags (value) != self->priv->direction)
      {
        self->priv->direction =
          g_value_get_flags (value) & self->priv->orig_direction;

        if (self->priv->direction == FS_DIRECTION_NONE)
        {
          if (self->priv->recv_valve)
            g_object_set (self->priv->recv_valve, "drop", TRUE, NULL);
          g_object_set (self->priv->session->valve, "drop", TRUE, NULL);
        }
        else if (self->priv->direction == FS_DIRECTION_SEND)
        {
          if (self->priv->codecbin)
            g_object_set (self->priv->session->valve, "drop", FALSE, NULL);
        }
        else if (self->priv->direction == FS_DIRECTION_RECV)
        {
         if (self->priv->recv_valve)
            g_object_set (self->priv->recv_valve, "drop", FALSE, NULL);
        }
      }
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
    GstElementFactory *fact = NULL;

    fact = gst_element_factory_find ("mimenc");
    if (fact)
    {
      gst_object_unref (fact);
    }
    else
    {
      g_set_error (&self->priv->construction_error,
          FS_ERROR, FS_ERROR_CONSTRUCTION,
          "mimenc missing");
      return;
    }
  }
  else if (self->priv->direction == FS_DIRECTION_RECV)
  {
   GstElementFactory *fact = NULL;

    fact = gst_element_factory_find ("mimdec");
    if (fact)
    {
      gst_object_unref (fact);
    }
    else
    {
      g_set_error (&self->priv->construction_error,
          FS_ERROR, FS_ERROR_CONSTRUCTION,
          "mimdec missing");
      return;
    }
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
  GError *error = NULL;
  GstPad *pad;
  GstElement *fdelem;
  int checkfd;

  GST_DEBUG ("******** CONNECTED %d**********", fd);

  if (self->priv->orig_direction == FS_DIRECTION_RECV)
    self->priv->codecbin = gst_parse_bin_from_description (
        "fdsrc name=fdsrc ! mimdec ! valve name=recv_valve", TRUE, &error);
  else
    self->priv->codecbin = gst_parse_bin_from_description (
        "ffmpegcolorspace ! videoscale ! mimenc ! fdsink name=fdsink",
        TRUE, &error);

  if (!self->priv->codecbin)
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not build codecbin", error->message);
    g_clear_error (&error);
    return;
  }


  if (self->priv->orig_direction == FS_DIRECTION_RECV)
    fdelem = gst_bin_get_by_name (GST_BIN (self->priv->codecbin), "fdsrc");
  else
    fdelem = gst_bin_get_by_name (GST_BIN (self->priv->codecbin), "fdsink");

  if (!fdelem)
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not get fd element",
        "Could not get fd element");
      gst_object_unref (self->priv->codecbin);
      self->priv->codecbin = NULL;
      return;
  }

  g_object_set (fdelem, "fd", fd, NULL);
  g_object_get (fdelem, "fd", &checkfd, NULL);
  gst_object_unref (fdelem);

  if (fd != checkfd)
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_INTERNAL,
        "Could not set file descriptor", "Could not set fd");
    gst_object_unref (self->priv->codecbin);
    self->priv->codecbin = NULL;
    return;
  }

  if (self->priv->orig_direction == FS_DIRECTION_RECV)
    pad = gst_element_get_static_pad (self->priv->codecbin, "src");
  else
    pad = gst_element_get_static_pad (self->priv->codecbin, "sink");

  if (!pad)
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not get codecbin pad",
        "Could not get codecbin pad");
    gst_object_unref (self->priv->codecbin);
    self->priv->codecbin = NULL;
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->codecbin))
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not add codecbin to the conference",
        "Could not add codecbin to the conference");
    gst_object_unref (pad);
    gst_object_unref (self->priv->codecbin);
    self->priv->codecbin = NULL;
    return;
  }

  if (self->priv->orig_direction == FS_DIRECTION_RECV)
  {
    FsCodec *mimic_codec;

    self->priv->src_pad = gst_ghost_pad_new ("src_1_1_1", pad);
    gst_object_unref (pad);

    if (!gst_element_add_pad (GST_ELEMENT (self->priv->conference),
            self->priv->src_pad))
    {
         fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
             "Could not add src_1_1_1 pad",
             "Could not add src_1_1_1 pad");
         gst_object_unref (self->priv->src_pad);
         self->priv->src_pad = NULL;
      return;
    }

    self->priv->recv_valve = fdelem = gst_bin_get_by_name (
        GST_BIN (self->priv->codecbin), "recv_valve");

    if (!self->priv->recv_valve)
    {
       fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
           "Could not get recv_valve",
           "Could not get recv_valve");
       return;
    }

    g_object_set (self->priv->recv_valve,
        "drop", !(self->priv->direction & FS_DIRECTION_RECV), NULL);

    mimic_codec = fs_codec_new (0, "mimic",
        FS_MEDIA_TYPE_VIDEO, 0);
    fs_stream_emit_src_pad_added (FS_STREAM (self), self->priv->src_pad,
        mimic_codec);
    fs_codec_destroy (mimic_codec);
  }
  else
  {
    GstPad *valvepad = gst_element_get_static_pad (self->priv->session->valve,
        "src");

    if (!valvepad)
    {
      gst_object_unref (pad);
      fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
          "Could not get valve sink pad",
          "Could not get valve sink pad");
      return;
    }

    if (GST_PAD_LINK_FAILED (gst_pad_link (valvepad, pad)))
    {
      gst_object_unref (valvepad);
      gst_object_unref (pad);
      fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
          "Could not link valve to codec bin",
          "Could not link valve to codec bin");
      return;
    }
    gst_object_unref (valvepad);
    gst_object_unref (pad);
  }

  if (!gst_element_sync_state_with_parent (self->priv->codecbin))
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not start codec bin",
        "Could not start codec bin");
    return;
  }


  if (self->priv->direction == FS_DIRECTION_SEND)
    g_object_set (self->priv->recv_valve,
        "drop", !(self->priv->direction & FS_DIRECTION_SEND), NULL);
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
    guint session_id,
    guint initial_port,
    GError **error)
{
  FsMsnStream *self = g_object_new (FS_TYPE_MSN_STREAM,
      "session", session,
      "participant", participant,
      "direction", direction,
      "conference", conference,
      NULL);

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

  if (!fs_msn_connection_gather_local_candidates (self->priv->connection,
          error))
  {
    g_object_unref (self);
    return NULL;
  }

  return self;
}


/*
 * Farsight2 - Farsight MSN Stream
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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
 * The #FsMsnStream::direction property can be used to pause the stream, but not
 * to change the direction between sending and receiving since this protocol
 * is unidirectional.
 *
 * The "foundation" field of the local #FsCandidate contains the "recipient-id"
 * that must be transmitted to the peer.
 *
 * The session id can either be retrieved as a property, but it is also
 * put into every #FsCandidate in the "username" field.
 *
 * If the peer started the webcam session, it picks the session-id, it can then
 * be set either in the transmitter parameters field of fs_session_new_stream()
 * or by putting it in the "username" field of the remote #FsCandidate.
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
  PROP_CONFERENCE,
  PROP_SESSION_ID,
  PROP_INITIAL_PORT
};



struct _FsMsnStreamPrivate
{
  FsMsnConference *conference;
  FsMsnSession *session;
  FsMsnParticipant *participant;
  FsStreamDirection direction;
  GstElement *codecbin;
  GstElement *recv_valve;
  GstPad *src_pad;
  FsMsnConnection *connection;

  GError *construction_error;

  guint session_id;
  guint initial_port;

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

static void
_connected (
    FsMsnConnection *connection,
    guint fd,
    gpointer user_data);

static void
_connection_failed (FsMsnConnection *connection, FsMsnStream *self);


static void
fs_msn_stream_class_init (FsMsnStreamClass *klass)
{
  GObjectClass *gobject_class;
  FsStreamClass *stream_class = FS_STREAM_CLASS (klass);

  gobject_class = (GObjectClass *) klass;

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
  g_object_class_install_property (gobject_class,
      PROP_SESSION_ID,
      g_param_spec_uint ("session-id",
          "The session-id of the session",
          "This is the session-id of the MSN session",
          0, 9999, 0,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INITIAL_PORT,
      g_param_spec_uint ("initial-port",
          "The initial port to listen on",
          "The initial port to try to listen on for incoming connection."
          " If already used, port+1 is tried until one succeeds",
          1025, 65535, 1025,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
fs_msn_stream_init (FsMsnStream *self)
{
  /* member init */
  self->priv = FS_MSN_STREAM_GET_PRIVATE (self);

  self->priv->session = NULL;
  self->priv->participant = NULL;

  self->priv->direction = FS_DIRECTION_NONE;

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
    gst_object_unref (self->priv->recv_valve);
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

  G_OBJECT_CLASS (fs_msn_stream_parent_class)->dispose (object);
}

static void
fs_msn_stream_finalize (GObject *object)
{
  FsMsnStream *self = FS_MSN_STREAM (object);

  g_mutex_free (self->priv->mutex);

  G_OBJECT_CLASS (fs_msn_stream_parent_class)->finalize (object);
}


static void
fs_msn_stream_get_property (GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
  FsMsnStream *self = FS_MSN_STREAM (object);
  FsMsnConference *conference = fs_msn_stream_get_conference (self, NULL);

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
    case PROP_SESSION_ID:
      g_object_get_property (G_OBJECT (self->priv->connection), "session-id",
          value);
      break;
    case PROP_INITIAL_PORT:
      g_value_set_uint (value, self->priv->initial_port);
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
fs_msn_stream_set_property (GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
  FsMsnStream *self = FS_MSN_STREAM (object);
  FsMsnConference *conference = fs_msn_stream_get_conference (self, NULL);

  if (!conference &&
      !(pspec->flags & (G_PARAM_CONSTRUCT_ONLY | G_PARAM_CONSTRUCT)))
    return;

  if (conference)
    GST_OBJECT_LOCK (conference);

  switch (prop_id)
  {
    case PROP_SESSION:
      self->priv->session = FS_MSN_SESSION (g_value_dup_object (value));
      break;
    case PROP_PARTICIPANT:
      self->priv->participant = FS_MSN_PARTICIPANT (g_value_dup_object (value));
      break;
    case PROP_DIRECTION:
      if (g_value_get_flags (value) != self->priv->direction)
      {
        GstElement *recv_valve = NULL;
        GstElement *session_valve = NULL;

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

        self->priv->direction =
          g_value_get_flags (value) & conference->max_direction;

        if (self->priv->direction == FS_DIRECTION_NONE)
        {
          GST_OBJECT_UNLOCK (conference);
          if (recv_valve)
            g_object_set (recv_valve, "drop", TRUE, NULL);
          g_object_set (session_valve, "drop", TRUE, NULL);
          GST_OBJECT_LOCK (conference);
        }
        else if (self->priv->direction == FS_DIRECTION_SEND)
        {
          if (self->priv->codecbin)
          {
            GST_OBJECT_UNLOCK (conference);
            g_object_set (session_valve, "drop", FALSE, NULL);
            GST_OBJECT_LOCK (conference);
          }
        }
        else if (self->priv->direction == FS_DIRECTION_RECV)
        {
          GST_OBJECT_UNLOCK (conference);
          if (recv_valve)
            g_object_set (recv_valve, "drop", FALSE, NULL);
          GST_OBJECT_LOCK (conference);
        }

        if (session_valve)
          gst_object_unref (session_valve);
        if (recv_valve)
          gst_object_unref (recv_valve);
      }
      self->priv->direction = g_value_get_flags (value);
      break;
    case PROP_CONFERENCE:
      self->priv->conference = FS_MSN_CONFERENCE (g_value_dup_object (value));
      break;
    case PROP_SESSION_ID:
      self->priv->session_id = g_value_get_uint (value);
      if (self->priv->session_id == 0)
        self->priv->session_id = g_random_int_range (9000, 9999);
      break;
    case PROP_INITIAL_PORT:
      self->priv->initial_port = g_value_get_uint (value);
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
fs_msn_stream_constructed (GObject *object)
{
  FsMsnStream *self = FS_MSN_STREAM_CAST (object);
  gboolean producer;

  if (self->priv->conference->max_direction == FS_DIRECTION_RECV)
    producer = FALSE;
  else if (self->priv->conference->max_direction == FS_DIRECTION_SEND)
    producer = TRUE;
  else
    g_assert_not_reached ();

  self->priv->connection = fs_msn_connection_new (self->priv->session_id,
      producer, self->priv->initial_port);

  g_signal_connect (self->priv->connection,
      "new-local-candidate",
      G_CALLBACK (_new_local_candidate), self);
  g_signal_connect (self->priv->connection,
      "local-candidates-prepared",
      G_CALLBACK (_local_candidates_prepared), self);
  g_signal_connect (self->priv->connection,
      "connected",
      G_CALLBACK (_connected), self);
  g_signal_connect (self->priv->connection,
      "connection-failed",
      G_CALLBACK (_connection_failed), self);

  if (!fs_msn_connection_gather_local_candidates (self->priv->connection,
          &self->priv->construction_error))
    return;

  if (G_OBJECT_CLASS (fs_msn_stream_parent_class)->constructed)
    G_OBJECT_CLASS (fs_msn_stream_parent_class)->constructed (object);
}


static void
_local_candidates_prepared (FsMsnConnection *connection,
    gpointer user_data)
{
  FsMsnStream *self = FS_MSN_STREAM (user_data);
  FsMsnConference *conference = fs_msn_stream_get_conference (self, NULL);

  if (!conference)
    return;

  gst_element_post_message (GST_ELEMENT (conference),
      gst_message_new_element (GST_OBJECT (conference),
          gst_structure_new ("farsight-local-candidates-prepared",
              "stream", FS_TYPE_STREAM, self,
              NULL)));

  gst_object_unref (conference);
}

static void
_new_local_candidate (
    FsMsnConnection *connection,
    FsCandidate *candidate,
    gpointer user_data)
{
  FsMsnStream *self = FS_MSN_STREAM (user_data);
  FsMsnConference *conference = fs_msn_stream_get_conference (self, NULL);

  if (!conference)
    return;

  gst_element_post_message (GST_ELEMENT (conference),
      gst_message_new_element (GST_OBJECT (conference),
          gst_structure_new ("farsight-new-local-candidate",
              "stream", FS_TYPE_STREAM, self,
              "candidate", FS_TYPE_CANDIDATE, candidate,
              NULL)));

  gst_object_unref (conference);
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
  FsMsnConference *conference = fs_msn_stream_get_conference (self, NULL);
  GstElement *codecbin = NULL;
  GstElement *recv_valve = NULL;
  GstElement *send_valve = NULL;
  gboolean drop;

  if (!conference)
    goto error;

  GST_DEBUG ("******** CONNECTED %d**********", fd);

  gst_element_post_message (GST_ELEMENT (conference),
      gst_message_new_element (GST_OBJECT (conference),
          gst_structure_new ("farsight-component-state-changed",
              "stream", FS_TYPE_STREAM, self,
              "component", G_TYPE_UINT, 1,
              "state", FS_TYPE_STREAM_STATE, FS_STREAM_STATE_READY,
              NULL)));

  if (self->priv->conference->max_direction == FS_DIRECTION_RECV)
    codecbin = gst_parse_bin_from_description (
        "fdsrc name=fdsrc do-timestamp=true ! mimdec ! valve name=recv_valve", TRUE, &error);
  else
    codecbin = gst_parse_bin_from_description (
        "ffmpegcolorspace ! videoscale ! mimenc name=enc ! fdsink name=fdsink",
        TRUE, &error);

  if (!codecbin)
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not build codecbin", error->message);
    g_clear_error (&error);
    goto error;
  }

  /* So we don't require an unlreased gst-plugins-bad mimenc */
  if (self->priv->conference->max_direction == FS_DIRECTION_SEND)
  {
    GstElement *mimenc = gst_bin_get_by_name (GST_BIN (codecbin), "enc");
    if (g_object_class_find_property (
            G_OBJECT_GET_CLASS (mimenc), "paused-mode"))
      g_object_set (mimenc, "paused-mode", TRUE, NULL);
    gst_object_unref (mimenc);
  }

  if (self->priv->conference->max_direction == FS_DIRECTION_RECV)
    fdelem = gst_bin_get_by_name (GST_BIN (codecbin), "fdsrc");
  else
    fdelem = gst_bin_get_by_name (GST_BIN (codecbin), "fdsink");

  if (!fdelem)
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not get fd element",
        "Could not get fd element");
    goto error;
  }

  g_object_set (fdelem, "fd", fd, NULL);
  g_object_get (fdelem, "fd", &checkfd, NULL);
  gst_object_unref (fdelem);

  if (fd != checkfd)
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_INTERNAL,
        "Could not set file descriptor", "Could not set fd");
    goto error;
  }

  if (self->priv->conference->max_direction == FS_DIRECTION_RECV)
    pad = gst_element_get_static_pad (codecbin, "src");
  else
    pad = gst_element_get_static_pad (codecbin, "sink");

  if (!pad)
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not get codecbin pad",
        "Could not get codecbin pad");
    goto error;
  }

  if (!gst_bin_add (GST_BIN (conference), codecbin))
  {
    gst_object_unref (pad);
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not add codecbin to the conference",
        "Could not add codecbin to the conference");
    goto error;
  }

  GST_OBJECT_LOCK (conference);
  self->priv->codecbin = gst_object_ref (codecbin);
  GST_OBJECT_UNLOCK (conference);

  if (self->priv->conference->max_direction == FS_DIRECTION_RECV)
  {
    FsCodec *mimic_codec;
    GstPad *src_pad;

    src_pad = gst_ghost_pad_new ("src_1_1_1", pad);
    gst_object_unref (pad);

    GST_OBJECT_LOCK (conference);
    self->priv->src_pad =  gst_object_ref (src_pad);
    GST_OBJECT_UNLOCK (conference);

    gst_pad_set_active (src_pad, TRUE);
    if (!gst_element_add_pad (GST_ELEMENT (conference), src_pad))
    {
      fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
          "Could not add src_1_1_1 pad",
          "Could not add src_1_1_1 pad");
      gst_object_unref (src_pad);
      goto error;
    }

    recv_valve = gst_bin_get_by_name (GST_BIN (codecbin), "recv_valve");

    if (!recv_valve)
    {
       fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
           "Could not get recv_valve",
           "Could not get recv_valve");
       gst_object_unref (src_pad);
       goto error;
    }

    GST_OBJECT_LOCK (conference);
    self->priv->recv_valve = gst_object_ref (recv_valve);
    drop = !(self->priv->direction & FS_DIRECTION_RECV);
    GST_OBJECT_UNLOCK (conference);

    g_object_set (recv_valve, "drop", drop, NULL);


    mimic_codec = fs_codec_new (0, "mimic",
        FS_MEDIA_TYPE_VIDEO, 0);
    fs_stream_emit_src_pad_added (FS_STREAM (self), src_pad, mimic_codec);
    fs_codec_destroy (mimic_codec);
    gst_object_unref (src_pad);

  }
  else
  {
    GstPad *valvepad;

    GST_OBJECT_LOCK (conference);
    if (self->priv->session->valve)
      send_valve = gst_object_ref (self->priv->session->valve);
    GST_OBJECT_UNLOCK (conference);

    if (!send_valve)
    {
      fs_stream_emit_error (FS_STREAM (self), FS_ERROR_DISPOSED,
          "Session was disposed",
          "Session was disposed");
      goto error;
    }

    valvepad = gst_element_get_static_pad (send_valve, "src");

    if (!valvepad)
    {
      gst_object_unref (pad);
      fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
          "Could not get valve sink pad",
          "Could not get valve sink pad");
      goto error;
    }

    if (GST_PAD_LINK_FAILED (gst_pad_link (valvepad, pad)))
    {
      gst_object_unref (valvepad);
      gst_object_unref (pad);
      fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
          "Could not link valve to codec bin",
          "Could not link valve to codec bin");
      goto error;
    }
    gst_object_unref (valvepad);
    gst_object_unref (pad);
  }

  if (!gst_element_sync_state_with_parent (codecbin))
  {
    fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONSTRUCTION,
        "Could not start codec bin",
        "Could not start codec bin");
    goto error;
  }

  if (self->priv->conference->max_direction == FS_DIRECTION_SEND)
  {
    GST_OBJECT_LOCK (conference);
    drop = !(self->priv->direction & FS_DIRECTION_SEND);
    GST_OBJECT_UNLOCK (conference);
    g_object_set (send_valve, "drop", drop, NULL);
  }

 error:

  if (send_valve)
    gst_object_unref (send_valve);
  if (recv_valve)
    gst_object_unref (recv_valve);
  if (codecbin)
    gst_object_unref (codecbin);
  if (conference)
    gst_object_unref (conference);
}

static void
_connection_failed (FsMsnConnection *connection, FsMsnStream *self)
{
  FsMsnConference *conference = fs_msn_stream_get_conference (self, NULL);

  if (!conference)
    return;

  gst_element_post_message (GST_ELEMENT (conference),
      gst_message_new_element (GST_OBJECT (conference),
          gst_structure_new ("farsight-component-state-changed",
              "stream", FS_TYPE_STREAM, self,
              "component", G_TYPE_UINT, 1,
              "state", FS_TYPE_STREAM_STATE, FS_STREAM_STATE_FAILED,
              NULL)));

  fs_stream_emit_error (FS_STREAM (self), FS_ERROR_CONNECTION_FAILED,
      "Could not establish streaming connection",
      "Could not establish streaming connection");

  gst_object_unref (conference);
}

/**
 * fs_msn_stream_set_remote_candidate:
 */
static gboolean
fs_msn_stream_set_remote_candidates (FsStream *stream, GList *candidates,
                                     GError **error)
{
  FsMsnStream *self = FS_MSN_STREAM (stream);
  FsMsnConference *conference = fs_msn_stream_get_conference (self, error);
  gboolean ret;

  if (!conference)
    return FALSE;

  GST_OBJECT_LOCK (conference);
  ret = fs_msn_connection_set_remote_candidates (self->priv->connection,
      candidates, error);
  GST_OBJECT_UNLOCK (conference);


  if (ret)
    gst_element_post_message (GST_ELEMENT (conference),
        gst_message_new_element (GST_OBJECT (conference),
            gst_structure_new ("farsight-component-state-changed",
                "stream", FS_TYPE_STREAM, self,
                "component", G_TYPE_UINT, 1,
                "state", FS_TYPE_STREAM_STATE, FS_STREAM_STATE_CONNECTING,
                NULL)));

  gst_object_unref (conference);

  return ret;
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
    guint n_parameters,
    GParameter *parameters,
    GError **error)
{
  FsMsnStream *self;
  GParameter *params;

  params = g_new0 (GParameter, n_parameters + 4);

  params[0].name = "session";
  g_value_init (&params[0].value, FS_TYPE_SESSION);
  g_value_set_object (&params[0].value, session);

  params[1].name = "participant";
  g_value_init (&params[1].value, FS_TYPE_PARTICIPANT);
  g_value_set_object (&params[1].value, participant);

  params[2].name = "direction";
  g_value_init (&params[2].value, FS_TYPE_STREAM_DIRECTION);
  g_value_set_flags (&params[2].value, direction);

  params[3].name = "conference";
  g_value_init (&params[3].value, FS_TYPE_MSN_CONFERENCE);
  g_value_set_object (&params[3].value, conference);

  if (n_parameters)
    memcpy (params+4, parameters, n_parameters * sizeof(GParameter));

  self = g_object_newv (FS_TYPE_MSN_STREAM, n_parameters + 4, params);

  g_free (params);

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


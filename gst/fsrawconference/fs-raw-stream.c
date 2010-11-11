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
};



struct _FsRawStreamPrivate
{
  FsRawConference *conference;
  FsRawSession *session;
  FsRawParticipant *participant;
  FsStreamDirection direction;
  GstElement *codecbin;
  GstElement *recv_valve;
  GstPad *src_pad;

  GError *construction_error;

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

static gboolean fs_raw_stream_set_remote_candidates (FsStream *stream,
    GList *candidates,
    GError **error);

static void
fs_raw_stream_class_init (FsRawStreamClass *klass)
{
  GObjectClass *gobject_class;
  FsStreamClass *stream_class = FS_STREAM_CLASS (klass);

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = fs_raw_stream_set_property;
  gobject_class->get_property = fs_raw_stream_get_property;
  gobject_class->dispose = fs_raw_stream_dispose;
  gobject_class->finalize = fs_raw_stream_finalize;

  stream_class->set_remote_candidates = fs_raw_stream_set_remote_candidates;


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

  g_object_class_install_property (gobject_class,
      PROP_CONFERENCE,
      g_param_spec_object ("conference",
          "The Conference this stream refers to",
          "This is a conveniance pointer for the Conference",
          FS_TYPE_RAW_CONFERENCE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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

  gst_object_unref (conference);
  gst_object_unref (conference);

  G_OBJECT_CLASS (fs_raw_stream_parent_class)->dispose (object);
}

static void
fs_raw_stream_finalize (GObject *object)
{
  FsRawStream *self = FS_RAW_STREAM (object);

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
      self->priv->conference = FS_RAW_CONFERENCE (g_value_dup_object (value));
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

/**
 * fs_raw_stream_set_remote_candidate:
 */
static gboolean
fs_raw_stream_set_remote_candidates (FsStream *stream, GList *candidates,
                                     GError **error)
{
  return TRUE;
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
    guint n_parameters,
    GParameter *parameters,
    GError **error)
{
  FsRawStream *self;
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
  g_value_init (&params[3].value, FS_TYPE_RAW_CONFERENCE);
  g_value_set_object (&params[3].value, conference);

  if (n_parameters)
    memcpy (params+4, parameters, n_parameters * sizeof(GParameter));

  self = g_object_newv (FS_TYPE_RAW_STREAM, n_parameters + 4, params);

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

void
fs_raw_stream_set_tos_locked (FsRawStream *self, gint tos)
{
}


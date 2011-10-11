/*
 * Farstream - Farstream MSN Conference Implementation
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-conference.c - MSN implementation for Farstream Conference Gstreamer
 *                       Elements
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
 * SECTION:fs-msn-conference
 * @short_description: Farstream MSN Conference Gstreamer Elements Base class
 *
 * This element implements the unidirection webcam feature found in various
 * version of MSN Messenger (tm) and Windows Live Messenger (tm).
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "fs-msn-conference.h"

#include "fs-msn-session.h"
#include "fs-msn-stream.h"
#include "fs-msn-participant.h"

#include "fs-msn-cam-send-conference.h"
#include "fs-msn-cam-recv-conference.h"

GST_DEBUG_CATEGORY (fsmsnconference_debug);
#define GST_CAT_DEFAULT fsmsnconference_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0
};


static GstStaticPadTemplate fs_msn_conference_sink_template =
  GST_STATIC_PAD_TEMPLATE ("sink_%d",
      GST_PAD_SINK,
      GST_PAD_SOMETIMES,
      GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate fs_msn_conference_src_template =
  GST_STATIC_PAD_TEMPLATE ("src_%d_%d_%d",
      GST_PAD_SRC,
      GST_PAD_SOMETIMES,
      GST_STATIC_CAPS_ANY);

#define FS_MSN_CONFERENCE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), FS_TYPE_MSN_CONFERENCE,  \
      FsMsnConferencePrivate))

struct _FsMsnConferencePrivate
{
  gboolean disposed;
  /* Protected by GST_OBJECT_LOCK */
  FsMsnParticipant *participant;
  FsMsnSession *session;
};

static void fs_msn_conference_do_init (GType type);


GST_BOILERPLATE_FULL (FsMsnConference, fs_msn_conference, FsConference,
    FS_TYPE_CONFERENCE, fs_msn_conference_do_init);

static FsSession *fs_msn_conference_new_session (FsConference *conf,
    FsMediaType media_type,
    GError **error);

static FsParticipant *fs_msn_conference_new_participant (FsConference *conf,
    GError **error);

static void _remove_session (gpointer user_data,
    GObject *where_the_object_was);
static void _remove_participant (gpointer user_data,
    GObject *where_the_object_was);

static void
fs_msn_conference_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (fsmsnconference_debug, "fsmsnconference", 0,
                           "Farstream MSN Conference Element");
}

static void
fs_msn_conference_dispose (GObject * object)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (object);

  if (self->priv->disposed)
    return;

  GST_OBJECT_LOCK (object);
  if (self->priv->session)
    g_object_weak_unref (G_OBJECT (self->priv->session), _remove_session, self);
  self->priv->session = NULL;

  if (self->priv->participant)
    g_object_weak_unref (G_OBJECT (self->priv->participant),
        _remove_participant, self);
  self->priv->participant = NULL;
  GST_OBJECT_UNLOCK (object);

  self->priv->disposed = TRUE;

  g_clear_error (&self->missing_element_error);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
fs_msn_conference_class_init (FsMsnConferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  FsConferenceClass *baseconf_class = FS_CONFERENCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsMsnConferencePrivate));

  baseconf_class->new_session =
    GST_DEBUG_FUNCPTR (fs_msn_conference_new_session);
  baseconf_class->new_participant =
    GST_DEBUG_FUNCPTR (fs_msn_conference_new_participant);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (fs_msn_conference_dispose);
}

static void
fs_msn_conference_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_msn_conference_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_msn_conference_src_template));
}

static void
fs_msn_conference_init (FsMsnConference *conf,
                        FsMsnConferenceClass *bclass)
{
  GST_DEBUG_OBJECT (conf, "fs_msn_conference_init");

  conf->priv = FS_MSN_CONFERENCE_GET_PRIVATE (conf);
}

static void
_remove_session (gpointer user_data,
                 GObject *where_the_object_was)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (user_data);

  GST_OBJECT_LOCK (self);
  if (self->priv->session == (FsMsnSession *) where_the_object_was)
    self->priv->session = NULL;
  GST_OBJECT_UNLOCK (self);
}

static void
_remove_participant (gpointer user_data,
                     GObject *where_the_object_was)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (user_data);

  GST_OBJECT_LOCK (self);
  if (self->priv->participant == (FsMsnParticipant *) where_the_object_was)
    self->priv->participant = NULL;
  GST_OBJECT_UNLOCK (self);
}


static FsSession *
fs_msn_conference_new_session (FsConference *conf,
                               FsMediaType media_type,
                               GError **error)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (conf);
  FsMsnSession *new_session = NULL;

  if (self->missing_element_error)
  {
    if (error)
      *error = g_error_copy (self->missing_element_error);
    return NULL;
  }

  if (media_type != FS_MEDIA_TYPE_VIDEO)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Only video supported for msn webcam");
    return NULL;
  }

  GST_OBJECT_LOCK (self);
  if (self->priv->session)
  {
    GST_OBJECT_UNLOCK (self);
    g_set_error (error, FS_ERROR, FS_ERROR_ALREADY_EXISTS,
        "There already is a session");
    return NULL;
  }

  GST_OBJECT_UNLOCK (self);

  new_session = fs_msn_session_new (media_type, self, error);

  if (new_session)
  {
    GST_OBJECT_LOCK (self);
    self->priv->session = new_session;
    g_object_weak_ref (G_OBJECT (new_session), _remove_session, self);
    GST_OBJECT_UNLOCK (self);
  }

  return FS_SESSION (new_session);
}


static FsParticipant *
fs_msn_conference_new_participant (FsConference *conf,
                                   GError **error)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (conf);
  FsMsnParticipant *new_participant = NULL;

  if (self->missing_element_error)
  {
    if (error)
      *error = g_error_copy (self->missing_element_error);
    return NULL;
  }

  GST_OBJECT_LOCK (self);
  if (self->priv->participant)
  {
    GST_OBJECT_UNLOCK (self);
    g_set_error (error, FS_ERROR, FS_ERROR_ALREADY_EXISTS,
        "There already is a participant");
    return NULL;
  }

  GST_OBJECT_UNLOCK (self);

  new_participant = fs_msn_participant_new ();

  if (new_participant)
  {
    GST_OBJECT_LOCK (self);
    self->priv->participant = new_participant;
    g_object_weak_ref (G_OBJECT (new_participant), _remove_participant, self);
    GST_OBJECT_UNLOCK (self);
  }

  return FS_PARTICIPANT (new_participant);

}


static gboolean plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "fsmsncamsendconference",
      GST_RANK_NONE, FS_TYPE_MSN_CAM_SEND_CONFERENCE) &&
    gst_element_register (plugin, "fsmsncamrecvconference",
        GST_RANK_NONE, FS_TYPE_MSN_CAM_RECV_CONFERENCE);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "fsmsnconference",
  "Farstream MSN Conference plugin",
  plugin_init,
  VERSION,
  "LGPL",
  "Farstream",
  "http://farstream.freedesktop.org/"
)

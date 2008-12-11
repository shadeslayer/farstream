/*
 * Farsight2 - Farsight MSN Conference Implementation
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007 Collabora Ltd.
 *  @author: Richard Spiers <richard.spiers@gmail.com>
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-conference.c - MSN implementation for Farsight Conference Gstreamer
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
 * @short_description: FarsightMSN Conference Gstreamer Elements
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-msn-conference.h"
#include "fs-msn-session.h"
#include "fs-msn-stream.h"
#include "fs-msn-participant.h"

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
  PROP_0,
  PROP_LOCAL_MSNADD,
};


static GstElementDetails fs_msn_conference_details =
{
  "Farsight MSN Conference",
  "Generic/Bin/MSN",
  "A Farsight MSN Conference",
  "Richard Spiers <richard.spiers@gmail.com>"
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
  gchar *local_address;

  FsMsnParticipant *participant;
  FsMsnSession *session;
};

static void fs_msn_conference_do_init (GType type);


GST_BOILERPLATE_FULL (FsMsnConference, fs_msn_conference, FsBaseConference,
    FS_TYPE_BASE_CONFERENCE, fs_msn_conference_do_init);

static void fs_msn_conference_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);

static void fs_msn_conference_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);

static void fs_msn_conference_finalize (GObject *object);

static FsSession *fs_msn_conference_new_session (FsBaseConference *conf,
    FsMediaType media_type,
    GError **error);

static FsParticipant *fs_msn_conference_new_participant (FsBaseConference *conf,
    const gchar *cname,
    GError **error);

static void _remove_session (gpointer user_data,
    GObject *where_the_object_was);
static void _remove_participant (gpointer user_data,
    GObject *where_the_object_was);

static void
fs_msn_conference_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (fsmsnconference_debug, "fsmsnconference", 0,
                           "Farsight MSN Conference Element");
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

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
fs_msn_conference_finalize (GObject * object)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (object);

  g_free (self->priv->local_address);
  self->priv->local_address = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
fs_msn_conference_class_init (FsMsnConferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  FsBaseConferenceClass *baseconf_class = FS_BASE_CONFERENCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsMsnConferencePrivate));

  parent_class = g_type_class_peek_parent (klass);

  baseconf_class->new_session =
    GST_DEBUG_FUNCPTR (fs_msn_conference_new_session);
  baseconf_class->new_participant =
    GST_DEBUG_FUNCPTR (fs_msn_conference_new_participant);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (fs_msn_conference_finalize);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (fs_msn_conference_dispose);
  gobject_class->set_property =
    GST_DEBUG_FUNCPTR (fs_msn_conference_set_property);
  gobject_class->get_property =
    GST_DEBUG_FUNCPTR (fs_msn_conference_get_property);

  gst_element_class_set_details (gstelement_class, &fs_msn_conference_details);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_msn_conference_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_msn_conference_src_template));

  g_object_class_install_property (gobject_class,PROP_LOCAL_MSNADD,
      g_param_spec_string ("local_address", "Msn Address",
          "The local contact address for the MSN sessions",
          NULL, G_PARAM_READWRITE));
}

static void
fs_msn_conference_base_init (gpointer g_class)
{
}

static void
fs_msn_conference_init (FsMsnConference *conf,
                        FsMsnConferenceClass *bclass)
{
  GST_DEBUG_OBJECT (conf, "fs_msn_conference_init");

  conf->priv = FS_MSN_CONFERENCE_GET_PRIVATE (conf);
}

static void
fs_msn_conference_get_property (GObject *object,
                                guint prop_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (object);

  switch (prop_id)
  {
    case PROP_LOCAL_MSNADD:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value,self->priv->local_address);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
fs_msn_conference_set_property (GObject *object,
                                guint prop_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (object);

  switch (prop_id)
  {
    case PROP_LOCAL_MSNADD:
      GST_OBJECT_LOCK (self);
      g_free (self->priv->local_address);
      self->priv->local_address = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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
fs_msn_conference_new_session (FsBaseConference *conf,
                               FsMediaType media_type,
                               GError **error)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (conf);
  FsMsnSession *new_session = NULL;

  if (media_type != FS_MEDIA_TYPE_VIDEO)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Only video supported for msn webcam");
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

  new_session = fs_msn_session_new (media_type, self, 1, error);

  if (new_session)
    g_object_weak_ref (G_OBJECT (new_session), _remove_session, self);

  GST_OBJECT_LOCK (self);
  if (new_session)
  {
    self->priv->session = new_session;
    g_object_weak_ref (G_OBJECT (new_session), _remove_session, self);
  }
  GST_OBJECT_UNLOCK (self);

  return FS_SESSION (new_session);
}


static FsParticipant *
fs_msn_conference_new_participant (FsBaseConference *conf,
                                   const gchar *cname,
                                   GError **error)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (conf);
  FsMsnParticipant *new_participant = NULL;

  GST_OBJECT_LOCK (self);
  if (self->priv->participant)
  {
    GST_OBJECT_UNLOCK (self);
    g_set_error (error, FS_ERROR, FS_ERROR_ALREADY_EXISTS,
        "There already is a participant");
    return NULL;
  }

  GST_OBJECT_UNLOCK (self);

  new_participant = fs_msn_participant_new (cname);

  if (new_participant)
    g_object_weak_ref (G_OBJECT (new_participant), _remove_participant, self);

  GST_OBJECT_LOCK (self);
  if (new_participant)
  {
    self->priv->participant = new_participant;
    g_object_weak_ref (G_OBJECT (new_participant), _remove_participant, self);
  }
  GST_OBJECT_UNLOCK (self);

  return FS_PARTICIPANT (new_participant);

}


static gboolean plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "fsmsnconference",
                               GST_RANK_NONE, FS_TYPE_MSN_CONFERENCE);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "fsmsnconference",
  "Farsight MSN Conference plugin",
  plugin_init,
  VERSION,
  "LGPL",
  "Farsight",
  "http://farsight.freedesktop.org/"
)

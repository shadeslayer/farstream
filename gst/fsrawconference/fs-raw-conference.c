/*
 * Farsight2 - Farsight Raw Conference Implementation
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-raw-conference.c - Raw implementation for Farsight Conference Gstreamer
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
 * SECTION:fs-raw-conference
 * @short_description: Farsight Raw Conference Gstreamer Elements Base class
 *
 * This element implements a raw content stream over which any Gstreamer
 * content may travel.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-raw-conference.h"

#include "fs-raw-participant.h"

GST_DEBUG_CATEGORY (fsrawconference_debug);
#define GST_CAT_DEFAULT fsrawconference_debug

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


static GstStaticPadTemplate fs_raw_conference_sink_template =
  GST_STATIC_PAD_TEMPLATE ("sink_%d",
      GST_PAD_SINK,
      GST_PAD_SOMETIMES,
      GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate fs_raw_conference_src_template =
  GST_STATIC_PAD_TEMPLATE ("src_%d_%d_%d",
      GST_PAD_SRC,
      GST_PAD_SOMETIMES,
      GST_STATIC_CAPS_ANY);

#define FS_RAW_CONFERENCE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), FS_TYPE_RAW_CONFERENCE,  \
      FsRawConferencePrivate))

struct _FsRawConferencePrivate
{
  gboolean disposed;

  /* Protected by GST_OBJECT_LOCK */
  GList *participants;
};

static void fs_raw_conference_do_init (GType type);


GST_BOILERPLATE_FULL (FsRawConference, fs_raw_conference, FsBaseConference,
    FS_TYPE_BASE_CONFERENCE, fs_raw_conference_do_init);

static FsSession *fs_raw_conference_new_session (FsBaseConference *conf,
    FsMediaType media_type,
    GError **error);

static FsParticipant *fs_raw_conference_new_participant (FsBaseConference *conf,
    const gchar *cname,
    GError **error);

static void _remove_participant (gpointer user_data,
    GObject *where_the_object_was);

static void
fs_raw_conference_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (fsrawconference_debug, "fsrawconference", 0,
                           "Farsight Raw Conference Element");
}

static void
fs_raw_conference_dispose (GObject * object)
{
  FsRawConference *self = FS_RAW_CONFERENCE (object);
  GList *item;

  if (self->priv->disposed)
    return;

  for (item = g_list_first (self->priv->participants);
       item;
       item = g_list_next (item))
    g_object_weak_unref (G_OBJECT (item->data), _remove_participant, self);
  g_list_free (self->priv->participants);
  self->priv->participants = NULL;

  self->priv->disposed = TRUE;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
fs_raw_conference_class_init (FsRawConferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  FsBaseConferenceClass *baseconf_class = FS_BASE_CONFERENCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsRawConferencePrivate));

  baseconf_class->new_session =
    GST_DEBUG_FUNCPTR (fs_raw_conference_new_session);
  baseconf_class->new_participant =
    GST_DEBUG_FUNCPTR (fs_raw_conference_new_participant);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (fs_raw_conference_dispose);
}

static void
fs_raw_conference_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_raw_conference_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_raw_conference_src_template));
}

static void
fs_raw_conference_init (FsRawConference *conf,
                        FsRawConferenceClass *bclass)
{
  GST_DEBUG_OBJECT (conf, "fs_raw_conference_init");

  conf->priv = FS_RAW_CONFERENCE_GET_PRIVATE (conf);
}

static void
_remove_participant (gpointer user_data,
                     GObject *where_the_object_was)
{
  FsRawConference *self = FS_RAW_CONFERENCE (user_data);

  GST_OBJECT_LOCK (self);
  self->priv->participants =
    g_list_remove_all (self->priv->participants, where_the_object_was);
  GST_OBJECT_UNLOCK (self);
}

static FsSession *
fs_raw_conference_new_session (FsBaseConference *conf,
                               FsMediaType media_type,
                               GError **error)
{
  return NULL;
}


static FsParticipant *
fs_raw_conference_new_participant (FsBaseConference *conf,
                                   const gchar *cname,
                                   GError **error)
{
  FsRawConference *self = FS_RAW_CONFERENCE (conf);
  FsParticipant *new_participant = NULL;
  GList *item = NULL;

  if (cname)
  {
    GST_OBJECT_LOCK (self);
    for (item = g_list_first (self->priv->participants);
         item;
         item = g_list_next (item))
    {
      gchar *lcname;

      g_object_get (item->data, "cname", &lcname, NULL);
      if (lcname && !g_strcmp0 (lcname, cname))
      {
        g_free (lcname);
        break;
      }
      g_free (lcname);
    }
    GST_OBJECT_UNLOCK (self);

    if (item)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "There is already a participant with this cname");
      return NULL;
    }
  }

  new_participant = FS_PARTICIPANT_CAST (fs_raw_participant_new (cname));


  GST_OBJECT_LOCK (self);
  self->priv->participants = g_list_append (self->priv->participants,
      new_participant);
  GST_OBJECT_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_participant), _remove_participant, self);

  return new_participant;
}


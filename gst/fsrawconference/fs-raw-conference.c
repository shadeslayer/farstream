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

  if (self->priv->disposed)
    return;

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
  return NULL;
}


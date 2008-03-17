/*
 * Farsight2 - Farsight RTP Special Codec
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-special-source.c - A Farsight RTP Special Source gobject
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/farsight/fs-base-conference.h>

#include "fs-rtp-special-source.h"

#include "fs-rtp-dtmf-event-source.h"

#define GST_CAT_DEFAULT fsrtpconference_debug

/**
 * SECTION:fs-rtp-special-source
 * @short_description: Base class to abstract how special sources are handled
 *
 * This class defines how special sources can be handled, it is the base
 * for DMTF and CN sources.
 *
 */

struct _FsRtpSpecialSourcePrivate {
  gboolean disposed;
};

static GObjectClass *parent_class = NULL;

static GList *classes = NULL;

G_DEFINE_ABSTRACT_TYPE(FsRtpSpecialSource, fs_rtp_special_source, G_TYPE_OBJECT);

#define FS_RTP_SPECIAL_SOURCE_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_SPECIAL_SOURCE,             \
   FsRtpSpecialSourcePrivate))

static void fs_rtp_special_source_dispose (GObject *object);

static FsRtpSpecialSource *
fs_rtp_special_source_new (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    GError **error);
static gboolean
fs_rtp_special_source_update (FsRtpSpecialSource *source,
    GList *negotiated_codecs,
    FsCodec *selected_codec);

static void
fs_rtp_special_source_class_init (FsRtpSpecialSourceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = fs_rtp_special_source_parent_class;

  gobject_class->dispose = fs_rtp_special_source_dispose;

  if (!classes)
  {
    classes = g_list_prepend (classes,
        g_type_class_ref (FS_TYPE_RTP_DTMF_EVENT_SOURCE));
  }
}


static void
fs_rtp_special_source_init (FsRtpSpecialSource *self)
{
  self->priv = FS_RTP_SPECIAL_SOURCE_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_rtp_special_source_dispose (GObject *object)
{
  FsRtpSpecialSource *self = FS_RTP_SPECIAL_SOURCE (object);

  if (self->priv->disposed)
    return;

  self->priv->disposed = TRUE;
  G_OBJECT_CLASS (fs_rtp_special_source_parent_class)->dispose (object);
}


static GList*
fs_rtp_special_source_class_add_blueprint (FsRtpSpecialSourceClass *klass,
    GList *blueprints)
{
  if (klass->add_blueprint)
    return klass->add_blueprint (klass, blueprints);

  return blueprints;
}

static gboolean
fs_rtp_special_source_class_want_source (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec)
{
  if (klass->want_source)
    return klass->want_source (klass, negotiated_codecs, selected_codec);

  return FALSE;
}

GList *
fs_rtp_special_sources_add_blueprints (GList *blueprints)
{
  GList *item = NULL;

  for (item = g_list_first (classes);
       item;
       item = g_list_next (item))
  {
    FsRtpSpecialSourceClass *klass = item->data;
    blueprints = fs_rtp_special_source_class_add_blueprint (klass, blueprints);
  }

  return blueprints;
}

/**
 * fs_rtp_special_sources_update:
 * @current_extra_sources: The #GList returned by previous calls to this function
 * @negotiated_codecs: A #GList of current negotiated #FsCodec
 * @error: NULL or the local of a #GError
 *
 * This function checks which extra sources are currently being used and
 * which should be used according to currently negotiated codecs. It then
 * creates, destroys or modifies the list accordingly
 *
 * Returns: A #GList to be passed to other functions in this class
 */

GList *
fs_rtp_special_sources_update (
    GList *current_extra_sources,
    GList *negotiated_codecs,
    FsCodec *send_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    GError **error)
{
  GList *klass_item = NULL;

  for (klass_item = g_list_first (classes);
       klass_item;
       klass_item = g_list_next (klass_item))
  {
    FsRtpSpecialSourceClass *klass = klass_item->data;
    GList *obj_item;
    FsRtpSpecialSource *obj = NULL;

    /* Check if we already have an object for this type */
    for (obj_item = g_list_first (current_extra_sources);
         obj_item;
         obj_item = g_list_next (obj_item))
    {
      obj = obj_item->data;
      if (G_OBJECT_TYPE(obj) == G_OBJECT_CLASS_TYPE(klass))
        break;
    }

    if (obj_item)
    {
      if (fs_rtp_special_source_class_want_source (klass, negotiated_codecs,
              send_codec))
      {
        if (!fs_rtp_special_source_update (obj, negotiated_codecs, send_codec))
        {
          current_extra_sources = g_list_remove (current_extra_sources, obj);
          g_object_unref (obj);
          obj = fs_rtp_special_source_new (klass, negotiated_codecs, send_codec,
              bin, rtpmuxer, error);
          if (!obj)
            goto error;
          current_extra_sources = g_list_prepend (current_extra_sources, obj);
        }
      }
      else
      {
        current_extra_sources = g_list_remove (current_extra_sources, obj);
        g_object_unref (obj);
      }
    }
    else
    {
      if (fs_rtp_special_source_class_want_source (klass, negotiated_codecs,
              send_codec))
      {
        obj = fs_rtp_special_source_new (klass, negotiated_codecs, send_codec,
            bin, rtpmuxer, error);
        if (!obj)
          goto error;
        current_extra_sources = g_list_prepend (current_extra_sources, obj);
      }
    }
  }

  error:

  return current_extra_sources;
}


static FsRtpSpecialSource *
fs_rtp_special_source_new (FsRtpSpecialSourceClass *klass,
    GList *negotiated_sources,
    FsCodec *selected_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    GError **error)
{
  if (klass->new)
    return klass->new (klass, negotiated_sources, selected_codec, bin, rtpmuxer,
        error);

  g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
      "new not defined for %s", G_OBJECT_CLASS_NAME (klass));

  return NULL;
}

static gboolean
fs_rtp_special_source_update (FsRtpSpecialSource *source,
    GList *negotiated_sources, FsCodec *selected_codec)
{
  FsRtpSpecialSourceClass *klass = FS_RTP_SPECIAL_SOURCE_GET_CLASS (source);

  if (klass->update)
    return klass->update (source, negotiated_sources, selected_codec);

  return FALSE;
}


gboolean
fs_rtp_special_sources_start_telephony_event (GList *current_extra_sources,
      guint8 event,
      guint8 volume,
      FsDTMFMethod method)
{
  GList *item = NULL;

  for (item = g_list_first (current_extra_sources);
       item;
       item = g_list_next (item))
  {
    FsRtpSpecialSource *source = item->data;
    FsRtpSpecialSourceClass *klass = FS_RTP_SPECIAL_SOURCE_GET_CLASS (source);

    if (klass->start_telephony_event)
      if (klass->start_telephony_event (source, event, volume, method))
        return TRUE;
  }

  return FALSE;
}

gboolean
fs_rtp_special_sources_stop_telephony_event (GList *current_extra_sources,
    FsDTMFMethod method)
{
  GList *item = NULL;

  for (item = g_list_first (current_extra_sources);
       item;
       item = g_list_next (item))
  {
    FsRtpSpecialSource *source = item->data;
    FsRtpSpecialSourceClass *klass = FS_RTP_SPECIAL_SOURCE_GET_CLASS (source);

    if (klass->stop_telephony_event)
      if (klass->stop_telephony_event (source, method))
        return TRUE;
  }

  return FALSE;
}

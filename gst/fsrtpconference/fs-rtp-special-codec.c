/*
 * Farsight2 - Farsight RTP Special Codec
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-substream.c - A Farsight RTP Substream gobject
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

#include "fs-rtp-special-codec.h"

#define GST_CAT_DEFAULT fsrtpconference_debug

/**
 * SECTION:fs-rtp-special-codec
 * @short_description: Base class to abstract how special codecs are handled
 *
 * This class defines how special codecs can be handled, it is the base
 * for DMTF and CN sources.
 *
 */


#define DEFAULT_NO_RTCP_TIMEOUT (7000)

struct _FsRtpSpecialCodecPrivate {
  gboolean disposed;
};

static GObjectClass *parent_class = NULL;

static GList *classes = NULL;

G_DEFINE_ABSTRACT_TYPE(FsRtpSpecialCodec, fs_rtp_special_codec, G_TYPE_OBJECT);

#define FS_RTP_SPECIAL_CODEC_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_SPECIAL_CODEC,             \
   FsRtpSpecialCodecPrivate))

static void fs_rtp_special_codec_dispose (GObject *object);

static FsRtpSpecialCodec *
fs_rtp_special_codec_new (FsRtpSpecialCodecClass *klass,
    GList *negotiated_codecs,
    GError **error);
static gboolean
fs_rtp_special_codec_update (FsRtpSpecialCodec *codec,
    GList *negotiated_codecs);

static void
fs_rtp_special_codec_class_init (FsRtpSpecialCodecClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = fs_rtp_special_codec_parent_class;

  gobject_class->dispose = fs_rtp_special_codec_dispose;

  if (!classes)
  {
    //classes = g_list_prepend (classes, g_type_ref_class(GTYPE1));
  }
}


static void
fs_rtp_special_codec_init (FsRtpSpecialCodec *self)
{
  self->priv = FS_RTP_SPECIAL_CODEC_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_rtp_special_codec_dispose (GObject *object)
{
  FsRtpSpecialCodec *self = FS_RTP_SPECIAL_CODEC (object);

  if (self->priv->disposed)
    return;

  self->priv->disposed = TRUE;
  G_OBJECT_CLASS (fs_rtp_special_codec_parent_class)->dispose (object);
}


static GList*
fs_rtp_special_codec_class_add_blueprint (FsRtpSpecialCodecClass *klass,
    GList *blueprints)
{
  if (klass->add_blueprint)
    return klass->add_blueprint (klass, blueprints);

  return blueprints;
}

static gboolean
fs_rtp_special_codec_class_want_codec (FsRtpSpecialCodecClass *klass,
    GList *negotiated_codecs)
{
  if (klass->want_codec)
    return klass->want_codec (klass, negotiated_codecs);

  return FALSE;
}

GList *
fs_rtp_special_codecs_add_blueprints (GList *blueprints)
{
  GList *item = NULL;

  for (item = g_list_first (classes);
       item;
       item = g_list_next (item))
  {
    FsRtpSpecialCodecClass *klass = item->data;
    blueprints = fs_rtp_special_codec_class_add_blueprint (klass, blueprints);
  }

  return blueprints;
}

/**
 * fs_rtp_special_codecs_update:
 * @current_extra_codecs: The #GList returned by previous calls to this function
 * @negotiated_codecs: A #GList of current negotiated #FsCodec
 * @error: NULL or the local of a #GError
 *
 * This function checks which extra codecs are currently being used and
 * which should be used according to currently negotiated codecs. It then
 * creates, destroys or modifies the list accordingly
 *
 * Returns: A #GList to be passed to other functions in this class
 */

GList *
fs_rtp_special_codecs_update (
    GList *current_extra_codecs,
    GList *negotiated_codecs,
    GError **error)
{
  GList *klass_item = NULL;

  for (klass_item = g_list_first (classes);
       klass_item;
       klass_item = g_list_next (klass_item))
  {
    FsRtpSpecialCodecClass *klass = klass_item->data;
    GList *obj_item;
    FsRtpSpecialCodec *obj = NULL;

    /* Check if we already have an object for this type */
    for (obj_item = g_list_first (current_extra_codecs);
         obj_item;
         obj_item = g_list_next (obj_item))
    {
      obj = obj_item->data;
      if (G_OBJECT_TYPE(obj) == G_OBJECT_CLASS_TYPE(klass))
        break;
    }

    if (obj_item)
    {
      if (fs_rtp_special_codec_class_want_codec (klass, negotiated_codecs))
      {
        if (!fs_rtp_special_codec_update (obj, negotiated_codecs))
        {
          current_extra_codecs = g_list_remove (current_extra_codecs, obj);
          g_object_unref (obj);
          obj = fs_rtp_special_codec_new (klass, negotiated_codecs, error);
          if (!obj)
            goto error;
          current_extra_codecs = g_list_prepend (current_extra_codecs, obj);
        }
      }
      else
      {
        current_extra_codecs = g_list_remove (current_extra_codecs, obj);
        g_object_unref (obj);
      }
    }
    else
    {
      if (fs_rtp_special_codec_class_want_codec (klass, negotiated_codecs))
      {
        obj = fs_rtp_special_codec_new (klass, negotiated_codecs, error);
        if (!obj)
          goto error;
        current_extra_codecs = g_list_prepend (current_extra_codecs, obj);
      }
    }
  }

  error:

  return current_extra_codecs;
}


static FsRtpSpecialCodec *
fs_rtp_special_codec_new (FsRtpSpecialCodecClass *klass,
    GList *negotiated_codecs,
    GError **error)
{
  /* STUB */
  return NULL;
}

static gboolean
fs_rtp_special_codec_update (FsRtpSpecialCodec *codec,
    GList *negotiated_codecs)
{
  return FALSE;
}

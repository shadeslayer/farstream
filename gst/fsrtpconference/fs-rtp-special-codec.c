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
fs_rtp_special_codec_class_add_blueprint (FsRtpSpecialCodecClass *class,
    GList *blueprints)
{
  if (class->add_blueprint)
    return class->add_blueprint (blueprints);

  return blueprints;
}

GList *
fs_rtp_special_codecs_add_blueprints (GList *blueprints)
{
  GList *item = NULL;

  for (item = g_list_first (classes);
       item;
       item = g_list_next (item))
  {
    FsRtpSpecialCodecClass *class = item->data;
    blueprints = fs_rtp_special_codecs_add_blueprint (class, blueprints);
  }

  return blueprints;
}

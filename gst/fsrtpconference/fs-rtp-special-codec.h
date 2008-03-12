/*
 * Farsight2 - Farsight RTP Special Codec
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-substream.h - A Farsight RTP Substream gobject
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


#ifndef __FS_RTP_SPECIAL_CODEC_H__
#define __FS_RTP_SPECIAL_CODEC_H__

#include <glib.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_SPECIAL_CODEC \
  (fs_rtp_special_codec_get_type())
#define FS_RTP_SPECIAL_CODEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_SPECIAL_CODEC, \
      FsRtpSpecialCodec))
#define FS_RTP_SPECIAL_CODEC_CLASS(klass) \
 (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_SPECIAL_CODEC, \
     FsRtpSpecialCodecClass))
#define FS_IS_RTP_SPECIAL_CODEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_SPECIAL_CODEC))
#define FS_IS_RTP_SPECIAL_CODEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_SPECIAL_CODEC))
#define FS_RTP_SPECIAL_CODEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_SPECIAL_CODEC,   \
    FsRtpSpecialCodecClass))
#define FS_RTP_SPECIAL_CODEC_CAST(obj) ((FsRtpSpecialCodec*) (obj))

typedef struct _FsRtpSpecialCodec FsRtpSpecialCodec;
typedef struct _FsRtpSpecialCodecClass FsRtpSpecialCodecClass;
typedef struct _FsRtpSpecialCodecPrivate FsRtpSpecialCodecPrivate;

struct _FsRtpSpecialCodecClass
{
  GObjectClass parent_class;

  gboolean (*want_codec) (GList *negotiated_codecs, GError **error);

  GList* (*add_blueprint) (GList *blueprints);
};

/**
 * FsRtpSpecialCodec:
 *
 */
struct _FsRtpSpecialCodec
{
  GObject parent;
  FsRtpSpecialCodecPrivate *priv;
};

GType fs_rtp_special_codec_get_type (void);

GList *
fs_rtp_special_codecs_update (
    GList *current_extra_codecs,
    GList *negotiated_codecs,
    GError **error);

GList *
fs_rtp_special_codecs_add_blueprints (GList *blueprints);


G_END_DECLS

#endif /* __FS_RTP_SPECIAL_CODEC_H__ */

/*
 * Farsight2 - Farsight Codec
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * Copyright 2005 Collabora Ltd.
 *   @author: Rob Taylor <rob.taylor@collabora.co.uk>
 *
 * fs-codec.h - A Farsight codec
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

#ifndef __FS_CODEC_H__
#define __FS_CODEC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _FsCodec FsCodec;
typedef struct _FsCodecParameter FsCodecParameter;

#define FS_TYPE_CODEC \
  (fs_codec_get_type())

#define FS_TYPE_CODEC_LIST \
  (fs_codec_list_get_type())

/**
 * FsMediaType:
 * @FS_MEDIA_TYPE_AUDIO: A media type that encodes audio.
 * @FS_MEDIA_TYPE_VIDEO: A media type that encodes video.
 * @FS_MEDIA_TYPE_APPLICATION: A media type that ???
 * @FS_MEDIA_TYPE_LAST: Largest valid #FsMediaType
 *
 * Enum used to signify the media type of a codec or stream.
 */
typedef enum
{
  FS_MEDIA_TYPE_AUDIO,
  FS_MEDIA_TYPE_VIDEO,
  FS_MEDIA_TYPE_APPLICATION,
  FS_MEDIA_TYPE_LAST = FS_MEDIA_TYPE_APPLICATION
} FsMediaType;

/**
 * FS_CODEC_ID_ANY:
 *
 * If the id of a #FsCodec is #FS_CODEC_ID_ANY, then it will be replaced
 * with a dynamic payload type at runtime
 */

/**
 * FS_CODEC_ID_DISABLE:
 *
 * If the id of a #FsCodec is #FS_CODEC_ID_DISABLE, then this codec will
 * not be used
 */

#define FS_CODEC_ID_ANY            (-1)
#define FS_CODEC_ID_DISABLE        (-2)

/**
 * FsCodec:
 * @id: numeric identifier for encoding, eg. PT for SDP
 * @encoding_name: the name of the codec
 * @media_type: type of media this codec is for
 * @clock_rate: clock rate of this stream
 * @channels: Number of channels codec should decode
 * @optional_params:  key pairs of param name to param data
 *
 * This structure reprensents one codec that can be offered or received
 */
/* TODO Should this be made into a GstStructure? */
struct _FsCodec
{
  gint id;
  char *encoding_name;
  FsMediaType media_type;
  guint clock_rate;
  guint channels;
  GList *optional_params;
  /*< private >*/
  gpointer _padding[4];         /* padding for binary-compatible
                                   expansion*/
};

/**
 * FsCodecParameter:
 * @name: paramter name.
 * @value: parameter value.
 *
 * Used to store arbitary parameters for a codec
 */
struct _FsCodecParameter {
    gchar *name;
    gchar *value;
};

GType fs_codec_get_type (void);
GType fs_codec_list_get_type (void);


FsCodec *fs_codec_new (int id, const char *encoding_name,
                       FsMediaType media_type, guint clock_rate);

void fs_codec_destroy (FsCodec * codec);
FsCodec *fs_codec_copy (const FsCodec * codec);
void fs_codec_list_destroy (GList *codec_list);
GList *fs_codec_list_copy (const GList *codec_list);

GList *fs_codec_list_from_keyfile (const gchar *filename, GError **error);
gchar *fs_codec_to_string (const FsCodec *codec);

gboolean fs_codec_are_equal (const FsCodec *codec1, const FsCodec *codec2);

GstCaps *fs_codec_to_gst_caps (const FsCodec *codec);

const gchar *fs_media_type_to_string (FsMediaType media_type);

G_END_DECLS

#endif /* __FS_CODEC_H__ */

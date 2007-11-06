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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __FS_CODEC_H__
#define __FS_CODEC_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _FsCodec FsCodec;
typedef struct _FsCodecParameter FsCodecParameter;
typedef struct _FsCodecPreference FsCodecPreference;

#define FS_TYPE_CODEC \
  (fs_codec_get_type())

#define FS_TYPE_CODEC_LIST \
  (fs_codec_list_get_type())

#define FS_TYPE_MEDIA_TYPE \
  (fs_media_type_get_type())

/**
 * FsMediaType:
 * @FS_MEDIA_TYPE_AUDIO: A media type that encodes audio.
 * @FS_MEDIA_TYPE_VIDEO: A media type that encodes video.
 * @FS_MEDIA_TYPE_AV: A media type that encodes muxed audio and video.
 *
 * Enum used to signify the media type of a codec or stream.
 */
typedef enum
{
  FS_MEDIA_TYPE_AUDIO,
  FS_MEDIA_TYPE_VIDEO,
  FS_MEDIA_TYPE_AV,
  FS_MEDIA_TYPE_LAST = FS_MEDIA_TYPE_AV
} FsMediaType;

/**
 * FsCodec:
 * @id: numeric identifier for encoding, eg. PT for SDP
 * @encoding_name: the name of the codec
 * @media_type: type of media this codec is for
 * @clock_rate: clock rate of this stream
 * @channels: Number of channels codec should decode
 * @optional_params:  key pairs of param name to param data
 */
struct _FsCodec
{
  /* TODO Should this be made into a GstStructure? */
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

/**
 * FsCodecPreference:
 * @encoding_name: name of encoding preferred
 * @clock_rate: rate of codec preffered
 *
 * Used to give a preferece for what type of codec to use.
 */
struct _FsCodecPreference {
    gchar *encoding_name;
    gint clock_rate;
};

GType fs_codec_get_type (void);
GType fs_codec_list_get_type (void);
GType fs_media_type_get_type (void);


FsCodec *fs_codec_new (int id, const char *encoding_name,
                       FsMediaType media_type, guint clock_rate);

void fs_codec_destroy (FsCodec * codec);
FsCodec *fs_codec_copy (FsCodec * codec);
void fs_codec_list_destroy (GList *codec_list);
GList *fs_codec_list_copy (const GList *codec_list);

GList *fs_codec_list_from_keyfile (const gchar *filename);
gchar *fs_codec_to_string (FsCodec *codec);

gboolean fs_codec_are_equal (FsCodec *codec1, FsCodec *codec2);

const gchar *fs_media_type_to_string (FsMediaType media_type);

G_END_DECLS

#endif /* __FS_CODEC_H__ */

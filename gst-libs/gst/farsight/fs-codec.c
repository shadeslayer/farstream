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
 * fs-codec.c - A Farsight codec
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

#include "fs-codec.h"

#include <string.h>

/**
 * SECTION:fs-codec
 * @short_description: Structure representing a media codec
 *
 * An #FsCodec is a way to exchange codec information between the client and
 * Farsight. The information specified in this structure is usually
 * representative of the codec information exchanged in the signaling.
 *
 */

/* TODO Make a fs_codec_new() function since there is a _destroy() */

GType
fs_codec_get_type (void)
{
  static GType codec_type = 0;
  if (codec_type == 0)
  {
    codec_type = g_boxed_type_register_static (
        "FsCodec",
        (GBoxedCopyFunc)fs_codec_destroy,
        (GBoxedFreeFunc)fs_codec_copy);
  }

  return codec_type;
}

GType
fs_codec_list_get_type (void)
{
  static GType codec_list_type = 0;
  if (codec_list_type == 0)
  {
    codec_list_type = g_boxed_type_register_static (
        "FsCodecGList",
        (GBoxedCopyFunc)fs_codec_list_destroy,
        (GBoxedFreeFunc)fs_codec_list_copy);
  }

  return codec_list_type;
}

GType
fs_media_type_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      { FS_MEDIA_TYPE_AUDIO, "Audio (default)", "audio"},
      { FS_MEDIA_TYPE_VIDEO, "Video", "video"},
      { FS_MEDIA_TYPE_AV, "Audio/Video", "av" },
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("FsMediaType", values);
  }
  return gtype;
}

/**
 * fs_codec_init:
 * @codec: #FsCodec structure to initialise
 * @id: codec identifier, if RTP this should be based on IETF RTP payload types
 * @encoding_name: Name of media type this encodes
 * @media_type: #FsMediaType for type of codec
 * @clock_rate: The clock rate this codec encodes at, if applicable
 *
 * Allocates and initializes a #FsCodec structure
 *
 * Returns: A newly allocated #FsCodec
 */
FsCodec *
fs_codec_new (int id, const char *encoding_name,
              FsMediaType media_type, guint clock_rate)
{
  FsCodec *codec = g_new0 (FsCodec, 1);

  codec->id = id;
  if (encoding_name)
    codec->encoding_name = g_strdup (encoding_name);
  else
    codec->encoding_name = NULL;
  codec->media_type = media_type;
  codec->clock_rate = clock_rate;

  return codec;
}

/**
 * fs_codec_destroy:
 * @codec: #FsCodec structure to free
 *
 * Deletes a #FsCodec structure and all its data
 */
void
fs_codec_destroy (FsCodec * codec)
{
  if (codec->encoding_name)
    g_free (codec->encoding_name);
  if (codec->optional_params) {
    GList *lp;
    FsCodecParameter *optional_param;

    for (lp = codec->optional_params; lp; lp = g_list_next (lp)) {
      optional_param = (FsCodecParameter *) lp->data;
      g_free (optional_param->name);
      g_free (optional_param->value);
      g_free (optional_param);
    }
    g_list_free (codec->optional_params);
  }

  g_free (codec);
}

/**
 * fs_codec_copy:
 * @codec: codec to copy
 *
 * Copies a #FsCodec structure.
 *
 * Returns: a copy of the codec
 */
FsCodec *
fs_codec_copy (FsCodec * codec)
{
  FsCodec *copy = g_new0 (FsCodec, 1);

  copy->id = codec->id;
  copy->media_type = codec->media_type;
  copy->clock_rate = codec->clock_rate;
  copy->channels = codec->channels;

  if (codec->encoding_name)
    copy->encoding_name = g_strdup (codec->encoding_name);
  else
    copy->encoding_name = NULL;

  copy->optional_params = NULL;

  if (codec->optional_params) {
    GList *lp;
    FsCodecParameter *param;
    FsCodecParameter *param_copy;

    for (lp = codec->optional_params; lp; lp = g_list_next (lp)) {
      param_copy = g_new0(FsCodecParameter,1);
      param = (FsCodecParameter *) lp->data;
      param_copy->name = g_strdup (param->name);
      param_copy->value = g_strdup (param->value);
      /* prepend then reverse the list for efficiency */
      copy->optional_params = g_list_prepend (copy->optional_params,
                                              param_copy);
    }
    copy->optional_params = g_list_reverse (copy->optional_params);
  }
  return copy;
}

/**
 * fs_codec_list_destroy:
 * @codec_list: a GList of #FsCodec to delete
 *
 * Deletes a list of #FsCodec structures and the list itself
 */
void
fs_codec_list_destroy (GList *codec_list)
{
  GList *lp;
  FsCodec *codec;

  for (lp = codec_list; lp; lp = g_list_next (lp)) {
    codec = (FsCodec *) lp->data;
    fs_codec_destroy(codec);
    lp->data = NULL;
  }
  g_list_free (codec_list);
}

/**
 * fs_codec_list_copy:
 * @codec_list: a GList of #FsCodec to copy
 *
 * Copies a list of #FsCodec structures.
 *
 * Returns: The new list.
 */
GList *
fs_codec_list_copy (const GList *codec_list)
{
  GList *copy = NULL;
  const GList *lp;
  FsCodec *codec;

  for (lp = codec_list; lp; lp = g_list_next (lp)) {
    codec = (FsCodec *) lp->data;
    /* prepend then reverse the list for efficiency */
    copy = g_list_prepend (copy, fs_codec_copy (codec));
  }
  copy = g_list_reverse (copy);
  return copy;
}

/**
 * fs_codec_list_from_keyfile
 * @filename: Name of the #GKeyFile to read the codecs parameters from
 *
 * Reads the content of a #GKeyFile of the following format into
 * a #GList of #FsCodec structures.
 *
 *
 * Example:
 * |[
 * [audio/codec1]
 * clock-rate=8000
 *
 * [audio/codec1:1]
 * clock-rate=16000
 *
 * [audio/codec2]
 * one_param=QCIF
 * another_param=WOW
 * ]|
 *
 * Return value: The read #GList of #FsCodec or %NULL if an error appended
 */
GList *
fs_codec_list_from_keyfile (const gchar *filename)
{
  GKeyFile *keyfile = NULL;
  GList *codecs = NULL;
  GError *gerror = NULL;
  gchar **groups = NULL;
  gsize groups_count = 0;
  int i;

  g_assert (filename);

  keyfile = g_key_file_new ();

  if (!g_key_file_load_from_file (keyfile, filename,
          G_KEY_FILE_NONE, &gerror)) {
    if (gerror) {
      g_debug ("Unable to read file %s: %s", filename, gerror->message);
      g_error_free (gerror);
    } else {
      g_warning ("Unknown error reading file %s", filename);
    }
    goto out;
  }

  groups = g_key_file_get_groups (keyfile, &groups_count);

  if (!groups)
    goto out;

  for (i=0; i < groups_count && groups[i]; i++) {
    FsCodec *codec = g_new0 (FsCodec, 1);
    gchar **keys = NULL;
    gsize keys_count;
    int j;
    gchar *encoding_name = NULL;
    gchar *next_tok = NULL;

    codec->id = -1;

    keys = g_key_file_get_keys (keyfile, groups[i], &keys_count, &gerror);

    if (!keys || gerror) {
      if (gerror) {
        g_warning ("Unable to read parameters for %s: %s\n",
            groups[i], gerror->message);

        g_clear_error (&gerror);
      } else {
        g_warning ("Unknown errors while reading parameters for %s",
            groups[i]);
      }

      goto next_codec;
    }

    next_tok = strchr (groups[i], '/');
    if (!next_tok) {
      g_warning ("Invalid codec name: %s", groups[i]);
      goto next_codec;
    }

    if ((next_tok - groups[i]) == 5 /* strlen("audio") */ &&
        !g_ascii_strncasecmp ("audio", groups[i], 5))
      codec->media_type = FS_MEDIA_TYPE_AUDIO;
    else if ((next_tok - groups[i]) == 5 /* strlen("video") */ &&
        !g_ascii_strncasecmp ("video", groups[i], 5))
      codec->media_type = FS_MEDIA_TYPE_VIDEO;
    else {
      g_warning ("Invalid media type in codec name name %s", groups[i]);
      goto next_codec;
    }

    encoding_name = next_tok+1;

    next_tok = strchr (groups[i], ':');

    if (next_tok) {
      codec->encoding_name = g_strndup (encoding_name,
          next_tok - encoding_name);
    } else {
      codec->encoding_name = g_strdup (encoding_name);
    }

    if (!codec->encoding_name || codec->encoding_name[0] == 0) {
      goto next_codec;
    }

    for (j = 0; j < keys_count && keys[j]; j++) {
      if (!strcmp ("clock-rate", keys[j])) {
        codec->clock_rate = g_key_file_get_integer (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror) {
          codec->clock_rate = 0;
          goto keyerror;
        }

      } else if (!strcmp ("id", keys[j])) {
         codec->id = g_key_file_get_integer (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror) {
          codec->id = 0;
          goto keyerror;
        }

      } else if (!strcmp ("channels", keys[j])) {
         codec->channels = g_key_file_get_integer (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror) {
          codec->channels = 0;
          goto keyerror;
        }

      } else {
        FsCodecParameter *param = g_new0 (FsCodecParameter, 1);

        param->name = g_strdup (keys[j]);
        param->value = g_key_file_get_string (keyfile, groups[i], keys[j], 
            &gerror);
        if (gerror) {
          g_free (param->name);
          g_free (param->value);
          g_free (param);
          goto keyerror;
        }

        if (!param->name || !param->value) {
          g_free (param->name);
          g_free (param->value);
          g_free (param);
        } else {
          codec->optional_params = g_list_append (codec->optional_params,
              param);
        }
      }
      continue;
    keyerror:
      g_warning ("Error reading key %s codec %s: %s", keys[j], groups[i],
          gerror->message);
      g_clear_error (&gerror);

    }

    codecs = g_list_append (codecs, codec);

    g_strfreev (keys);
    continue;
  next_codec:
    fs_codec_destroy (codec);
    g_strfreev (keys);

  }


 out:

  g_strfreev (groups);
  g_key_file_free (keyfile);

  return codecs;
}

const gchar *
fs_media_type_to_string (FsMediaType media_type)
{
  if (media_type == FS_MEDIA_TYPE_AUDIO) {
    return "audio";
  } else if (media_type == FS_MEDIA_TYPE_VIDEO) {
    return "video";
  } else if (media_type == FS_MEDIA_TYPE_AV) {
    return "audiovideo";
  } else {
    return NULL;
  }
}

/**
 * fs_codec_to_string
 * @codec: A farsight codec
 *
 * Returns a newly-allocated string representing the codec
 *
 * Return value: the newly-allocated string
 */
gchar *
fs_codec_to_string (FsCodec *codec)
{
  GString *string = g_string_new ("");
  GList *item;
  gchar *charstring;

  g_string_printf (string, "%d: %s %s clock:%d channels:%d",
      codec->id, fs_media_type_to_string (codec->media_type),
      codec->encoding_name, codec->clock_rate, codec->channels);

  for (item = codec->optional_params;
       item;
       item = g_list_next (item)) {
    FsCodecParameter *param = item->data;
    g_string_append_printf (string, " %s=%s", param->name, param->value);
  }

  charstring = string->str;
  g_string_free (string, FALSE);

  return charstring;
}

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

#include "fs-codec.h"

#include "fs-private.h"

#include <string.h>

#define GST_CAT_DEFAULT fs_base_conference_debug

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
        (GBoxedCopyFunc)fs_codec_copy,
        (GBoxedFreeFunc)fs_codec_destroy);
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
        (GBoxedCopyFunc)fs_codec_list_copy,
        (GBoxedFreeFunc)fs_codec_list_destroy);
  }

  return codec_list_type;
}

/**
 * fs_codec_new:
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
  FsCodec *codec = g_slice_new0 (FsCodec);

  codec->id = id;
  codec->encoding_name = g_strdup (encoding_name);
  codec->media_type = media_type;
  codec->clock_rate = clock_rate;

  return codec;
}

/**
 * fs_codec_destroy:
 * @codec: #FsCodec structure to free
 *
 * Deletes a #FsCodec structure and all its data. Is a no-op on %NULL codec
 */
void
fs_codec_destroy (FsCodec * codec)
{
  if (codec == NULL)
    return;

  g_free (codec->encoding_name);
  if (codec->optional_params) {
    GList *lp;
    FsCodecParameter *param;

    for (lp = codec->optional_params; lp; lp = g_list_next (lp)) {
      param = (FsCodecParameter *) lp->data;
      g_free (param->name);
      g_free (param->value);
      g_slice_free (FsCodecParameter, param);
    }
    g_list_free (codec->optional_params);
  }

  g_slice_free (FsCodec, codec);
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
fs_codec_copy (const FsCodec * codec)
{
  FsCodec *copy = NULL;
  GList *lp;
  FsCodecParameter *param;
  FsCodecParameter *param_copy;

  if (codec == NULL)
    return NULL;

  copy = g_slice_new0 (FsCodec);

  copy->id = codec->id;
  copy->media_type = codec->media_type;
  copy->clock_rate = codec->clock_rate;
  copy->channels = codec->channels;

  copy->encoding_name = g_strdup (codec->encoding_name);

  for (lp = codec->optional_params; lp; lp = g_list_next (lp))
  {
    param_copy = g_slice_new (FsCodecParameter);
    param = (FsCodecParameter *) lp->data;
    param_copy->name = g_strdup (param->name);
    param_copy->value = g_strdup (param->value);
    /* prepend then reverse the list for efficiency */
    copy->optional_params = g_list_prepend (copy->optional_params,
        param_copy);
  }
  copy->optional_params = g_list_reverse (copy->optional_params);

  return copy;
}

/**
 * fs_codec_list_destroy:
 * @codec_list: a GList of #FsCodec to delete
 *
 * Deletes a list of #FsCodec structures and the list itself.
 * Does nothing on %NULL lists.
 */
void
fs_codec_list_destroy (GList *codec_list)
{
  GList *lp;
  FsCodec *codec;

  for (lp = codec_list; lp; lp = g_list_next (lp)) {
    codec = (FsCodec *) lp->data;
    fs_codec_destroy (codec);
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
 * @error: location of a #GError, or NULL if no error occured
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
 * Return value: The #GList of #FsCodec or %NULL if the keyfile was empty
 *  or an error occured.
 */
GList *
fs_codec_list_from_keyfile (const gchar *filename, GError **error)
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
          G_KEY_FILE_NONE, error)) {
    goto out;
  }

  groups = g_key_file_get_groups (keyfile, &groups_count);

  if (!groups)
    goto out;

  for (i=0; i < groups_count && groups[i]; i++) {
    FsCodec *codec = g_slice_new0 (FsCodec);
    gchar **keys = NULL;
    gsize keys_count;
    int j;
    gchar *encoding_name = NULL;
    gchar *next_tok = NULL;

    codec->id = FS_CODEC_ID_ANY;

    keys = g_key_file_get_keys (keyfile, groups[i], &keys_count, &gerror);

    if (!keys || gerror) {
      if (gerror) {
        GST_WARNING ("Unable to read parameters for %s: %s\n",
            groups[i], gerror->message);

      } else {
        GST_WARNING ("Unknown errors while reading parameters for %s",
            groups[i]);
      }
      g_clear_error (&gerror);

      goto next_codec;
    }

    next_tok = strchr (groups[i], '/');
    if (!next_tok) {
      GST_WARNING ("Invalid codec name: %s", groups[i]);
      goto next_codec;
    }

    if ((next_tok - groups[i]) == 5 /* strlen ("audio") */ &&
        !g_ascii_strncasecmp ("audio", groups[i], 5))
      codec->media_type = FS_MEDIA_TYPE_AUDIO;
    else if ((next_tok - groups[i]) == 5 /* strlen ("video") */ &&
        !g_ascii_strncasecmp ("video", groups[i], 5))
      codec->media_type = FS_MEDIA_TYPE_VIDEO;
    else {
      GST_WARNING ("Invalid media type in codec name name %s", groups[i]);
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
          codec->id = FS_CODEC_ID_ANY;
          goto keyerror;
        }

        if (codec->id < 0)
          codec->id = FS_CODEC_ID_DISABLE;

      } else if (!strcmp ("channels", keys[j])) {
         codec->channels = g_key_file_get_integer (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror) {
          codec->channels = 0;
          goto keyerror;
        }

      } else {
        FsCodecParameter *param = g_slice_new (FsCodecParameter);

        param->name = g_strdup (keys[j]);
        param->value = g_key_file_get_string (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror) {
          g_free (param->name);
          g_free (param->value);
          g_slice_free (FsCodecParameter, param);
          goto keyerror;
        }

        if (!param->name || !param->value) {
          g_free (param->name);
          g_free (param->value);
          g_slice_free (FsCodecParameter, param);
        } else {
          codec->optional_params = g_list_append (codec->optional_params,
              param);
        }
      }
      continue;
    keyerror:
      GST_WARNING ("Error reading key %s codec %s: %s", keys[j], groups[i],
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

/**
 * fs_media_type_to_string
 * @media_type: A media type
 *
 * Gives a user-printable string representing the media type
 *
 * Return value: a static string representing the media type
 */

const gchar *
fs_media_type_to_string (FsMediaType media_type)
{
  if (media_type == FS_MEDIA_TYPE_AUDIO) {
    return "audio";
  } else if (media_type == FS_MEDIA_TYPE_VIDEO) {
    return "video";
  } else if (media_type == FS_MEDIA_TYPE_APPLICATION) {
    return "application";
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
fs_codec_to_string (const FsCodec *codec)
{
  GString *string = NULL;
  GList *item;
  gchar *charstring;

  if (codec == NULL)
    return g_strdup ("(NULL)");

  string = g_string_new ("");

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



/*
 * Check if all of the elements of list1 are in list2
 * It compares GLists of FarsightCodecParameter
 */
static gboolean
compare_lists (GList *list1, GList *list2)
{
  GList *item1;

  for (item1 = g_list_first (list1);
       item1;
       item1 = g_list_next (item1)) {
    FsCodecParameter *param1 = item1->data;
    GList *item2 = NULL;

    for (item2 = g_list_first (list2);
         item2;
         item2 = g_list_next (item2)) {
      FsCodecParameter *param2 = item2->data;

      if (!strcmp (param1->name, param2->name) &&
          !strcmp (param1->value, param2->value))
        break;
    }
    if (!item2)
      return FALSE;
  }

  return TRUE;
}


/**
 * fs_codec_are_equal:
 * @codec1: First codec
 * @codec2: Second codec
 *
 * Compare two codecs, it will declare two codecs to be identical even
 * if their optional parameters are in a different order. %NULL encoding names
 * are ignored.
 *
 * Return value: %TRUE of the codecs are identical, %FALSE otherwise
 */

gboolean
fs_codec_are_equal (const FsCodec *codec1, const FsCodec *codec2)
{
  if (codec1 == codec2)
    return TRUE;

  if (!codec1 || !codec2)
    return FALSE;

  if (codec1->id != codec2->id ||
      codec1->media_type != codec2->media_type ||
      codec1->clock_rate != codec2->clock_rate ||
      codec1->channels != codec2->channels ||
      codec1->encoding_name == NULL ||
      codec2->encoding_name == NULL ||
      strcmp (codec1->encoding_name, codec2->encoding_name))
    return FALSE;


  /* Is there a smarter way to compare to un-ordered linked lists
   * to make sure they contain exactly the same elements??
   */
  if (!compare_lists (codec1->optional_params, codec2->optional_params) ||
      !compare_lists (codec2->optional_params, codec1->optional_params))
    return FALSE;

  return TRUE;
}

/**
 * fs_codec_to_gst_caps
 * @codec: A #FsCodec to be converted
 *
 * This function converts a #FsCodec to a fixed #GstCaps with media type
 * application/x-rtp.
 *
 * Return value: A newly-allocated #GstCaps or %NULL if the codec was %NULL
 */

GstCaps *
fs_codec_to_gst_caps (const FsCodec *codec)
{
  GstCaps *caps;
  GstStructure *structure;
  GList *item;

  if (codec == NULL)
    return NULL;

  structure = gst_structure_new ("application/x-rtp", NULL);

  if (codec->encoding_name)
  {
    gchar *encoding_name = g_ascii_strup (codec->encoding_name, -1);

    if (!g_ascii_strcasecmp (encoding_name, "H263-N800")) {
      g_free (encoding_name);
      encoding_name = g_strdup ("H263-1998");
    }

    gst_structure_set (structure,
        "encoding-name", G_TYPE_STRING, encoding_name,
        NULL);
    g_free (encoding_name);
  }

  if (codec->clock_rate)
    gst_structure_set (structure,
      "clock-rate", G_TYPE_INT, codec->clock_rate, NULL);

  if (fs_media_type_to_string (codec->media_type))
    gst_structure_set (structure, "media", G_TYPE_STRING,
      fs_media_type_to_string (codec->media_type), NULL);

  if (codec->id >= 0 && codec->id < 128)
    gst_structure_set (structure, "payload", G_TYPE_INT, codec->id, NULL);

  if (codec->channels)
    gst_structure_set (structure, "channels", G_TYPE_INT, codec->channels,
      NULL);

  for (item = codec->optional_params;
       item;
       item = g_list_next (item)) {
    FsCodecParameter *param = item->data;
    gchar *lower_name = g_ascii_strdown (param->name, -1);
    gst_structure_set (structure, lower_name, G_TYPE_STRING, param->value,
      NULL);
    g_free (lower_name);
  }

  caps = gst_caps_new_full (structure, NULL);

  return caps;
}


/**
 * fs_codec_list_are_equal:
 * @list1: a #GList of #FsCodec
 * @list2: a #GList of #FsCodec
 *
 * Verifies if two glist of fscodecs are identical
 *
 * Returns: %TRUE if they are identical, %FALSE otherwise
 */

gboolean
fs_codec_list_are_equal (GList *list1, GList *list2)
{

  for (;
       list1 && list2;
       list1 = g_list_next (list1), list2 = g_list_next (list2))
  {
    if (!fs_codec_are_equal (list1->data, list2->data))
      return FALSE;
  }

  if (list1 == NULL && list2 == NULL)
    return TRUE;
  else
    return FALSE;
}

/**
 * fs_codec_add_optional_parameter:
 * @codec: The #FsCodec to add the parameter to
 * @name: The name of the optional parameter
 * @value: The value of the optional parameter
 *
 * This function adds an new optional parameter to a #FsCodec
 */

void
fs_codec_add_optional_parameter (FsCodec *codec,
    const gchar *name,
    const gchar *value)
{
  FsCodecParameter *param;

  g_return_if_fail (name != NULL && value != NULL);

  param = g_slice_new (FsCodecParameter);

  param->name = g_strdup (name);
  param->value = g_strdup (value);

  codec->optional_params = g_list_append (codec->optional_params, param);
}

/**
 * fs_codec_remove_optional_parameter:
 * @codec: a #FsCodec
 * @param: a pointer to the #FsCodecParameter to remove
 *
 * Removes an optional parameter from a codec
 */

void
fs_codec_remove_optional_parameter (FsCodec *codec,
    FsCodecParameter *param)
{
  g_free (param->name);
  g_free (param->value);
  g_slice_free (FsCodecParameter, param);
  codec->optional_params = g_list_remove (codec->optional_params, param);
}

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

#include <string.h>

#include "fs-private.h"

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
  codec->ABI.ABI.minimum_reporting_interval = G_MAXUINT;

  return codec;
}

static void
free_optional_parameter (FsCodecParameter *param)
{
  g_free (param->name);
  g_free (param->value);
  g_slice_free (FsCodecParameter, param);
}


static void
free_feedback_parameter (FsFeedbackParameter *param)
{
  g_free (param->type);
  g_free (param->subtype);
  g_free (param->extra_params);
  g_slice_free (FsFeedbackParameter, param);
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

  g_list_foreach (codec->optional_params, (GFunc) free_optional_parameter,
        NULL);
  g_list_free (codec->optional_params);

  g_list_foreach (codec->ABI.ABI.feedback_params,
      (GFunc) free_feedback_parameter, NULL);
  g_list_free (codec->ABI.ABI.feedback_params);

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

  if (codec == NULL)
    return NULL;

  copy = fs_codec_new (codec->id, codec->encoding_name, codec->media_type,
      codec->clock_rate);

  copy->channels = codec->channels;
  copy->ABI.ABI.maxptime = codec->ABI.ABI.maxptime;
  copy->ABI.ABI.ptime = codec->ABI.ABI.ptime;
  copy->ABI.ABI.minimum_reporting_interval =
      codec->ABI.ABI.minimum_reporting_interval;

  copy->encoding_name = g_strdup (codec->encoding_name);

  for (lp = codec->optional_params; lp; lp = g_list_next (lp))
  {
    FsCodecParameter *param_copy;
    FsCodecParameter *param = lp->data;;

    param_copy = g_slice_new (FsCodecParameter);
    param_copy->name = g_strdup (param->name);
    param_copy->value = g_strdup (param->value);
    /* prepend then reverse the list for efficiency */
    copy->optional_params = g_list_prepend (copy->optional_params,
        param_copy);
  }
  copy->optional_params = g_list_reverse (copy->optional_params);

  for (lp = codec->ABI.ABI.feedback_params; lp; lp = g_list_next (lp))
  {
    FsFeedbackParameter *param_copy;
    FsFeedbackParameter *param = lp->data;;

    param_copy = g_slice_new (FsFeedbackParameter);
    param_copy->type = g_strdup (param->type);
    param_copy->subtype = g_strdup (param->subtype);
    param_copy->extra_params = g_strdup (param->extra_params);
    /* prepend then reverse the list for efficiency */
    copy->ABI.ABI.feedback_params = g_list_prepend (copy->ABI.ABI.feedback_params,
        param_copy);
  }
  copy->ABI.ABI.feedback_params =
      g_list_reverse (copy->ABI.ABI.feedback_params);

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
 *
 * [video/codec3]
 * wierd_param=42
 * feedback:nack/pli=1
 * feedback:tfrc=
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

  g_return_val_if_fail (filename, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  keyfile = g_key_file_new ();

  if (!g_key_file_load_from_file (keyfile, filename,
          G_KEY_FILE_NONE, error)) {
    goto out;
  }

  groups = g_key_file_get_groups (keyfile, &groups_count);

  if (!groups)
    goto out;

  for (i=0; i < groups_count && groups[i]; i++) {
    FsCodec *codec;
    gchar **keys = NULL;
    gsize keys_count;
    int j;
    gchar *encoding_name = NULL;
    gchar *next_tok = NULL;
    FsMediaType media_type;

    keys = g_key_file_get_keys (keyfile, groups[i], &keys_count, &gerror);

    if (!keys || gerror) {
      if (gerror)
        GST_WARNING ("Unable to read parameters for %s: %s\n",
            groups[i], gerror->message);
      else
        GST_WARNING ("Unknown errors while reading parameters for %s",
            groups[i]);

      g_clear_error (&gerror);

      goto next_codec;
    }

    next_tok = strchr (groups[i], '/');
    if (!next_tok)
    {
      GST_WARNING ("Invalid codec name: %s", groups[i]);
      goto next_codec;
    }

    if ((next_tok - groups[i]) == 5 /* strlen ("audio") */ &&
        !g_ascii_strncasecmp ("audio", groups[i], 5))
    {
      media_type = FS_MEDIA_TYPE_AUDIO;
    }
    else if ((next_tok - groups[i]) == 5 /* strlen ("video") */ &&
        !g_ascii_strncasecmp ("video", groups[i], 5))
    {
      media_type = FS_MEDIA_TYPE_VIDEO;
    }
    else
    {
      GST_WARNING ("Invalid media type in codec name name %s", groups[i]);
      goto next_codec;
    }

    encoding_name = next_tok + 1;

    next_tok = strchr (encoding_name, ':');

    if (encoding_name[0] == 0 || next_tok - encoding_name == 1)
      goto next_codec;

    if (next_tok)
      encoding_name = g_strndup (encoding_name,
          next_tok - encoding_name);
    else
      encoding_name = g_strdup (encoding_name);

    codec = fs_codec_new (FS_CODEC_ID_ANY, encoding_name, media_type, 0);

    g_free (encoding_name);

    for (j = 0; j < keys_count && keys[j]; j++) {
      if (!g_ascii_strcasecmp ("clock-rate", keys[j])) {
        codec->clock_rate = g_key_file_get_integer (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror) {
          codec->clock_rate = 0;
          goto keyerror;
        }

      } else if (!g_ascii_strcasecmp ("id", keys[j])) {
         codec->id = g_key_file_get_integer (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror) {
          codec->id = FS_CODEC_ID_ANY;
          goto keyerror;
        }

        if (codec->id < 0)
          codec->id = FS_CODEC_ID_DISABLE;

      } else if (!g_ascii_strcasecmp ("channels", keys[j])) {
         codec->channels = g_key_file_get_integer (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror) {
          codec->channels = 0;
          goto keyerror;
        }

      } else if (!g_ascii_strcasecmp ("maxptime", keys[j])) {
        codec->ABI.ABI.maxptime = g_key_file_get_integer (keyfile, groups[i],
            keys[j], &gerror);
        if (gerror) {
          codec->ABI.ABI.maxptime = 0;
          goto keyerror;
        }
      } else if (!g_ascii_strcasecmp ("ptime", keys[j])) {
        codec->ABI.ABI.ptime = g_key_file_get_integer (keyfile, groups[i],
            keys[j], &gerror);
        if (gerror) {
          codec->ABI.ABI.ptime = 0;
          goto keyerror;
        }
      } else if (!g_ascii_strcasecmp ("trr-int", keys[j])) {
        codec->ABI.ABI.minimum_reporting_interval =
            g_key_file_get_integer (keyfile, groups[i], keys[j], &gerror);
        if (gerror) {
          codec->ABI.ABI.minimum_reporting_interval = G_MAXUINT;
          goto keyerror;
        }
      } else if (g_str_has_prefix (keys[j], "feedback:")) {
        gchar *type = keys[j] + strlen ("feedback:");
        gchar *subtype = strchr (type, '/');
        gchar *extra_params;

        extra_params = g_key_file_get_string (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror)
          goto keyerror;

        /* Replace / with \0 and point to name (the next char) */
        if (subtype)
        {
          *subtype=0;
          subtype++;
        }
        else
        {
          subtype = "";
        }

        fs_codec_add_feedback_parameter (codec, type, subtype,
            extra_params);
        g_free (extra_params);
      } else {
        FsCodecParameter *param = g_slice_new (FsCodecParameter);

        param->name = g_strdup (keys[j]);
        param->value = g_key_file_get_string (keyfile, groups[i], keys[j],
            &gerror);
        if (gerror) {
          free_optional_parameter (param);
          goto keyerror;
        }

        if (!param->name || !param->value)
          free_optional_parameter (param);
        else
          codec->optional_params = g_list_append (codec->optional_params,
              param);
      }
      continue;
    keyerror:
      GST_WARNING ("Error reading key %s codec %s: %s", keys[j], groups[i],
          gerror->message);
      g_clear_error (&gerror);

    }

    codecs = g_list_append (codecs, codec);

  next_codec:
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

  if (codec->ABI.ABI.maxptime)
    g_string_append_printf (string, " maxptime=%u", codec->ABI.ABI.maxptime);

  if (codec->ABI.ABI.ptime)
    g_string_append_printf (string, " ptime=%u", codec->ABI.ABI.ptime);

  if (codec->ABI.ABI.minimum_reporting_interval != G_MAXUINT)
    g_string_append_printf (string, " trr-int=%u",
        codec->ABI.ABI.minimum_reporting_interval);

  for (item = codec->optional_params;
       item;
       item = g_list_next (item)) {
    FsCodecParameter *param = item->data;
    g_string_append_printf (string, " %s=%s", param->name, param->value);
  }

  for (item = codec->ABI.ABI.feedback_params;
       item;
       item = g_list_next (item)) {
    FsFeedbackParameter *param = item->data;
    g_string_append_printf (string, " %s/%s=%s", param->type, param->subtype,
        param->extra_params);
  }

  charstring = string->str;
  g_string_free (string, FALSE);

  return charstring;
}


static gboolean
compare_optional_params (const gpointer p1, const gpointer p2)
{
  const FsCodecParameter *param1 = p1;
  const FsCodecParameter *param2 = p2;

  if (!g_ascii_strcasecmp (param1->name, param2->name) &&
      !strcmp (param1->value, param2->value))
    return TRUE;
  else
    return FALSE;
}

static gboolean
compare_feedback_params (const gpointer p1, const gpointer p2)
{
  const FsFeedbackParameter *param1 = p1;
  const FsFeedbackParameter *param2 = p2;

  if (!g_ascii_strcasecmp (param1->subtype, param2->subtype) &&
      !g_ascii_strcasecmp (param1->type, param2->type) &&
      !g_strcmp0 (param1->extra_params, param2->extra_params))
    return TRUE;
  else
    return FALSE;
}

/*
 * Check if all of the elements of list1 are in list2
 * It compares GLists of X using the comparison function
 */
static gboolean
compare_lists (GList *list1, GList *list2,
    gboolean (*compare_params) (const gpointer p1, const gpointer p2))
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

      if (compare_params (param1, param2))
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
      codec1->ABI.ABI.maxptime != codec2->ABI.ABI.maxptime ||
      codec1->ABI.ABI.ptime != codec2->ABI.ABI.ptime ||
      codec1->ABI.ABI.minimum_reporting_interval !=
      codec2->ABI.ABI.minimum_reporting_interval ||
      codec1->encoding_name == NULL ||
      codec2->encoding_name == NULL ||
      g_ascii_strcasecmp (codec1->encoding_name, codec2->encoding_name))
    return FALSE;


  /* Is there a smarter way to compare to un-ordered linked lists
   * to make sure they contain exactly the same elements??
   */
  if (!compare_lists (codec1->optional_params, codec2->optional_params,
          compare_optional_params) ||
      !compare_lists (codec2->optional_params, codec1->optional_params,
          compare_optional_params))
    return FALSE;

  if (!compare_lists (codec1->ABI.ABI.feedback_params,
          codec2->ABI.ABI.feedback_params, compare_feedback_params) ||
      !compare_lists (codec2->ABI.ABI.feedback_params,
          codec1->ABI.ABI.feedback_params, compare_feedback_params))
    return FALSE;

  return TRUE;
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
 * @extra_params: The extra_params of the optional parameter
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
 * Removes an optional parameter from a codec.
 *
 * NULL param will do nothing.
 */

void
fs_codec_remove_optional_parameter (FsCodec *codec,
    FsCodecParameter *param)
{
  g_return_if_fail (codec);

  if (!param)
    return;

  free_optional_parameter (param);
  codec->optional_params = g_list_remove (codec->optional_params, param);
}

/**
 * fs_codec_get_optional_parameter:
 * @codec: a #FsCodec
 * @name: The name of the parameter to search for
 * @value: The value of the parameter to search for or %NULL for any value
 *
 * Finds the #FsCodecParameter in the #FsCodec that has the requested name
 * and, if not %NULL, the requested value
 *
 * Returns: the #FsCodecParameter from the #FsCodec or %NULL
 */

FsCodecParameter *
fs_codec_get_optional_parameter (FsCodec *codec, const gchar *name,
    const gchar *value)
{
  GList *item = NULL;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  for (item = g_list_first (codec->optional_params);
       item;
       item = g_list_next (item))
  {
    FsCodecParameter *param = item->data;
    if (!g_ascii_strcasecmp (param->name, name) &&
        (value == NULL || !g_ascii_strcasecmp (param->value, value)))
      return param;
  }

  return NULL;
}

/**
 * fs_codec_add_feedback_parameter:
 * @codec: The #FsCodec to add the parameter to
 * @type: The type of the feedback parameter
 * @subtype: The subtype of the feedback parameter
 * @extra_params: The extra_params of the feeback parameter
 *
 * This function adds an new feedback parameter to a #FsCodec
 */

void
fs_codec_add_feedback_parameter (FsCodec *codec, const gchar *type,
    const gchar *subtype, const gchar *extra_params)
{
  FsFeedbackParameter *param;

  g_return_if_fail (type != NULL);
  g_return_if_fail (subtype != NULL);
  g_return_if_fail (extra_params != NULL);

  param = g_slice_new (FsFeedbackParameter);

  param->type = g_strdup (type);
  param->subtype = g_strdup (subtype);
  param->extra_params = g_strdup (extra_params);

  codec->ABI.ABI.feedback_params =
      g_list_append (codec->ABI.ABI.feedback_params, param);
}


/**
 * fs_codec_get_feedback_parameter:
 * @codec: a #FsCodec
 * @type: The subtype of the parameter to search for or %NULL for any type
 * @subtype: The subtype of the parameter to search for or %NULL for any subtype
 * @extra_params: The extra_params of the parameter to search for or %NULL for
 *   any extra_params
 *
 * Finds the #FsFeedbackParameter in the #FsCodec that has the requested
 * subtype, type and extra_params. One of which must be non-NULL;
 *
 * Returns: the #FsFeedbackParameter from the #FsCodec or %NULL
 */

FsFeedbackParameter *
fs_codec_get_feedback_parameter (FsCodec *codec,
    const gchar *type, const gchar *subtype, const gchar *extra_params)
{
  GList *item = NULL;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (type != NULL || subtype != NULL, NULL);

  for (item = g_list_first (codec->ABI.ABI.feedback_params);
       item;
       item = g_list_next (item))
  {
    FsFeedbackParameter *param = item->data;
    if (!g_ascii_strcasecmp (param->type, type) &&
        (subtype == NULL || !g_ascii_strcasecmp (param->subtype, subtype)) &&
        (extra_params == NULL || !g_ascii_strcasecmp (param->extra_params,
            extra_params)))
      return param;
  }

  return NULL;
}



/**
 * fs_codec_remove_optional_parameter:
 * @codec: a #FsCodec
 * @item: a pointer to the #GList element to remove that contains a
 * #FsFeedbackParameter
 *
 * Removes an optional parameter from a codec.
 *
 * NULL param will do nothing.
 */

void
fs_codec_remove_feedback_parameter (FsCodec *codec, GList *item)
{
  g_return_if_fail (codec);

  if (!item)
    return;

  free_feedback_parameter (item->data);
  codec->ABI.ABI.feedback_params =
      g_list_delete_link (codec->ABI.ABI.feedback_params, item);
}

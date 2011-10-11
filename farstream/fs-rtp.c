/*
 * Farstream - Farstream RTP specific types
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
 *
 * fs-rtp.c - Farstream RTP specific types
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

#include "fs-rtp.h"

#include <string.h>

typedef GList FsRtpHeaderExtensionGList;

G_DEFINE_BOXED_TYPE (FsRtpHeaderExtension, fs_rtp_header_extension,
    fs_rtp_header_extension_copy, fs_rtp_header_extension_destroy)
G_DEFINE_BOXED_TYPE (FsRtpHeaderExtensionGList, fs_rtp_header_extension_list,
    fs_rtp_header_extension_list_copy, fs_rtp_header_extension_list_destroy)


/**
 * fs_rtp_header_extension_new:
 * @id: The identifier of the RTP header extension
 * @direction: the direction in which this extension can be used
 * @uri: The URI that defines this extension
 *
 * Creates a new #FsRtpHeaderExtension
 *
 * Returns: a new #FsRtpHeaderExtension
 */

FsRtpHeaderExtension *
fs_rtp_header_extension_new (guint id, FsStreamDirection direction,
    const gchar *uri)
{
  FsRtpHeaderExtension *extension;

  extension = g_slice_new (FsRtpHeaderExtension);

  extension->id = id;
  extension->direction = direction;
  extension->uri = g_strdup (uri);

  return extension;
}

/**
 * fs_rtp_header_extension_copy:
 * @extension: The RTP header extension definition to copy
 *
 * Copies a #FsRtpHeaderExtension
 *
 * Returns: a new #FsRtpHeaderExtension
 */

FsRtpHeaderExtension *
fs_rtp_header_extension_copy (FsRtpHeaderExtension *extension)
{
  if (extension)
    return fs_rtp_header_extension_new (extension->id, extension->direction,
        extension->uri);
  else
    return NULL;
}

/**
 * fs_rtp_header_extension_are_equal:
 * @extension1: The first #FsRtpHeaderExtension
 * @extension2: The second #FsRtpHeaderExtension
 *
 * Compares two #FsRtpHeaderExtension structures
 *
 * Returns: %TRUE if they are identical, %FALSE otherwise
 */

gboolean
fs_rtp_header_extension_are_equal (FsRtpHeaderExtension *extension1,
    FsRtpHeaderExtension *extension2)
{
  if (extension1 == extension2)
    return TRUE;

  if (!extension2 || !extension2)
    return FALSE;

  if (extension1->id == extension2->id &&
      extension1->direction == extension2->direction &&
      (extension1->uri == extension2->uri ||
          (extension1->uri && extension2->uri &&
              !strcmp (extension1->uri, extension2->uri))))
    return TRUE;
  else
    return FALSE;
}

/**
 * fs_rtp_header_extension_destroy:
 * @extension: A RTP header extension to free
 *
 * Frees the passed #FsRtpHeaderExtension
 */

void
fs_rtp_header_extension_destroy (FsRtpHeaderExtension *extension)
{
  if (extension)
  {
    g_free (extension->uri);
    g_slice_free (FsRtpHeaderExtension, extension);
  }
}

/**
 * fs_rtp_header_extension_list_copy:
 * @extensions: a #GList of #FsRtpHeaderExtension
 *
 * Does a deep copy of a #GList of #FsRtpHeaderExtension
 *
 * Returns: (element-type FsRtpHeaderExtension) (transfer full): a new
 * #GList of #FsRtpHeaderExtension
 */

GList *
fs_rtp_header_extension_list_copy (GList *extensions)
{
  GQueue copy = G_QUEUE_INIT;
  const GList *lp;

  for (lp = extensions; lp; lp = g_list_next (lp)) {
    FsRtpHeaderExtension *ext = lp->data;

    g_queue_push_tail (&copy, fs_rtp_header_extension_copy (ext));
  }

  return copy.head;
}

/**
 * fs_rtp_header_extension_list_destroy:
 * @extensions: a #GList of #FsRtpHeaderExtension
 *
 * Frees the passed #GList of #FsRtpHeaderExtension
 */

void
fs_rtp_header_extension_list_destroy (GList *extensions)
{
  g_list_foreach (extensions, (GFunc) fs_rtp_header_extension_destroy, NULL);
  g_list_free (extensions);
}

#define RTP_HDREXT_PREFIX "rtp-hdrext:"
#define RTP_HDREXT_AUDIO_PREFIX "audio:"
#define RTP_HDREXT_VIDEO_PREFIX "video:"

/**
 * fs_rtp_header_extension_list_from_keyfile:
 * @filename: Name of the #GKeyFile to read the RTP Header Extensions from
 * @media_type: The media type for which to get header extensions
 * @error: location of a #GError, or NULL if no error occured
 *
 * Reads the content of a #GKeyFile of the following format into a
 * #GList of #FsRtpHeaderExtension structures.
 *
 * The groups have a format "rtp-hdrext:audio:XXX" or
 * "rtp-hdrext:video:XXX" where XXX is a unique string (per media type).
 *
 * The valid keys are:
 * <itemizedlist>
 *  <listitem>id: a int between in the 1-255 and 4096-4351 ranges</listitem>
 *  <listitem>uri: a URI describing the RTP Header Extension</listitem>
 *  <listitem>direction (optional): To only send or receive a RTP Header
 *      Extension, possible values are "send", "receive", "none" or "both".
 *      Defaults to "both"</listitem>
 * </itemizedlist>
 *
 * Example:
 * |[
 * [rtp-hdrext:audio:a]
 * id=1
 * uri=urn:ietf:params:rtp-hdrext:toffset
 *
 * [rtp-hdrext:audio:abc]
 * id=3
 * uri=urn:ietf:params:rtp-hdrext:ntp-64
 * direction=receive
 * ]|
 *
 * Returns: (element-type FsRtpHeaderExtension) (transfer full): a
 * #GList of #FsRtpHeaderExtension that must be freed with
 * fs_rtp_header_extension_list_destroy()
 */

GList *
fs_rtp_header_extension_list_from_keyfile (const gchar *filename,
    FsMediaType media_type,
    GError **error)
{
  GKeyFile *keyfile = NULL;
  GList *extensions = NULL;
  gchar **groups = NULL;
  gsize groups_count = 0;
  int i;

  g_return_val_if_fail (filename, NULL);
  g_return_val_if_fail (media_type <= FS_MEDIA_TYPE_LAST, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  keyfile = g_key_file_new ();

  if (!g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, error))
    goto out;

  groups = g_key_file_get_groups (keyfile, &groups_count);

  if (!groups)
    goto out;

  for (i=0; i < groups_count && groups[i]; i++)
  {
    FsStreamDirection direction = FS_DIRECTION_BOTH;
    gint id;
    gchar *uri;
    GError *gerror = NULL;
    gchar *str;

    if (g_ascii_strncasecmp (RTP_HDREXT_PREFIX, groups[i],
            strlen (RTP_HDREXT_PREFIX)))
      continue;

    if (!g_ascii_strncasecmp (RTP_HDREXT_AUDIO_PREFIX,
            groups[i] + strlen (RTP_HDREXT_PREFIX),
            strlen (RTP_HDREXT_AUDIO_PREFIX)))
    {
      if (media_type != FS_MEDIA_TYPE_AUDIO)
        continue;
    }
    else if (!g_ascii_strncasecmp (RTP_HDREXT_VIDEO_PREFIX,
            groups[i] + strlen (RTP_HDREXT_PREFIX),
            strlen (RTP_HDREXT_VIDEO_PREFIX)))
    {
      if (media_type != FS_MEDIA_TYPE_VIDEO)
        continue;
    }
    else
    {
      continue;
    }

    id = g_key_file_get_integer (keyfile, groups[i], "id", &gerror);
    if (gerror)
    {
      g_clear_error (&gerror);
      continue;
    }

    str = g_key_file_get_string (keyfile, groups[i], "direction", &gerror);
    if (gerror)
    {
      GQuark domain = gerror->domain;
      gint code = gerror->code;

      g_clear_error (&gerror);
      if (domain != G_KEY_FILE_ERROR || code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
        continue;
    }
    else
    {
      if (!g_ascii_strcasecmp (str, "none"))
        direction = FS_DIRECTION_NONE;
      else if (!g_ascii_strcasecmp (str, "send"))
        direction = FS_DIRECTION_SEND;
      else if (!g_ascii_strcasecmp (str, "recv") ||
          !g_ascii_strcasecmp (str, "receive"))
        direction = FS_DIRECTION_RECV;
      g_free (str);
    }

    uri = g_key_file_get_string (keyfile, groups[i], "uri", &gerror);
    if (gerror)
    {
      g_clear_error (&gerror);
      continue;
    }

    extensions = g_list_append (extensions, fs_rtp_header_extension_new (id,
            direction, uri));
    g_free (uri);
  }

  out:

  g_strfreev (groups);
  g_key_file_free (keyfile);

  return extensions;
}

/*
 * Farsight2 - Farsight RTP specific types
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
 *
 * fs-rtp.c - Farsight RTP specific types
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

typedef GList FsRtpHeaderExtensionGList;

G_DEFINE_BOXED_TYPE (FsRtpHeaderExtension, fs_rtp_header_extension,
    fs_rtp_header_extension_copy, fs_rtp_header_extension_destroy)
G_DEFINE_BOXED_TYPE (FsRtpHeaderExtensionGList, fs_rtp_header_extension_list,
    fs_rtp_header_extension_list_copy, fs_rtp_header_extension_list_destroy)


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

FsRtpHeaderExtension *
fs_rtp_header_extension_copy (FsRtpHeaderExtension *extension)
{
  if (extension)
    return fs_rtp_header_extension_new (extension->id, extension->direction,
        extension->uri);
  else
    return NULL;
}

void
fs_rtp_header_extension_destroy (FsRtpHeaderExtension *extension)
{
  if (extension)
  {
    g_free (extension->uri);
    g_slice_free (FsRtpHeaderExtension, extension);
  }
}

GList *
fs_rtp_header_extension_list_copy (GList *extensions)
{
  GList *copy = NULL;
  const GList *lp;
  FsRtpHeaderExtension *ext;

  for (lp = extensions; lp; lp = g_list_next (lp)) {
    ext = (FsRtpHeaderExtension *) lp->data;
    /* prepend then reverse the list for efficiency */
    copy = g_list_prepend (copy, fs_rtp_header_extension_copy (ext));
  }
  copy = g_list_reverse (copy);
  return copy;
}

void
fs_rtp_header_extension_list_destroy (GList *extensions)
{
  g_list_foreach (extensions, (GFunc) fs_rtp_header_extension_destroy, NULL);
  g_list_free (extensions);
}


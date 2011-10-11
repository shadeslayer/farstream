/*
 * Farstream - Farstream RTP specific types
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
 *
 * fs-rtp.h - Farstream RTP specific types
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

#ifndef __FS_RTP_H__
#define __FS_RTP_H__

#include <gst/gst.h>
#include <farstream/fs-stream.h>

G_BEGIN_DECLS

/**
 * FsRtpHeaderExtension:
 * @id: The identifier of the RTP header extension
 * @direction: the direction in which this extension can be used
 * @uri: The URI that defines this extension
 *
 * Defines a RTP header extension with its negotiated identifier, direction
 * and URI. They should only be created with fs_rtp_header_extension_new().
 */

typedef struct _FsRtpHeaderExtension {
  guint id;
  FsStreamDirection direction;
  gchar *uri;
} FsRtpHeaderExtension;

/**
 * FS_TYPE_RTP_HEADER_EXTENSION:
 *
 * Boxed type for #FsRtpHeaderExtension
 */

/**
 * FS_TYPE_RTP_HEADER_EXTENSION_LIST:
 *
 * Boxed type for a #GList of #FsRtpHeaderExtension
 */

#define FS_TYPE_RTP_HEADER_EXTENSION \
  fs_rtp_header_extension_get_type ()
#define FS_TYPE_RTP_HEADER_EXTENSION_LIST \
  fs_rtp_header_extension_list_get_type ()

GType fs_rtp_header_extension_get_type (void);
GType fs_rtp_header_extension_list_get_type (void);


FsRtpHeaderExtension *
fs_rtp_header_extension_new (guint id, FsStreamDirection direction,
    const gchar *uri);

FsRtpHeaderExtension *
fs_rtp_header_extension_copy (FsRtpHeaderExtension *extension);
void
fs_rtp_header_extension_destroy (FsRtpHeaderExtension *extension);

gboolean
fs_rtp_header_extension_are_equal (FsRtpHeaderExtension *extension1,
    FsRtpHeaderExtension *extension2);

GList *
fs_rtp_header_extension_list_copy (GList *extensions);
void
fs_rtp_header_extension_list_destroy (GList *extensions);

GList *
fs_rtp_header_extension_list_from_keyfile (const gchar *filename,
    FsMediaType media_type,
    GError **error);

/**
 * FS_RTP_HEADER_EXTENSION_FORMAT:
 *
 * A format that can be used in printf like format strings to format a
 * FsRtpHeaderExtension
 */

/**
 * FS_RTP_HEADER_EXTENSION_ARGS:
 * @hdrext: a #FsRtpHeaderExtension
 *
 * Formats the codec in args for FS_RTP_HEADER_EXTENSION_ARGS
 */

#define FS_RTP_HEADER_EXTENSION_FORMAT "%d: (%s) %s"
#define FS_RTP_HEADER_EXTENSION_ARGS(hdrext)                    \
  (hdrext)->id,                                                 \
    (hdrext)->direction == FS_DIRECTION_BOTH ? "both" :         \
      ((hdrext)->direction == FS_DIRECTION_RECV? "recv" :       \
          ((hdrext)->direction == FS_DIRECTION_SEND ? "send" : "none")), \
    (hdrext)->uri

G_END_DECLS

#endif /* __FS_RTP_H__ */

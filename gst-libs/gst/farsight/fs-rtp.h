/*
 * Farsight2 - Farsight RTP specific types
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
 *
 * fs-rtp.h - Farsight RTP specific types
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
#include <gst/farsight/fs-stream.h>

G_BEGIN_DECLS

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

GList *
fs_rtp_header_extension_list_copy (GList *extensions);
void
fs_rtp_header_extension_list_destroy (GList *extensions);

G_END_DECLS

#endif /* __FS_RTP_H__ */

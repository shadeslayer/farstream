/*
 * Farstream - Miscellaneous useful functions
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
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



#ifndef __FS_UTILS_H__
#define __FS_UTILS_H__

#include <gst/gst.h>

#include <farstream/fs-codec.h>

G_BEGIN_DECLS

GList *fs_utils_get_default_codec_preferences (GstElement *element);

GKeyFile *fs_utils_get_default_element_properties (GstElement *element);

void fs_utils_set_bitrate (GstElement *element, glong bitrate);

GList *fs_utils_get_default_rtp_header_extension_preferences (
  GstElement *element, FsMediaType media_type);

G_END_DECLS

#endif /* __FS_UTILS_H__ */

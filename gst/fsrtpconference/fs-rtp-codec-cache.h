/*
 * Farsight2 - Farsight RTP Discovered Codecs cache
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-codec-cache.c - A Farsight RTP Codec Caching gobject
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



#ifndef __FS_RTP_CODEC_CACHE_H__
#define __FS_RTP_CODEC_CACHE_H__

#include "fs-rtp-discover-codecs.h"

G_BEGIN_DECLS

GList *load_codecs_cache(FsMediaType media_type, GError **error);
gboolean save_codecs_cache(FsMediaType media_type, GList *codec_blueprints);


G_END_DECLS

#endif /* __FS_RTP_CODEC_CACHE_H__ */

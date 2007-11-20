/*
 * Farsight2 - Farsight RTP Discovered Codecs cache
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-codec-cache.c - A Farsight RTP Codec Caching gobject
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



#ifndef __FS_RTP_CODEC_CACHE_H__
#define __FS_RTP_CODEC_CACHE_H__

#include "fs-rtp-discover-codecs.h"

G_BEGIN_DECLS

gboolean load_codecs_cache(FsMediaType media_type);
void save_codecs_cache(FsMediaType media_type);


G_END_DECLS

#endif /* __FS_RTP_CODEC_CACHE_H__ */

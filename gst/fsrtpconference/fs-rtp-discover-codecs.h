/*
 * Farsight2 - Farsight RTP Discover Codecs
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-discover-codecs.h - A Farsight RTP Codec Discovery
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

#ifndef __FS_RTP_DISCOVER_CODECS_H__
#define __FS_RTP_DISCOVER_CODECS_H__

#include <gst/gst.h>

#include <gst/farsight/fs-codec.h>

G_BEGIN_DECLS

typedef struct _CodecBlueprint
{
  FsCodec *codec;
  GstCaps *media_caps;
  GstCaps *rtp_caps;
  /*
   * These are #GList of #GList of #GstElementFactory
   */
  GList *send_pipeline_factory;
  GList *receive_pipeline_factory;
} CodecBlueprint;

GList *fs_rtp_blueprints_get (FsMediaType media_type, GError **error);
void fs_rtp_blueprints_unref (FsMediaType media_type);


/*
 * Only exported for the caching stuff
 */

void codec_blueprint_destroy (CodecBlueprint *codec_blueprint);

G_END_DECLS

#endif /* __FS_RTP_DISCOVER_CODECS_H__ */

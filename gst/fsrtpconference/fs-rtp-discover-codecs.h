/*
 * Farsight2 - Farsight RTP Discover Codecs
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-discover-codecs.h - A Farsight RTP Codec Discovery gobject
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
  GList *send_pipeline_factory;
  GList *receive_pipeline_factory;
  gboolean has_sink;
  gboolean has_src;

  gint send_has_unique;
  gint receive_has_unique;
  GstElement *send_unique_bin;
  GstElement *receive_unique_bin;
} CodecBlueprint;

gboolean load_codecs (FsMediaType media_type, GError **error);
void unload_codecs (FsMediaType media_type);

void codec_blueprint_destroy (CodecBlueprint *codec_blueprint);



G_END_DECLS

#endif /* __FS_RTP_DISCOVER_CODECS_H__ */

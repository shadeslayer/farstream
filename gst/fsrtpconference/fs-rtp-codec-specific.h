/*
 * fs-rtp-codec-specific.h - Per-codec SDP negotiation
 *
 * Farsight RTP/AVP/SAVP/AVPF Module
 * Copyright (C) 2007-2010 Collabora Ltd.
 * Copyright (C) 2007-2010 Nokia Corporation
 *   @author Olivier Crete <olivier.crete@collabora.co.uk>
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

#ifndef __FS_RTP_SPECIFIC_NEGO_H__
#define __FS_RTP_SPECIFIC_NEGO_H__

#include <glib.h>
#include <gst/gst.h>

#include <gst/farsight/fs-codec.h>

G_BEGIN_DECLS

typedef enum {
  FS_PARAM_TYPE_SEND = 1 << 0,
  FS_PARAM_TYPE_RECV = 1 << 1,
  FS_PARAM_TYPE_BOTH = FS_PARAM_TYPE_SEND | FS_PARAM_TYPE_RECV,
  FS_PARAM_TYPE_CONFIG = 1 << 2,
  FS_PARAM_TYPE_SEND_AVOID_NEGO = 1 << 3
} FsParamType;

FsCodec *
sdp_negotiate_codec (FsCodec *local_recv_codec, FsCodec *remote_codec);

gboolean
codec_needs_config (FsCodec *codec);

gboolean
codec_has_config_data_named (FsCodec *codec, const gchar *name);

FsCodec *
codec_copy_filtered (FsCodec *codec, FsParamType types);

G_END_DECLS

#endif

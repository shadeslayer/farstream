/*
 * fs-rtp-codec-specific.h - Per-codec SDP negotiation
 *
 * Farstream RTP/AVP/SAVP/AVPF Module
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

#include <farstream/fs-codec.h>

G_BEGIN_DECLS

/*
 * These are the basic types:
 *
 * @FS_PARAM_TYPE_SEND: The parameter define what we are allowed to send
 * @FS_PARAM_TYPE_RECV: The parameter defines what will be received,
 * @FS_PARAM_TYPE_CONFIG: The parameter is some configuration that must be
 *  fed to the decoder to be able to decode the stream
 * @FS_PARAM_TYPE_SEND_AVOID_NEGO: The parameter is not negotiated and can
 *  be different on both sides
 * @FS_PARAM_TYPE_MANDATORY: This parameter is mandatory and the codec's
 *  definition is not useful without it.
 */

typedef enum {
  FS_PARAM_TYPE_SEND = 1 << 0,
  FS_PARAM_TYPE_RECV = 1 << 1,
  FS_PARAM_TYPE_BOTH = FS_PARAM_TYPE_SEND | FS_PARAM_TYPE_RECV,
  FS_PARAM_TYPE_CONFIG = 1 << 2,
  FS_PARAM_TYPE_SEND_AVOID_NEGO = 1 << 3,
  FS_PARAM_TYPE_MANDATORY = 1 << 4,
  FS_PARAM_TYPE_ALL = FS_PARAM_TYPE_BOTH | FS_PARAM_TYPE_CONFIG
  | FS_PARAM_TYPE_SEND_AVOID_NEGO
} FsParamType;

FsCodec *
sdp_negotiate_codec (FsCodec *local_recv_codec, FsParamType local_paramtypes,
    FsCodec *remote_codec, FsParamType remote_paramtypes);

gboolean
codec_needs_config (FsCodec *codec);

gboolean
codec_has_config_data_named (FsCodec *codec, const gchar *name);

FsCodec *
codec_copy_filtered (FsCodec *codec, FsParamType types);

GList *
codecs_list_has_codec_config_changed (GList *old, GList *new);

G_END_DECLS

#endif

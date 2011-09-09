/*
 * Farsight2 - Farsight RTP Keyunit request manager
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
 *
 * fs-rtp-keyunit-request.h - A Farsight RTP Key Unit request manager
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

#ifndef __FS_RTP_KEYUNIT_MANAGER_H__
#define __FS_RTP_KEYUNIT_MANAGER_H__

#include <gst/gst.h>

#include <gst/farsight/fs-codec.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_KEYUNIT_MANAGER \
  (fs_rtp_keyunit_manager_get_type ())
#define FS_RTP_KEYUNIT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_KEYUNIT_MANAGER, \
      FsRtpKeyunitManager))
#define FS_RTP_KEYUNIT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_KEYUNIT_MANAGER, \
      FsRtpKeyunitManagerClass))
#define FS_IS_RTP_KEYUNIT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_KEYUNIT_MANAGER))
#define FS_IS_RTP_KEYUNIT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_KEYUNIT_MANAGER))
#define FS_RTP_KEYUNIT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_KEYUNIT_MANAGER, \
      FsRtpKeyunitManagerClass))
#define FS_RTP_KEYUNIT_MANAGER_CAST(obj) ((FsRtpKeyunitManager *) (obj))

typedef struct _FsRtpKeyunitManager FsRtpKeyunitManager;
typedef struct _FsRtpKeyunitManagerClass FsRtpKeyunitManagerClass;
typedef struct _FsRtpKeyunitManagerPrivate FsRtpKeyunitManagerPrivate;

GType fs_rtp_keyunit_manager_get_type (void);

FsRtpKeyunitManager *fs_rtp_keyunit_manager_new (
  GObject *rtpbin_internal_session);


void fs_rtp_keyunit_manager_codecbin_changed (FsRtpKeyunitManager *self,
    GstElement *codecbin, FsCodec *send_codec);

gboolean fs_rtp_keyunit_manager_has_key_request_feedback (FsCodec *send_codec);

G_END_DECLS

#endif /* __FS_RTP_KEYUNIT_MANAGER_H__ */

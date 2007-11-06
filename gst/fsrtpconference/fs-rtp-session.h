/*
 * Farsight2 - Farsight RTP Session
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-session.h - A Farsight RTP Session gobject
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

#ifndef __FS_RTP_SESSION_H__
#define __FS_RTP_SESSION_H__

#include <gst/gst.h>

#include <gst/farsight/fs-session.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_SESSION \
  (fs_rtp_session_get_type())
#define FS_RTP_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_SESSION, FsRtpSession))
#define FS_RTP_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_SESSION, FsRtpSessionClass))
#define FS_IS_RTP_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_SESSION))
#define FS_IS_RTP_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_SESSION))
#define FS_RTP_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_SESSION, FsRtpSessionClass))

typedef struct _FsRtpSession FsRtpSession;
typedef struct _FsRtpSessionClass FsRtpSessionClass;
typedef struct _FsRtpSessionPrivate FsRtpSessionPrivate;

struct _FsRtpSessionClass
{
  FsSession parent_class;
};

/**
 * FsSession:
 *
 */
struct _FsRtpSession
{
  FsSession parent;
  FsRtpSessionPrivate *priv;
};

GType fs_rtp_session_get_type (void);

FsRtpSession *fs_rtp_session_new (FsMediaType media_type, GstElement *gstrtpbin,
                                  guint id);

GstCaps *fs_rtp_session_request_pt_map (FsRtpSession *session, guint pt);

G_END_DECLS

#endif /* __FS_RTP_SESSION_H__ */

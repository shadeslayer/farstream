/*
 * Farsight2 - Farsight RTP Session
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-session.h - A Farsight RTP Session gobject
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

#ifndef __FS_RTP_SESSION_H__
#define __FS_RTP_SESSION_H__

#include <gst/gst.h>

#include <gst/farsight/fs-session.h>

#include "fs-rtp-conference.h"

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
#define FS_RTP_SESSION_CAST(obj) ((FsRtpSession *) (obj))

typedef struct _FsRtpSession FsRtpSession;
typedef struct _FsRtpSessionClass FsRtpSessionClass;
typedef struct _FsRtpSessionPrivate FsRtpSessionPrivate;

struct _FsRtpSessionClass
{
  FsSessionClass parent_class;
};

/**
 * FsRtpSession:
 *
 */
struct _FsRtpSession
{
  FsSession parent;

  guint id;

  /*< private >*/

  GStaticRecMutex mutex; /* Should only be accessed using the macros */

  FsRtpSessionPrivate *priv;
};

#define FS_RTP_SESSION_LOCK(session) \
  g_static_rec_mutex_lock (&FS_RTP_SESSION (session)->mutex)
#define FS_RTP_SESSION_UNLOCK(session) \
  g_static_rec_mutex_unlock (&FS_RTP_SESSION (session)->mutex)


GType fs_rtp_session_get_type (void);

FsRtpSession *fs_rtp_session_new (FsMediaType media_type,
                                  FsRtpConference *conference,
                                  guint id, GError **error);

GstCaps *fs_rtp_session_request_pt_map (FsRtpSession *session, guint pt);


void fs_rtp_session_new_recv_pad (FsRtpSession *session, GstPad *new_pad,
  guint32 ssrc, guint pt);

gboolean fs_rtp_session_negotiate_codecs (FsRtpSession *session,
    GList *remote_codecs,
    gpointer stream,
    GError **error);

GstElement *fs_rtp_session_new_recv_codec_bin_locked (FsRtpSession *session,
    guint32 ssrc,
    guint pt,
    FsCodec **out_codec,
    GError **error);

FsCodec *fs_rtp_session_get_recv_codec_for_pt (FsRtpSession *session,
    gint pt);

void fs_rtp_session_associate_ssrc_cname (FsRtpSession *session,
    guint32 ssrc,
    const gchar *cname);

void fs_rtp_session_bye_ssrc (FsRtpSession *session,
    guint32 ssrc);

G_END_DECLS

#endif /* __FS_RTP_SESSION_H__ */

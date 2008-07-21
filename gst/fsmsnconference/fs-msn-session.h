/*
 * Farsight2 - Farsight MSN Session
 *
 *  @author: Richard Spiers <richard.spiers@gmail.com>
 *
 * fs-msn-session.h - A Farsight Msn Session gobject
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

#ifndef __FS_MSN_SESSION_H__
#define __FS_MSN_SESSION_H__

#include <gst/gst.h>

#include <gst/farsight/fs-session.h>

#include "fs-msn-conference.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_MSN_SESSION \
  (fs_msn_session_get_type ())
#define FS_MSN_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_MSN_SESSION, FsMsnSession))
#define FS_MSN_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_MSN_SESSION, FsMsnSessionClass))
#define FS_IS_MSN_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_MSN_SESSION))
#define FS_IS_MSN_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_MSN_SESSION))
#define FS_MSN_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_MSN_SESSION, FsMsnSessionClass))
#define FS_MSN_SESSION_CAST(obj) ((FsMsnSession *) (obj))

typedef struct _FsMsnSession FsMsnSession;
typedef struct _FsMsnSessionClass FsMsnSessionClass;
typedef struct _FsMsnSessionPrivate FsMsnSessionPrivate;

struct _FsMsnSessionClass
{
  FsSessionClass parent_class;
};

/**
 * FsMsnSession:
 *
 */
struct _FsMsnSession
{
  FsSession parent;

  guint id;

  /*< private >*/

  GStaticRecMutex mutex; /* Should only be accessed using the macros */

  FsMsnSessionPrivate *priv;
};

#define FS_MSN_SESSION_LOCK(session) \
  g_static_rec_mutex_lock (&FS_MSN_SESSION (session)->mutex)
#define FS_MSN_SESSION_UNLOCK(session) \
  g_static_rec_mutex_unlock (&FS_MSN_SESSION (session)->mutex)


GType fs_msn_session_get_type (void);

FsMsnSession *fs_msn_session_new (FsMediaType media_type,
                                  FsMsnConference *conference,
                                  guint id,GError **error);

void fs_msn_session_new_recv_pad (FsMsnSession *session, GstPad *new_pad,
  guint32 ssrc, guint pt);

G_END_DECLS

#endif /* __FS_MSN_SESSION_H__ */

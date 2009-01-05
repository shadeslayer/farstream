/*
 * Farsight2 - Farsight MSN Stream
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * fs-msn-connection.h - An MSN Connection class
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

#ifndef __FS_MSN_CONNECTION_H__
#define __FS_MSN_CONNECTION_H__

#include "fs-msn-participant.h"
#include "fs-msn-session.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_MSN_CONNECTION \
  (fs_msn_connection_get_type ())
#define FS_MSN_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_MSN_CONNECTION, FsMsnConnection))
#define FS_MSN_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_MSN_CONNECTION, FsMsnConnectionClass))
#define FS_IS_MSN_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_MSN_CONNECTION))
#define FS_IS_MSN_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_MSN_CONNECTION))
#define FS_MSN_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_MSN_CONNECTION, FsMsnConnectionClass))
#define FS_MSN_CONNECTION_CAST(obj) ((FsMsnConnection*) (obj))

typedef struct _FsMsnConnection FsMsnConnection;
typedef struct _FsMsnConnectionClass FsMsnConnectionClass;
typedef struct _FsMsnConnectionPrivate FsMsnConnectionPrivate;


struct _FsMsnConnectionClass
{
  GObjectClass parent_class;
};

/**
 * FsMsnConnection:
 *
 */
struct _FsMsnConnection
{
  GObject parent;

  gchar *local_recipient_id;
  gchar *remote_recipient_id;
  gint session_id;
  gint initial_port;

  GThread *polling_thread;
  GstClockTime poll_timeout;
  GstPoll *poll;
  GArray *pollfds;

  gboolean disposed;
};

GType fs_msn_connection_get_type (void);

FsMsnConnection *fs_msn_connection_new (guint session_id, guint initial_port);

gboolean fs_msn_connection_gather_local_candidates (FsMsnConnection *connection);

gboolean fs_msn_connection_set_remote_candidates (FsMsnConnection *connection,
    GList *candidates, GError **error);

G_END_DECLS

#endif /* __FS_MSN_CONNECTION_H__ */

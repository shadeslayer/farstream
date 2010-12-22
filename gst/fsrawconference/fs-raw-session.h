/*
 * Farsight2 - Farsight Raw Session
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007,2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Mike Ruprecht <mike.ruprecht@collabora.co.uk>
 *
 * fs-raw-session.h - A Farsight Raw Session gobject
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

#ifndef __FS_RAW_SESSION_H__
#define __FS_RAW_SESSION_H__

#include <gst/gst.h>

#include <gst/farsight/fs-session.h>

#include "fs-raw-conference.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RAW_SESSION \
  (fs_raw_session_get_type ())
#define FS_RAW_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RAW_SESSION, FsRawSession))
#define FS_RAW_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RAW_SESSION, FsRawSessionClass))
#define FS_IS_RAW_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RAW_SESSION))
#define FS_IS_RAW_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RAW_SESSION))
#define FS_RAW_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RAW_SESSION, FsRawSessionClass))
#define FS_RAW_SESSION_CAST(obj) ((FsRawSession *) (obj))

typedef struct _FsRawSession FsRawSession;
typedef struct _FsRawSessionClass FsRawSessionClass;
typedef struct _FsRawSessionPrivate FsRawSessionPrivate;

struct _FsRawSessionClass
  {
    FsSessionClass parent_class;
  };

/**
 * FsRawSession:
 *
 */
struct _FsRawSession
{
  FsSession parent;

  /* Protected by the conf lock */
  GstElement *valve;

  /*< private >*/

  /* This ID can be accessed by the streams for this session */
  guint id;

  FsRawSessionPrivate *priv;
};


GType fs_raw_session_get_type (void);

FsRawSession *fs_raw_session_new (FsMediaType media_type,
    FsRawConference *conference,
    guint id,
    GError **error);

G_END_DECLS

#endif /* __FS_RAW_SESSION_H__ */

/*
 * Farsight2 - Farsight libnice Transmitter thread object
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-nice-thread.h - A Farsight libnice transmitter thread object
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

#ifndef __FS_NICE_THREAD_H__
#define __FS_NICE_THREAD_H__

#include <glib-object.h>
#include <gst-libs/gst/farsight/fs-plugin.h>


G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_NICE_THREAD \
  (fs_nice_thread_get_type ())
#define FS_NICE_THREAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_NICE_THREAD, \
    FsNiceThread))
#define FS_NICE_THREAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_NICE_THREAD, \
    FsNiceThreadClass))
#define FS_IS_NICE_THREAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_NICE_THREAD))
#define FS_IS_NICE_THREAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_NICE_THREAD))
#define FS_NICE_THREAD_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_NICE_THREAD, \
    FsNiceThreadClass))
#define FS_NICE_THREAD_CAST(obj) ((FsNiceThread *) (obj))

typedef struct _FsNiceThread FsNiceThread;
typedef struct _FsNiceThreadClass FsNiceThreadClass;
typedef struct _FsNiceThreadPrivate FsNiceThreadPrivate;

/**
 * FsNiceThreadClass:
 * @parent_class: Our parent
 *
 * The class structure
 */

struct _FsNiceThreadClass
{
  GObjectClass parent_class;
};

/**
 * FsNiceThread:
 *
 * All members are private, access them using methods and properties
 */
struct _FsNiceThread
{
  GObject parent;

  /*< private >*/
  FsNiceThreadPrivate *priv;
};


GType fs_nice_thread_get_type (void);

GMainContext *
fs_nice_thread_get_context (FsNiceThread *self);

void fs_nice_thread_add_weak_object (FsNiceThread *self,
    GObject *object);

FsNiceThread *fs_nice_thread_new (GError **error);


GType
fs_nice_thread_register_type (FsPlugin *module);

G_END_DECLS

#endif /* __FS_NICE_THREAD_H__ */

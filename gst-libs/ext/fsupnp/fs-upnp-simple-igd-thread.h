/*
 * Farsight2 - Farsight UPnP IGD abstraction
 *
 * Copyright 2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2008 Nokia Corp.
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

#ifndef __FS_UPNP_SIMPLE_IGD_THREAD_H__
#define __FS_UPNP_SIMPLE_IGD_THREAD_H__

#include <ext/fsupnp/fs-upnp-simple-igd.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_UPNP_SIMPLE_IGD_THREAD       \
  (fs_upnp_simple_igd_thread_get_type ())
#define FS_UPNP_SIMPLE_IGD_THREAD(obj)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_UPNP_SIMPLE_IGD_THREAD, \
      FsUpnpSimpleIgdThread))
#define FS_UPNP_SIMPLE_IGD_THREAD_CLASS(klass)                       \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_UPNP_SIMPLE_IGD_THREAD,  \
      FsUpnpSimpleIgdThreadClass))
#define FS_IS_UPNP_SIMPLE_IGD_THREAD(obj)                            \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_UPNP_SIMPLE_IGD_THREAD))
#define FS_IS_UPNP_SIMPLE_IGD_THREAD_CLASS(klass)                    \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_UPNP_SIMPLE_IGD_THREAD))
#define FS_UPNP_SIMPLE_IGD_THREAD_GET_CLASS(obj)                     \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_UPNP_SIMPLE_IGD_THREAD, \
      FsUpnpSimpleIgdThreadClass))
#define FS_UPNP_SIMPLE_IGD_THREAD_CAST(obj)                          \
  ((FsUpnpSimpleIgdThread *) (obj))

typedef struct _FsUpnpSimpleIgdThread FsUpnpSimpleIgdThread;
typedef struct _FsUpnpSimpleIgdThreadClass FsUpnpSimpleIgdThreadClass;
typedef struct _FsUpnpSimpleIgdThreadPrivate FsUpnpSimpleIgdThreadPrivate;

/**
 * FsUpnpSimpleIgdThreadClass:
 * @parent_class: Our parent
 *
 * The Raw UDP component transmitter class
 */

struct _FsUpnpSimpleIgdThreadClass
{
  FsUpnpSimpleIgdClass parent_class;

  /*virtual functions */
  /*< private >*/
};

/**
 * FsUpnpSimpleIgdThread:
 *
 * All members are private, access them using methods and properties
 */
struct _FsUpnpSimpleIgdThread
{
  FsUpnpSimpleIgd parent;

  /*< private >*/
  FsUpnpSimpleIgdThreadPrivate *priv;
};

GType fs_upnp_simple_igd_thread_get_type (void);

FsUpnpSimpleIgdThread *
fs_upnp_simple_igd_thread_new (void);

G_END_DECLS

#endif /* __FS_UPNP_SIMPLE_IGD_THREAD_H__ */

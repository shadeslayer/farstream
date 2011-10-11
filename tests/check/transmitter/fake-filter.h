/*
 * Farstream Voice+Video library
 *
 *  Copyright 2008 Collabora Ltd,
 *  Copyright 2008 Nokia Corporation
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifndef __FS_FAKE_FILTER_H__
#define __FS_FAKE_FILTER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define FS_TYPE_FAKE_FILTER \
  (fs_fake_filter_get_type())
#define FS_FAKE_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
  FS_TYPE_FAKE_FILTER,FsFakeFilter))
#define FS_FAKE_FILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
  FS_TYPE_FAKE_FILTER,FsFakeFilterClass))
#define FS_IS_FAKE_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),FS_TYPE_FAKE_FILTER))
#define FS_IS_FAKE_FILTER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),FS_TYPE_FAKE_FILTER))

typedef struct _FsFakeFilter FsFakeFilter;
typedef struct _FsFakeFilterClass FsFakeFilterClass;
typedef struct _FsFakeFilterPrivate FsFakeFilterPrivate;

struct _FsFakeFilter
{
  GstBaseTransform parent;
};

struct _FsFakeFilterClass
{
  GstBaseTransformClass parent_class;
};

GType fs_fake_filter_get_type (void);

gboolean fs_fake_filter_register (void);


G_END_DECLS

#endif /* __FS_FAKE_FILTER_H__ */

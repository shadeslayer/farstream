/*
 * Farsight Voice+Video library
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

#ifndef __GST_VIDEOANYRATE_H__
#define __GST_VIDEOANYRATE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_VIDEOANYRATE \
  (gst_videoanyrate_get_type())
#define GST_VIDEOANYRATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
  GST_TYPE_VIDEOANYRATE,GstVideoanyrate))
#define GST_VIDEOANYRATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
  GST_TYPE_VIDEOANYRATE,GstVideoanyrateClass))
#define GST_IS_VIDEOANYRATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOANYRATE))
#define GST_IS_VIDEOANYRATE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOANYRATE))

typedef struct _GstVideoanyrate GstVideoanyrate;
typedef struct _GstVideoanyrateClass GstVideoanyrateClass;
typedef struct _GstVideoanyratePrivate GstVideoanyratePrivate;

struct _GstVideoanyrate
{
  GstBaseTransform parent;
};

struct _GstVideoanyrateClass
{
  GstBaseTransformClass parent_class;
};

GType gst_videoanyrate_get_type (void);

G_END_DECLS

#endif /* __GST_VIDEOANYRATE_H__ */

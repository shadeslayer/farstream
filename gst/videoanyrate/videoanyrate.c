/*
 * Farsight Voice+Video library
 *
 *  Copyright 2007 Collabora Ltd, 
 *  Copyright 2007 Nokia Corporation
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "videoanyrate.h"

#include <string.h>

GST_DEBUG_CATEGORY (videoanyrate_debug);
#define GST_CAT_DEFAULT (videoanyrate_debug)

/* elementfactory information */
static const GstElementDetails gst_videoanyrate_details =
GST_ELEMENT_DETAILS (
  "Videoanyrate element",
  "Filter",
  "This element removes the framerate from caps",
  "Olivier Crete <olivier.crete@collabora.co.uk>");


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* Videoanyrate signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};


static GstCaps *
gst_videoanyrate_transform_caps (GstBaseTransform *trans,
    GstPadDirection direction,
    GstCaps *caps);


static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT
    (videoanyrate_debug, "fsvideoanyrate", 0, "fsvideoanyrate");
}

GST_BOILERPLATE_FULL (GstVideoanyrate, gst_videoanyrate, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _do_init);

static void
gst_videoanyrate_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details (element_class, &gst_videoanyrate_details);
}

static void
gst_videoanyrate_class_init (GstVideoanyrateClass *klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  gstbasetransform_class->transform_caps =
    GST_DEBUG_FUNCPTR(gst_videoanyrate_transform_caps);
}

static void
gst_videoanyrate_init (GstVideoanyrate *videoanyrate,
    GstVideoanyrateClass *klass)
{
}

static GstCaps *
gst_videoanyrate_transform_caps (GstBaseTransform *trans,
    GstPadDirection direction,
    GstCaps *caps)
{
  GstStructure *s, *s2;
  GstCaps *mycaps = gst_caps_make_writable (caps);

  if (gst_caps_get_size (mycaps) == 0)
    return gst_caps_ref (mycaps);

  s = gst_caps_get_structure (mycaps, 0);

  if (gst_structure_has_field (s, "framerate"))
  {
    s2 = gst_structure_copy (s);
    gst_structure_remove_field (s2, "framerate");
    gst_caps_append_structure (mycaps, s2);
  }

  return gst_caps_ref (mycaps);
}

gboolean
gst_videoanyrate_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "fsvideoanyrate",
      GST_RANK_MARGINAL, GST_TYPE_VIDEOANYRATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "fsvideoanyrate",
    "Videoanyrate",
    gst_videoanyrate_plugin_init, VERSION, "LGPL", "Farsight",
    "http://farsight.sf.net")

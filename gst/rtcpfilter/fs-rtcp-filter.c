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

/**
 * SECTION:element-fsrtcpfilter
 * @short_description: Removes the framerate from video caps
 *
 * This element will remove the framerate from video caps, it is a poor man's
 * videorate for live pipelines.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtcp-filter.h"

#include <gst/rtp/gstrtcpbuffer.h>

GST_DEBUG_CATEGORY (rtcp_filter_debug);
#define GST_CAT_DEFAULT (rtcp_filter_debug)

/* elementfactory information */
static const GstElementDetails fs_rtcp_filter_details =
GST_ELEMENT_DETAILS (
  "RTCP Filter element",
  "Filter",
  "This element removes unneeded parts of rtcp buffers",
  "Olivier Crete <olivier.crete@collabora.co.uk>");


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp"));

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};


GstFlowReturn
fs_rtcp_filter_transform_ip (GstBaseTransform *transform, GstBuffer *buf);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT
    (rtcp_filter_debug, "fsrtcpfilter", 0, "fsrtcpfilter");
}

GST_BOILERPLATE_FULL (FsRtcpFilter, fs_rtcp_filter, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _do_init);

static void
fs_rtcp_filter_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details (element_class, &fs_rtcp_filter_details);
}

static void
fs_rtcp_filter_class_init (FsRtcpFilterClass *klass)
{
  GstBaseTransformClass *gstbasetransform_class;

  gstbasetransform_class = (GstBaseTransformClass *) klass;

  gstbasetransform_class->transform_ip = fs_rtcp_filter_transform_ip;
}

static void
fs_rtcp_filter_init (FsRtcpFilter *rtcpfilter,
    FsRtcpFilterClass *klass)
{
}

GstFlowReturn
fs_rtcp_filter_transform_ip (GstBaseTransform *transform, GstBuffer *buf)
{
  if (!gst_rtcp_buffer_validate (buf))
  {
    GST_ERROR_OBJECT (transform, "Invalid RTCP buffer");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

gboolean
fs_rtcp_filter_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "fsrtcpfilter",
      GST_RANK_MARGINAL, FS_TYPE_RTCP_FILTER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "fsrtcpfilter",
    "RtcpFilter",
    fs_rtcp_filter_plugin_init, VERSION, "LGPL", "Farsight",
    "http://farsight.sf.net")

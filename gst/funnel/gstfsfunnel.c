/*
 * Farsight2 - Farsight Funnel element
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * gstfsfunnel.c: Simple Funnel element
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * SECTION:element-funnel
 * @short_description: N-to-1 simple funnel
 *
 * Takes packets from various input sinks into one output source
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstfsfunnel.h"

GST_DEBUG_CATEGORY_STATIC (fs_funnel_debug);
#define GST_CAT_DEFAULT fs_funnel_debug


static GstStaticPadTemplate funnel_sink_template =
  GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate funnel_src_template =
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);



#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (fs_funnel_debug, "fsfunnel", 0, "fsfunnel element");

GST_BOILERPLATE_FULL (FsFunnel, fs_funnel, GstElement, GST_TYPE_ELEMENT,
  _do_init);



static GstPad *fs_funnel_request_new_pad (GstElement * element,
  GstPadTemplate * templ, const gchar * name);
static void fs_funnel_release_pad (GstElement * element, GstPad * pad);
static GstFlowReturn fs_funnel_chain (GstPad * pad, GstBuffer * buffer);




static void
fs_funnel_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
      "Farsight Funnel pipe fitting",
      "Generic",
      "N-to-1 pipe fitting",
      "Olivier Crete <olivier.crete@collabora.co.uk>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_src_template));
}


static void
fs_funnel_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
fs_funnel_class_init (FsFunnelClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (fs_funnel_finalize);

  gstelement_class->request_new_pad =
    GST_DEBUG_FUNCPTR (fs_funnel_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (fs_funnel_release_pad);
}



static void
fs_funnel_init (FsFunnel * funnel, FsFunnelClass * g_class)
{
  funnel->srcpad = gst_pad_new_from_static_template (&funnel_src_template,
    "src");

  gst_element_add_pad (GST_ELEMENT (funnel), funnel->srcpad);
}


static GstPad *
fs_funnel_request_new_pad (GstElement * element, GstPadTemplate * templ,
  const gchar * name)
{
  GstPad *sinkpad;
  FsFunnel *funnel = FS_FUNNEL (element);

  GST_DEBUG_OBJECT (funnel, "requesting pad");

  sinkpad = gst_pad_new_from_template (templ, name);

  //  gst_pad_set_setcaps_function ()

  gst_pad_set_chain_function (sinkpad, fs_funnel_chain);

  gst_pad_set_active (sinkpad, TRUE);

  gst_element_add_pad (element, sinkpad);

  return sinkpad;
}

static void
fs_funnel_release_pad (GstElement * element, GstPad * pad)
{
  FsFunnel *funnel = FS_FUNNEL (element);

  GST_DEBUG_OBJECT (funnel, "releasing pad");

  gst_pad_set_active (pad, FALSE);

  gst_element_remove_pad (GST_ELEMENT_CAST (funnel), pad);
}

static GstFlowReturn
fs_funnel_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  FsFunnel *funnel = FS_FUNNEL (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (funnel, "received buffer %p", buffer);

  res = gst_pad_chain (funnel->srcpad, buffer);

  GST_DEBUG_OBJECT (funnel, "handled buffer %s", gst_flow_get_name (res));

  gst_object_unref (funnel);

  return res;
}



static gboolean plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "fsfunnel",
                               GST_RANK_NONE, FS_TYPE_FUNNEL);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "fsfunnel",
  "Farsight Funnel plugin",
  plugin_init,
  VERSION,
  "LGPL",
  "Farsight",
  "http://farsight.freedesktop.org/"
)

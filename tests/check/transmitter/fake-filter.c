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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fake-filter.h"

GST_DEBUG_CATEGORY (fake_filter_debug);
#define GST_CAT_DEFAULT (fake_filter_debug)

/* elementfactory information */
static const GstElementDetails fs_fake_filter_details =
GST_ELEMENT_DETAILS (
  "Fake Filter element",
  "Filter",
  "This element ignores the sending property",
  "Olivier Crete <olivier.crete@collabora.co.uk>");


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SENDING
};

static void fs_fake_filter_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_fake_filter_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT
    (fake_filter_debug, "fsfakefilter", 0, "fsfakefilter");
}

GST_BOILERPLATE_FULL (FsFakeFilter, fs_fake_filter, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _do_init);

static void
fs_fake_filter_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details (element_class, &fs_fake_filter_details);
}

static void
fs_fake_filter_class_init (FsFakeFilterClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (fs_fake_filter_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (fs_fake_filter_get_property);

  g_object_class_install_property (gobject_class,
      PROP_SENDING,
      g_param_spec_boolean ("sending",
          "Sending RTP?",
          "If set to FALSE, it assumes that all RTP has been dropped",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
fs_fake_filter_init (FsFakeFilter *fakefilter,
    FsFakeFilterClass *klass)
{
}

static void
fs_fake_filter_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  switch (prop_id)
  {
    case PROP_SENDING:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
fs_fake_filter_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  switch (prop_id)
  {
    case PROP_SENDING:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


gboolean
fs_fake_filter_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "fsfakefilter",
      GST_RANK_MARGINAL, FS_TYPE_FAKE_FILTER);
}

gboolean
fs_fake_filter_register (void)
{
  return gst_plugin_register_static (
      GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      "fsfakefilter",
      "FakeFilter",
      fs_fake_filter_plugin_init,
      VERSION,
      "LGPL",
      "Farstream",
      "Farstream",
      "Farstream testing suite");
}

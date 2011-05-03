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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtp-bitrate-adapter.h"

/* This is a magical value that smarter people discovered */
#define  H264_MAX_PIXELS_PER_BIT 25

GST_DEBUG_CATEGORY_STATIC (fs_rtp_bitrate_adapter_debug);
#define GST_CAT_DEFAULT fs_rtp_bitrate_adapter_debug

static GstStaticPadTemplate fs_rtp_bitrate_adapter_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("video/x-raw-yuv,"
            "width = (int) [ 1, max ],"
            "height =  (int) [ 1, max ],"
            "framerate = (fraction) [ 1/max, max ];"
            "video/x-raw-rgb,"
            "width = (int) [ 1, max ],"
            "height =  (int) [ 1, max ],"
            "framerate = (fraction) [ 1/max, max ];"
            "video/x-raw-gray,"
            "width = (int) [ 1, max ],"
            "height =  (int) [ 1, max ],"
            "framerate = (fraction) [ 1/max, max ]"));

static GstStaticPadTemplate fs_rtp_bitrate_adapter_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("video/x-raw-yuv,"
            "width = (int) [ 1, max ],"
            "height =  (int) [ 1, max ],"
            "framerate = (fraction) [ 1/max, max ];"
            "video/x-raw-rgb,"
            "width = (int) [ 1, max ],"
            "height =  (int) [ 1, max ],"
            "framerate = (fraction) [ 1/max, max ];"
            "video/x-raw-gray,"
            "width = (int) [ 1, max ],"
            "height =  (int) [ 1, max ],"
            "framerate = (fraction) [ 1/max, max ]"));

enum
{
  PROP_0,
  PROP_RTP_CAPS,
  PROP_BITRATE
};


static void fs_rtp_bitrate_adapter_finalize (GObject *object);
static void fs_rtp_bitrate_adapter_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);


GST_BOILERPLATE (FsRtpBitrateAdapter, fs_rtp_bitrate_adapter, GstElement,
    GST_TYPE_ELEMENT);

static GstFlowReturn fs_rtp_bitrate_adapter_chain (GstPad *pad,
    GstBuffer *buffer);
static GstCaps *fs_rtp_bitrate_adapter_getcaps (GstPad *pad);
static GstFlowReturn fs_rtp_bitrate_adapter_bufferalloc (GstPad *pad,
    guint64 offset, guint size, GstCaps *caps, GstBuffer **buf);

static void
fs_rtp_bitrate_adapter_base_init (gpointer klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT
      (fs_rtp_bitrate_adapter_debug, "fsrtpbitrateadapter", 0,
          "fsrtpbitrateadapter element");

  gst_element_class_set_details_simple (gstelement_class,
      "Farsight RTP Video Bitrate adater",
      "Generic",
      "Filter that can modify the resolution and framerate based"
      " on the bitrate",
      "Olivier Crete <olivier.crete@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_rtp_bitrate_adapter_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_rtp_bitrate_adapter_src_template));
}


static void
fs_rtp_bitrate_adapter_class_init (FsRtpBitrateAdapterClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = fs_rtp_bitrate_adapter_set_property;
  gobject_class->finalize = fs_rtp_bitrate_adapter_finalize;

  g_object_class_install_property (gobject_class,
      PROP_RTP_CAPS,
      g_param_spec_pointer ("rtp-caps",
          "Current RTP Caps",
          "The RTP caps currently used",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_BITRATE,
      g_param_spec_uint ("bitrate",
          "Bitrate to adapt for",
          "The bitrate to adapt for (0 means no adaption)",
          0, G_MAXUINT, 0,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}

static void
fs_rtp_bitrate_adapter_init (FsRtpBitrateAdapter *self,
    FsRtpBitrateAdapterClass *klass)
{
  self->sinkpad = gst_pad_new_from_static_template (
    &fs_rtp_bitrate_adapter_sink_template, "sink");
  gst_pad_set_chain_function (self->sinkpad, fs_rtp_bitrate_adapter_chain);
  gst_pad_set_setcaps_function (self->sinkpad, gst_pad_proxy_setcaps);
  gst_pad_set_getcaps_function (self->sinkpad,
      fs_rtp_bitrate_adapter_getcaps);
  gst_pad_set_bufferalloc_function (self->sinkpad,
      fs_rtp_bitrate_adapter_bufferalloc);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (
    &fs_rtp_bitrate_adapter_src_template, "src");
  gst_pad_set_getcaps_function (self->srcpad,
      fs_rtp_bitrate_adapter_getcaps);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

static void
fs_rtp_bitrate_adapter_finalize (GObject *object)
{
  FsRtpBitrateAdapter *self = FS_RTP_BITRATE_ADAPTER (object);

  if (self->caps)
    gst_caps_unref (self->caps);
  if (self->last_caps)
    gst_caps_unref (self->last_caps);
  if (self->rtp_caps)
    gst_caps_unref (self->rtp_caps);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

struct Resolution {
  guint width;
  guint height;
};

static const struct Resolution one_on_one_resolutions[] =
{
  {1920, 1200},
  {1920, 1080},
//  {1600, 1200},
//  {1680, 1050},
  {1280, 800},
  {1280, 768},
  {1280, 720},
//  {1024, 768},
//  {800, 600},
  {854, 480},
  {800, 480},
  {640, 480},
  {320, 240},
  {160, 120},
  {128, 96},
  {1, 1}
};

static const struct Resolution twelve_on_eleven_resolutions[] =
{
  {1480, 1152},
  {704, 576},
  {352, 288},
  {176, 144},
  {1, 1}
};

static void
video_caps_add (GstCaps *caps, const gchar *type,
    guint max_framerate, guint min_width,
    guint max_width, guint min_height, guint max_height,
    guint par_n, guint par_d)
{
  GstStructure *s;

  s = gst_structure_new (type,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d,
      NULL);

  if (max_framerate == 1 || max_width == 0)
    gst_structure_set (s,
        "framerate", GST_TYPE_FRACTION, max_framerate, 1,  NULL);
  else
    gst_structure_set (s,
        "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, max_framerate, 1,
        NULL);

  if (max_width != 0 && min_width != max_width)
    gst_structure_set (s, "width", GST_TYPE_INT_RANGE, min_width, max_width,
        NULL);
  else
    gst_structure_set (s, "width", G_TYPE_INT, min_width, NULL);

  if (max_height != 0 && min_height != max_height)
    gst_structure_set (s, "height", GST_TYPE_INT_RANGE, min_height, max_height,
        NULL);
  else
    gst_structure_set (s, "height", G_TYPE_INT, min_height, NULL);


  gst_caps_append_structure (caps, s);
}

static void
add_one_resolution_inner (GstCaps *caps, GstCaps *caps_gray,
    const struct Resolution *resolutions, gint i,
    guint max_framerate, guint par_n, guint par_d)
{
  video_caps_add (caps, "video/x-raw-yuv", max_framerate,
      resolutions[i].width, 0,
      resolutions[i].height, 0,
      par_n, par_d);
  video_caps_add (caps, "video/x-raw-yuv", max_framerate,
      resolutions[i + 1].width,
      resolutions[i].width,
      resolutions[i + 1].height,
      resolutions[i].height,
      par_n, par_d);
  video_caps_add (caps, "video/x-raw-rgb", max_framerate,
      resolutions[i].width, 0,
      resolutions[i].height, 0,
      par_n, par_d);
  video_caps_add (caps, "video/x-raw-rgb", max_framerate,
      resolutions[i + 1].width,
      resolutions[i].width,
      resolutions[i + 1].height,
      resolutions[i].height,
      par_n, par_d);
  video_caps_add (caps_gray, "video/x-raw-gray", max_framerate,
      resolutions[i].width, 0,
      resolutions[i].height, 0,
      par_n, par_d);
  video_caps_add (caps_gray, "video/x-raw-gray", max_framerate,
      resolutions[i + 1].width,
      resolutions[i].width,
      resolutions[i + 1].height,
      resolutions[i].height,
      par_n, par_d);
}

static void
add_one_resolution (GstCaps *caps, GstCaps *caps_gray,
    GstCaps *lower_caps, GstCaps *lower_caps_gray,
    GstCaps *extra_low_caps, GstCaps *extra_low_caps_gray,
    const struct Resolution *resolutions, gint i,
    guint max_pixels_per_second,
    guint par_n, guint par_d)
{
  guint pixels_per_frame = one_on_one_resolutions[i].width *
      one_on_one_resolutions[i].height;
  guint max_framerate = max_pixels_per_second / pixels_per_frame;

  max_framerate = MIN (max_framerate, 35);

  if (max_framerate >= 30)
    add_one_resolution_inner (caps, caps_gray, resolutions, i, max_framerate,
        par_n, par_d);
  else if (max_framerate >= 10)
    add_one_resolution_inner (lower_caps, lower_caps_gray, resolutions, i,
        max_framerate, par_n, par_d);
  else if (max_framerate > 0)
    add_one_resolution_inner (extra_low_caps, extra_low_caps_gray,
        resolutions, i, max_framerate, par_n, par_d);
}


GstCaps *
caps_from_bitrate (guint bitrate)
{
  GstCaps *caps = gst_caps_new_empty ();
  GstCaps *caps_gray = gst_caps_new_empty ();
  GstCaps *lower_caps = gst_caps_new_empty ();
  GstCaps *lower_caps_gray = gst_caps_new_empty ();
  GstCaps *extra_low_caps = gst_caps_new_empty ();
  GstCaps *extra_low_caps_gray = gst_caps_new_empty ();
  guint max_pixels_per_second = bitrate * H264_MAX_PIXELS_PER_BIT;
  gint i;

  /* At least one FPS at a very low res */
  max_pixels_per_second = MAX (max_pixels_per_second, 128 * 96);

  for (i = 0; one_on_one_resolutions[i].width > 1; i++)
    add_one_resolution (caps, caps_gray, lower_caps, lower_caps_gray,
        extra_low_caps, extra_low_caps_gray,
        one_on_one_resolutions, i, max_pixels_per_second, 1, 1);

  for (i = 0; twelve_on_eleven_resolutions[i].width > 1; i++)
    add_one_resolution (caps, caps_gray, lower_caps, lower_caps_gray,
        extra_low_caps, extra_low_caps_gray,
        twelve_on_eleven_resolutions, i, max_pixels_per_second, 12, 11);

  gst_caps_append (caps, lower_caps);
  if (gst_caps_is_empty (caps))
  {
    gst_caps_append (caps, extra_low_caps);
  }
  else
  {
    gst_caps_unref (extra_low_caps);
    gst_caps_unref (extra_low_caps_gray);
    extra_low_caps_gray = NULL;
  }
  gst_caps_append (caps, caps_gray);
  gst_caps_append (caps, lower_caps_gray);
  if (extra_low_caps_gray)
    gst_caps_append (caps, extra_low_caps_gray);

  return caps;
}

static GstCaps *
fs_rtp_bitrate_adapter_get_suggested_caps (FsRtpBitrateAdapter *self)
{
  GstCaps *sink_allowed_caps;
  GstCaps *src_allowed_caps;
  GstCaps *allowed_caps;
  GstCaps *wanted_caps;
  GstCaps *caps = NULL;

  GST_OBJECT_LOCK (self);
  if (self->caps)
    caps = gst_caps_ref (self->caps);
  GST_OBJECT_UNLOCK (self);

  if (!caps)
    return NULL;

  sink_allowed_caps = gst_pad_get_allowed_caps (self->sinkpad);
  src_allowed_caps = gst_pad_get_allowed_caps (self->srcpad);

  allowed_caps = gst_caps_intersect (sink_allowed_caps, src_allowed_caps);
  gst_caps_unref (sink_allowed_caps);
  gst_caps_unref (src_allowed_caps);

  wanted_caps = gst_caps_intersect_full (caps, allowed_caps,
      GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (allowed_caps);
  gst_caps_unref (caps);

  gst_pad_fixate_caps (self->srcpad, wanted_caps);

  return wanted_caps;
}


static GstCaps *
fs_rtp_bitrate_adapter_getcaps (GstPad *pad)
{
  FsRtpBitrateAdapter *self = FS_RTP_BITRATE_ADAPTER (
    gst_pad_get_parent_element (pad));
  GstCaps *caps;
  GstPad *otherpad;
  GstCaps *peer_caps;

  if (!self)
    return gst_caps_new_empty ();

  if (pad == self->srcpad)
    otherpad = self->sinkpad;
  else
    otherpad = self->srcpad;

  peer_caps = gst_pad_peer_get_caps_reffed (otherpad);

  GST_OBJECT_LOCK (self);
  if (peer_caps)
  {
    if (self->caps)
    {
      caps = gst_caps_intersect_full (self->caps, peer_caps,
          GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (peer_caps);
    }
    else
      caps = peer_caps;
  }
  else
  {
    if (self->caps)
      caps = gst_caps_ref (self->caps);
    else
      caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }
  GST_OBJECT_UNLOCK (self);

  gst_object_unref (self);

  return caps;
}

static GstFlowReturn
fs_rtp_bitrate_adapter_chain (GstPad *pad, GstBuffer *buffer)
{
  FsRtpBitrateAdapter *self =  FS_RTP_BITRATE_ADAPTER (
    gst_pad_get_parent_element (pad));
  GstFlowReturn ret;

  if (!self)
    return GST_FLOW_NOT_LINKED;

  ret = gst_pad_push (self->srcpad, buffer);

  gst_object_unref (self);
  return ret;
}

static GstFlowReturn
fs_rtp_bitrate_adapter_bufferalloc (GstPad *pad,
    guint64 offset, guint size, GstCaps *caps, GstBuffer **buf)
{
  FsRtpBitrateAdapter *self = FS_RTP_BITRATE_ADAPTER (
    gst_pad_get_parent_element (pad));
  GstFlowReturn ret;

  if (self->new_suggestion)
  {
    GstCaps *suggested_caps = fs_rtp_bitrate_adapter_get_suggested_caps (self);

    self->new_suggestion = FALSE;

    if (gst_caps_is_equal_fixed (caps, suggested_caps))
      ret = gst_pad_alloc_buffer_and_set_caps (self->srcpad, offset, size,
          caps, buf);
    else
      ret = gst_pad_alloc_buffer_and_set_caps (self->srcpad, offset, size,
          suggested_caps, buf);

    gst_caps_unref (suggested_caps);
  }
  else
  {
    ret = gst_pad_alloc_buffer_and_set_caps (self->srcpad, offset, size, caps,
        buf);
  }

  gst_object_unref (self);
  return ret;
}

static void
fs_rtp_bitrate_adapter_updated (FsRtpBitrateAdapter *self)
{
  GstCaps *wanted_caps;
  guint bitrate;
  GstCaps *caps = NULL;
  GstCaps *negotiated_caps;

  GST_OBJECT_LOCK (self);
  bitrate = self->bitrate;
  if (self->caps)
    gst_caps_unref (self->caps);
  self->caps = NULL;

  if (!bitrate)
  {
    GST_OBJECT_UNLOCK (self);
    return;
  }
  self->caps = caps_from_bitrate (bitrate);
  caps = gst_caps_ref (self->caps);
  GST_OBJECT_UNLOCK (self);

  negotiated_caps = gst_pad_get_negotiated_caps (self->sinkpad);
  if (!negotiated_caps)
    goto out;

  wanted_caps = fs_rtp_bitrate_adapter_get_suggested_caps (self);

  GST_OBJECT_LOCK (self);
  if (self->caps == caps &&
      !gst_caps_is_equal_fixed (negotiated_caps, wanted_caps))
    self->new_suggestion = TRUE;
  GST_OBJECT_UNLOCK (self);

  gst_caps_unref (wanted_caps);
  gst_caps_unref (negotiated_caps);

out:
  gst_caps_unref (caps);
}


static void
fs_rtp_bitrate_adapter_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  FsRtpBitrateAdapter *self = FS_RTP_BITRATE_ADAPTER (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id)
  {
    case PROP_RTP_CAPS:
      if (self->rtp_caps)
        gst_caps_unref (self->rtp_caps);
      self->rtp_caps = g_value_get_pointer (value);
      if (self->rtp_caps)
        gst_caps_ref (self->rtp_caps);
      break;
    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);

  fs_rtp_bitrate_adapter_updated (self);
}

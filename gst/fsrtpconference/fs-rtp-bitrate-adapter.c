/*
 * Farsight Voice+Video library
 *
 *  Copyright 2011 Collabora Ltd,
 *  Copyright 2011 Nokia Corporation
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

#include <math.h>

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
  PROP_BITRATE,
  PROP_INTERVAL,
  PROP_CAPS,
};

#define PROP_INTERVAL_DEFAULT (10 * GST_SECOND)

static void fs_rtp_bitrate_adapter_finalize (GObject *object);
static void fs_rtp_bitrate_adapter_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_rtp_bitrate_adapter_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);


GST_BOILERPLATE (FsRtpBitrateAdapter, fs_rtp_bitrate_adapter, GstElement,
    GST_TYPE_ELEMENT);

static GstFlowReturn fs_rtp_bitrate_adapter_chain (GstPad *pad,
    GstBuffer *buffer);
static GstCaps *fs_rtp_bitrate_adapter_getcaps (GstPad *pad);

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

static GParamSpec *caps_pspec;

static void
fs_rtp_bitrate_adapter_class_init (FsRtpBitrateAdapterClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = fs_rtp_bitrate_adapter_get_property;
  gobject_class->set_property = fs_rtp_bitrate_adapter_set_property;
  gobject_class->finalize = fs_rtp_bitrate_adapter_finalize;

  g_object_class_install_property (gobject_class,
      PROP_BITRATE,
      g_param_spec_uint ("bitrate",
          "Bitrate to adapt for",
          "The bitrate to adapt for (0 means no adaption)",
          0, G_MAXUINT, 0,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

 g_object_class_install_property (gobject_class,
      PROP_INTERVAL,
      g_param_spec_uint64 ("interval",
          "Minimum interval before adaptation",
          "The minimum interval before adapting after a change",
          0, G_MAXUINT64, PROP_INTERVAL_DEFAULT,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

 caps_pspec = g_param_spec_pointer ("caps",
     "Current input caps",
     "The caps that getcaps on the sink pad would return",
     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
 g_object_class_install_property (gobject_class,
     PROP_CAPS, caps_pspec);
}

struct BitratePoint
{
  GstClockTime timestamp;
  guint bitrate;
};

static struct BitratePoint *
bitrate_point_new (GstClockTime timestamp, guint bitrate)
{
  struct BitratePoint *bp = g_slice_new (struct BitratePoint);

  bp->timestamp = timestamp;
  bp->bitrate = bitrate;

  return bp;
}

static void
bitrate_point_free (struct BitratePoint *bp)
{
  g_slice_free (struct BitratePoint, bp);
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
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (
    &fs_rtp_bitrate_adapter_src_template, "src");
  gst_pad_set_getcaps_function (self->srcpad,
      fs_rtp_bitrate_adapter_getcaps);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  g_queue_init (&self->bitrate_history);
  self->system_clock = gst_system_clock_obtain ();
  self->interval = PROP_INTERVAL_DEFAULT;
}

static void
fs_rtp_bitrate_adapter_finalize (GObject *object)
{
  FsRtpBitrateAdapter *self = FS_RTP_BITRATE_ADAPTER (object);

  if (self->caps)
    gst_caps_unref (self->caps);

  if (self->system_clock)
    gst_object_unref (self->system_clock);

  g_queue_foreach (&self->bitrate_history, (GFunc) bitrate_point_free, NULL);
  g_queue_clear(&self->bitrate_history);

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
  {1600, 1200},
  {1680, 1050},
  {1280, 800},
  {1280, 768},
  {1280, 720},
  {1024, 768},
  {800, 600},
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
    guint min_framerate, guint max_framerate, guint width, guint height,
    guint par_n, guint par_d)
{
  GstStructure *s;

  s = gst_structure_new (type,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d,
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      NULL);

  gst_structure_set (s,
      "framerate", GST_TYPE_FRACTION_RANGE, min_framerate, 1,
      max_framerate, 1, NULL);

  gst_caps_append_structure (caps, s);
}

static void
add_one_resolution_inner (GstCaps *caps, GstCaps *caps_gray,
    guint min_framerate, guint max_framerate, guint width, guint height,
    guint par_n, guint par_d)
{
  video_caps_add (caps, "video/x-raw-yuv", min_framerate, max_framerate,
      width, height, par_n, par_d);
  video_caps_add (caps, "video/x-raw-rgb", min_framerate, max_framerate,
      width, height, par_n, par_d);
  video_caps_add (caps_gray, "video/x-raw-gray", min_framerate, max_framerate,
      width, height, par_n, par_d);
}

static void
add_one_resolution (GstCaps *caps, GstCaps *caps_gray,
    GstCaps *lower_caps, GstCaps *lower_caps_gray,
    GstCaps *extra_low_caps, GstCaps *extra_low_caps_gray,
    guint max_pixels_per_second,
    guint width, guint height,
    guint par_n, guint par_d)
{
  guint pixels_per_frame = width * height;
  guint max_framerate = max_pixels_per_second / pixels_per_frame;

  /* 66 as the max framerate is a arbitrary number that I'm getting from
   * being 2/3 of 666 which is clearly evil
   */

  if (max_framerate >= 20)
    add_one_resolution_inner (caps, caps_gray,
        20, 66, width, height, par_n, par_d);
  else if (max_framerate >= 10)
    add_one_resolution_inner (lower_caps, lower_caps_gray,
        10, 66, width, height, par_n, par_d);
  else if (max_framerate > 0)
    add_one_resolution_inner (extra_low_caps, extra_low_caps_gray,
        1, 66, width, height, par_n, par_d);
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
        max_pixels_per_second,
        one_on_one_resolutions[i].width,
        one_on_one_resolutions[i].height, 1, 1);

  for (i = 0; twelve_on_eleven_resolutions[i].width > 1; i++)
    add_one_resolution (caps, caps_gray, lower_caps, lower_caps_gray,
        extra_low_caps, extra_low_caps_gray,
        twelve_on_eleven_resolutions[i].width,
        twelve_on_eleven_resolutions[i].height,
        max_pixels_per_second, 12, 11);

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
  GstCaps *allowed_caps;
  GstCaps *wanted_caps;
  GstCaps *caps = NULL;

  GST_OBJECT_LOCK (self);
  if (self->caps)
    caps = gst_caps_ref (self->caps);
  GST_OBJECT_UNLOCK (self);

  if (!caps)
    return NULL;

  allowed_caps = gst_pad_get_allowed_caps (self->sinkpad);

  if (!allowed_caps)
  {
    gst_caps_unref (caps);
    return NULL;
  }

  g_debug ("suggested_allowed: %p %s", allowed_caps,
      gst_caps_to_string (allowed_caps));

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
      caps = gst_caps_intersect_full (self->caps, peer_caps,
          GST_CAPS_INTERSECT_FIRST);
    else
      caps = gst_caps_intersect (peer_caps,
          gst_pad_get_pad_template_caps (pad));

      gst_caps_unref (peer_caps);
  }
  else
  {
    if (self->caps)
      caps = gst_caps_intersect (self->caps,
          gst_pad_get_pad_template_caps (pad));
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


static guint
fs_rtp_bitrate_adapter_get_bitrate_locked (FsRtpBitrateAdapter *self)
{
  gdouble mean = 0;
  guint count = 0;
  gdouble S = 0;
  GList *item;
  gdouble stddev;

  for (item = self->bitrate_history.head; item ;item = item->next) {
    struct BitratePoint *bp = item->data;
    gdouble delta;

    count++;
    delta = bp->bitrate - mean;
    mean = mean + delta/count;
    S = S + delta * (bp->bitrate - mean);
  }

  if (count == 0)
    return G_MAXUINT;

  g_assert (S >= 0);
  stddev = sqrt (S/count);

  if (mean > stddev)
    return (guint) (mean - stddev);
  else
    return G_MAXUINT;
}

static void
fs_rtp_bitrate_adapter_updated (FsRtpBitrateAdapter *self)
{
  GstCaps *wanted_caps;
  guint bitrate;
  GstCaps *negotiated_caps;

  GST_OBJECT_LOCK (self);
  bitrate = fs_rtp_bitrate_adapter_get_bitrate_locked (self);
  if (self->caps)
    gst_caps_unref (self->caps);
  self->caps = NULL;

  GST_DEBUG ("Computed average lower bitrate: %u", bitrate);
  if (bitrate == G_MAXUINT)
  {
    GST_OBJECT_UNLOCK (self);
    return;
  }
  self->caps = caps_from_bitrate (bitrate);
  GST_OBJECT_UNLOCK (self);

  negotiated_caps = gst_pad_get_negotiated_caps (self->sinkpad);
  if (!negotiated_caps)
    return;

  wanted_caps = fs_rtp_bitrate_adapter_get_suggested_caps (self);

  GST_DEBUG ("wanted: %s", gst_caps_to_string (wanted_caps));
  GST_DEBUG ("current: %s", gst_caps_to_string (negotiated_caps));

  if (!gst_caps_is_equal_fixed (negotiated_caps, wanted_caps))
    gst_element_post_message (GST_ELEMENT (self),
        gst_message_new_element (
          GST_OBJECT (self),
          gst_structure_new ("fs-rtp-bitrate-adapter-caps-changed", NULL)));

  gst_caps_unref (wanted_caps);
  gst_caps_unref (negotiated_caps);
}

static void
fs_rtp_bitrate_adapter_cleanup (FsRtpBitrateAdapter *self,
    GstClockTime now)
{
  for (;;)
  {
    struct BitratePoint *bp = g_queue_peek_head (&self->bitrate_history);

    if (bp && bp->timestamp < now - self->interval)
    {
      g_queue_pop_head (&self->bitrate_history);
      bitrate_point_free (bp);
    }
    else
    {
      break;
    }
  }
}

static gboolean
clock_callback (GstClock *clock, GstClockTime now, GstClockID clockid,
    gpointer user_data)
{
  FsRtpBitrateAdapter *self = user_data;

  GST_OBJECT_LOCK (self);
  if (self->clockid == clockid)
  {
    gst_clock_id_unref (self->clockid);
    self->clockid = NULL;
  }
  GST_OBJECT_UNLOCK (self);

  fs_rtp_bitrate_adapter_updated (self);

  return TRUE;
}

static gboolean
fs_rtp_bitrate_adapter_add_bitrate_locked (FsRtpBitrateAdapter *self,
    guint bitrate)
{
  GstClockTime now = gst_clock_get_time (self->system_clock);
  gboolean first = FALSE;

  g_queue_push_tail (&self->bitrate_history, bitrate_point_new (now, bitrate));

  first = (g_queue_get_length (&self->bitrate_history) == 1);

  fs_rtp_bitrate_adapter_cleanup (self, now);

  if (!self->clockid)
  {
    self->clockid = gst_clock_new_single_shot_id (self->system_clock,
        now + self->interval);
    gst_clock_id_wait_async_full (self->clockid,
        clock_callback, gst_object_ref (self), gst_object_unref);
  }

  return first;
}


static void
fs_rtp_bitrate_adapter_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  FsRtpBitrateAdapter *self = FS_RTP_BITRATE_ADAPTER (object);
  gboolean first = FALSE;

  GST_OBJECT_LOCK (self);
  switch (prop_id)
  {
    case PROP_BITRATE:
      first = fs_rtp_bitrate_adapter_add_bitrate_locked (self,
          g_value_get_uint (value));
      break;
    case PROP_INTERVAL:
      self->interval = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);

  if (first)
    fs_rtp_bitrate_adapter_updated (self);

}


static void
fs_rtp_bitrate_adapter_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  FsRtpBitrateAdapter *self = FS_RTP_BITRATE_ADAPTER (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id)
  {
    case PROP_CAPS:
      if (self->caps)
        g_value_set_pointer (value, gst_caps_ref (self->caps));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}


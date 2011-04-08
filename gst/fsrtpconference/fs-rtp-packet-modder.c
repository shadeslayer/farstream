/*
 * Farsight2 - Farsight RTP Packet modder
 *
 * Copyright 2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2010 Nokia Corp.
 *
 * fs-rtp-packet-modder.c - Filter to modify RTP packets
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fs-rtp-packet-modder.h"

GST_DEBUG_CATEGORY_STATIC (fs_rtp_packet_modder_debug);
#define GST_CAT_DEFAULT fs_rtp_packet_modder_debug

static GstStaticPadTemplate fs_rtp_packet_modder_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("application/x-rtp"));

static GstStaticPadTemplate fs_rtp_packet_modder_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("application/x-rtp"));

GST_BOILERPLATE (FsRtpPacketModder, fs_rtp_packet_modder, GstElement,
    GST_TYPE_ELEMENT);

static GstFlowReturn fs_rtp_packet_modder_chain (GstPad *pad,
    GstBuffer *buffer);
static GstCaps *fs_rtp_packet_modder_getcaps (GstPad *pad);
static GstFlowReturn fs_rtp_packet_modder_bufferalloc (GstPad *pad,
    guint64 offset, guint size, GstCaps *caps, GstBuffer **buf);
static gboolean fs_rtp_packet_modder_sink_event (GstPad *pad, GstEvent *event);
static gboolean fs_rtp_packet_modder_query (GstPad *pad, GstQuery *query);
static GstStateChangeReturn fs_rtp_packet_modder_change_state (
  GstElement *element, GstStateChange transition);



static void
fs_rtp_packet_modder_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT
      (fs_rtp_packet_modder_debug, "fsrtppacketmodder", 0,
          "fsrtppacketmodder element");

  gst_element_class_set_details_simple (gstelement_class,
      "Farsight RTP Packet modder",
      "Generic",
      "Filter that can modify RTP packets",
      "Olivier Crete <olivier.crete@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_rtp_packet_modder_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_rtp_packet_modder_src_template));
}



static void
fs_rtp_packet_modder_class_init (FsRtpPacketModderClass *klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = fs_rtp_packet_modder_change_state;
}

static void
fs_rtp_packet_modder_init (FsRtpPacketModder *self,
    FsRtpPacketModderClass *g_class)
{
  self->sinkpad = gst_pad_new_from_static_template (
    &fs_rtp_packet_modder_sink_template, "sink");
  gst_pad_set_chain_function (self->sinkpad, fs_rtp_packet_modder_chain);
  gst_pad_set_setcaps_function (self->sinkpad, gst_pad_proxy_setcaps);
  gst_pad_set_getcaps_function (self->sinkpad, fs_rtp_packet_modder_getcaps);
  gst_pad_set_bufferalloc_function (self->sinkpad,
      fs_rtp_packet_modder_bufferalloc);
  gst_pad_set_event_function (self->sinkpad, fs_rtp_packet_modder_sink_event);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (
    &fs_rtp_packet_modder_src_template, "src");
  gst_pad_set_getcaps_function (self->srcpad, fs_rtp_packet_modder_getcaps);
  gst_pad_set_query_function (self->srcpad, fs_rtp_packet_modder_query);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

FsRtpPacketModder *
fs_rtp_packet_modder_new (FsRtpPacketModderFunc func, gpointer user_data)
{
  FsRtpPacketModder *self;

  g_return_val_if_fail (func != NULL, NULL);

  self = g_object_new (FS_TYPE_RTP_PACKET_MODDER, NULL);

  self->func = func;
  self->user_data = user_data;

  return self;
}

static void
fs_rtp_packet_modder_sync_to_clock (FsRtpPacketModder *self,
  GstClockTime buffer_ts)
{
  GstClockTime running_time;
  GstClockTime sync_time;
  GstClockID id;
  GstClock *clock;
  GstClockReturn clockret;

  GST_OBJECT_LOCK (self);
  running_time =  gst_segment_to_running_time (&self->segment, GST_FORMAT_TIME,
     buffer_ts);
again:

  sync_time = running_time + GST_ELEMENT_CAST (self)->base_time +
      self->peer_latency;

  clock = GST_ELEMENT_CLOCK (self);
  if (!clock) {
    GST_OBJECT_UNLOCK (self);
    /* let's just push if there is no clock */
    GST_DEBUG_OBJECT (self, "No clock, push right away");
    return;
  }

  GST_DEBUG_OBJECT (self, "sync to running timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (running_time));

  id = self->clock_id = gst_clock_new_single_shot_id (clock, sync_time);
  self->unscheduled = FALSE;
  GST_OBJECT_UNLOCK (self);

  clockret = gst_clock_id_wait (id, NULL);

  GST_OBJECT_LOCK (self);
  gst_clock_id_unref (id);
  self->clock_id = NULL;

  if (clockret == GST_CLOCK_UNSCHEDULED && !self->unscheduled)
    goto again;
  GST_OBJECT_UNLOCK (self);
}

static GstFlowReturn
fs_rtp_packet_modder_chain (GstPad *pad, GstBuffer *buffer)
{
  FsRtpPacketModder *self = FS_RTP_PACKET_MODDER (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_ERROR;

  buffer = self->func (self, buffer, self->user_data);

  if (!buffer)
  {
    GST_LOG_OBJECT (self, "Got NULL from FsRtpPacketModderFunc");
    goto invalid;
  }

  fs_rtp_packet_modder_sync_to_clock (self, GST_BUFFER_TIMESTAMP (buffer));

  ret = gst_pad_push (self->srcpad, buffer);

invalid:

  gst_object_unref (self);

  return ret;
}


static GstCaps *
fs_rtp_packet_modder_getcaps (GstPad *pad)
{
  FsRtpPacketModder *self = FS_RTP_PACKET_MODDER (gst_pad_get_parent (pad));
  GstCaps *peercaps;
  GstCaps *caps;
  GstPad *otherpad = self->sinkpad == pad ? self->srcpad : self->sinkpad;

  peercaps = gst_pad_peer_get_caps_reffed (otherpad);

  if (peercaps)
  {
    caps = gst_caps_intersect (peercaps, gst_pad_get_pad_template_caps (pad));
    gst_caps_unref (peercaps);
  }
  else
  {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  gst_object_unref (self);
  return caps;
}

static GstFlowReturn
fs_rtp_packet_modder_bufferalloc (GstPad *pad, guint64 offset, guint size,
    GstCaps *caps, GstBuffer **buf)
{
  FsRtpPacketModder *self = FS_RTP_PACKET_MODDER (gst_pad_get_parent (pad));
  GstFlowReturn ret;

  ret = gst_pad_alloc_buffer (self->srcpad, offset, size, caps, buf);

  gst_object_unref (self);

  return ret;
}

static gboolean
fs_rtp_packet_modder_sink_event (GstPad *pad, GstEvent *event)
{
  FsRtpPacketModder *self = FS_RTP_PACKET_MODDER (gst_pad_get_parent (pad));
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* we need time for now */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG_OBJECT (self,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      /* now configure the values, we need these to time the release of the
       * buffers on the srcpad. */
      gst_segment_set_newsegment_full (&self->segment, update,
          rate, arate, format, start, stop, time);

      /* FIXME, push SEGMENT in the queue. Sorting order might be difficult. */
      ret = gst_pad_push_event (self->srcpad, event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      GST_OBJECT_LOCK (self);
      if (self->clock_id)
      {
        gst_clock_id_unschedule (self->clock_id);
        self->unscheduled = TRUE;
      }
      GST_OBJECT_UNLOCK (self);
      ret = gst_pad_push_event (self->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      ret = gst_pad_push_event (self->srcpad, event);
      gst_segment_init (&self->segment, GST_FORMAT_TIME);
      break;
    default:
      ret = gst_pad_push_event (self->srcpad, event);
      break;
  }

done:
  gst_object_unref (self);
  return ret;

newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (self, "received non TIME newsegment");
    ret = FALSE;
    gst_event_unref (event);
    goto done;
  }
}

static GstStateChangeReturn
fs_rtp_packet_modder_change_state (GstElement *element,
    GstStateChange transition)
{
  FsRtpPacketModder *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  self = FS_RTP_PACKET_MODDER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (self);
      /* reset negotiated values */
      self->peer_latency = 0;
      gst_segment_init (&self->segment, GST_FORMAT_TIME);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* we are a live element because we sync to the clock, which we can only
       * do in the PLAYING state */
      if (ret != GST_STATE_CHANGE_FAILURE)
        ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_OBJECT_LOCK (self);
      if (self->clock_id)
      {
        gst_clock_id_unschedule (self->clock_id);
        self->unscheduled = TRUE;
      }
      GST_OBJECT_UNLOCK (self);
      break;
   default:
      break;
  }

  return ret;
}


static gboolean
fs_rtp_packet_modder_query (GstPad *pad, GstQuery *query)
{
  FsRtpPacketModder *self = FS_RTP_PACKET_MODDER (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* We need to send the query upstream and add the returned latency to our
       * own */
      GstClockTime min_latency, max_latency;
      gboolean us_live;

      if ((res = gst_pad_peer_query (self->sinkpad, query))) {
        gst_query_parse_latency (query, &us_live, &min_latency, &max_latency);

        GST_DEBUG_OBJECT (self, "Peer latency: min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        /* store this so that we can safely sync on the peer buffers. */
        GST_OBJECT_LOCK (self);
        self->peer_latency = min_latency;
        if (self->clock_id)
          gst_clock_id_unschedule (self->clock_id);
        GST_OBJECT_UNLOCK (self);

        /* we add some latency but can buffer an infinite amount of time */

        GST_DEBUG_OBJECT (self, "Calculated total latency : min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        gst_query_set_latency (query, TRUE, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (self);

  return res;
}

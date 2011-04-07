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
static GstFlowReturn fs_rtp_packet_modder_chain_list (GstPad *pad,
    GstBufferList *bufferlist);
static GstCaps *fs_rtp_packet_modder_getcaps (GstPad *pad);
static GstFlowReturn fs_rtp_packet_modder_bufferalloc (GstPad *pad,
    guint64 offset, guint size, GstCaps *caps, GstBuffer **buf);



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
fs_rtp_packet_modder_class_init (FsRtpPacketModderClass * klass)
{
  //GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  //GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
}

static void
fs_rtp_packet_modder_init (FsRtpPacketModder * self,
    FsRtpPacketModderClass * g_class)
{
  self->sinkpad = gst_pad_new_from_static_template (
    &fs_rtp_packet_modder_sink_template, "sink");
  gst_pad_set_chain_function (self->sinkpad, fs_rtp_packet_modder_chain);
  gst_pad_set_chain_list_function (self->sinkpad,
      fs_rtp_packet_modder_chain_list);
  gst_pad_set_setcaps_function (self->sinkpad, gst_pad_proxy_setcaps);
  gst_pad_set_getcaps_function (self->sinkpad, fs_rtp_packet_modder_getcaps);
  gst_pad_set_bufferalloc_function (self->sinkpad,
      fs_rtp_packet_modder_bufferalloc);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (
    &fs_rtp_packet_modder_src_template, "src");
  gst_pad_set_getcaps_function (self->srcpad, fs_rtp_packet_modder_getcaps);
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

static GstFlowReturn
fs_rtp_packet_modder_chain_common (GstPad *pad, gpointer buf, gboolean is_list)
{
  FsRtpPacketModder *self = FS_RTP_PACKET_MODDER (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_ERROR;
  gpointer newbuf;

  newbuf = self->func (self, buf, self->user_data);

  if (!newbuf)
  {
    GST_LOG_OBJECT (self, "Got NULL from FsRtpPacketModderFunc");
    goto invalid;
  }

  if (newbuf != buf)
  {
    if (GST_IS_BUFFER_LIST (newbuf))
      is_list = TRUE;
    else if (GST_IS_BUFFER (newbuf))
      is_list = FALSE;
    else
    {
      GST_LOG_OBJECT (self, "Got non-buffer from FsRtpPacketModderFunc");
      goto invalid;
    }

    buf = newbuf;
  }

  if (is_list)
    ret = gst_pad_push_list (self->srcpad, buf);
  else
    ret = gst_pad_push (self->srcpad, buf);

invalid:

  gst_object_unref (self);

  return ret;
}

static GstFlowReturn
fs_rtp_packet_modder_chain (GstPad *pad, GstBuffer *buffer)
{
  return fs_rtp_packet_modder_chain_common (pad, buffer, FALSE);
}

static GstFlowReturn
fs_rtp_packet_modder_chain_list (GstPad *pad, GstBufferList *bufferlist)
{
  return fs_rtp_packet_modder_chain_common (pad, bufferlist, TRUE);
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

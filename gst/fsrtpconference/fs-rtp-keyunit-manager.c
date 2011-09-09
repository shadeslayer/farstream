/*
 * Farsight2 - Farsight RTP Keyunit request manager
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
 *
 * fs-rtp-keyunit-request.h - A Farsight RTP Key Unit request manager
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
#include "config.h"
#endif

#include "fs-rtp-keyunit-manager.h"

#include <string.h>

#include <gst/rtp/gstrtcpbuffer.h>


struct _FsRtpKeyunitManagerClass
{
  GstObjectClass parent_class;
};

struct _FsRtpKeyunitManager
{
  GstObject parent;

  GObject *rtpbin_internal_session;

  GstElement *codecbin;
  gulong rtcp_feedback_id;
};


G_DEFINE_TYPE (FsRtpKeyunitManager, fs_rtp_keyunit_manager, GST_TYPE_OBJECT);

static void fs_rtp_keyunit_manager_dispose (GObject *obj);

static void
fs_rtp_keyunit_manager_class_init (FsRtpKeyunitManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = fs_rtp_keyunit_manager_dispose;
}

static void
fs_rtp_keyunit_manager_init (FsRtpKeyunitManager *self)
{
}

static void
fs_rtp_keyunit_manager_dispose (GObject *obj)
{
  FsRtpKeyunitManager *self = FS_RTP_KEYUNIT_MANAGER (obj);

  GST_OBJECT_LOCK (self);

  if (self->rtcp_feedback_id)
    g_signal_handler_disconnect (self->rtpbin_internal_session,
        self->rtcp_feedback_id);
  self->rtcp_feedback_id = 0;

  if (self->rtpbin_internal_session)
    g_object_unref (self->rtpbin_internal_session);
  self->rtpbin_internal_session = NULL;

  if (self->codecbin)
    g_object_unref (self->codecbin);
  self->codecbin = NULL;

  GST_OBJECT_UNLOCK (self);

  G_OBJECT_CLASS (fs_rtp_keyunit_manager_parent_class)->dispose (obj);
}

FsRtpKeyunitManager *
fs_rtp_keyunit_manager_new (GObject *rtpbin_internal_session)
{
  FsRtpKeyunitManager *self =  g_object_new (FS_TYPE_RTP_KEYUNIT_MANAGER, NULL);

  self->rtpbin_internal_session = g_object_ref (rtpbin_internal_session);

  return self;
}

struct ElementProperty {
  gchar *element;
  gchar *property;
  guint value;
};

static const struct ElementProperty no_keyframe_property[] = {
  {"x264enc", "key-int-max", G_MAXINT},
  {"dsph263enc", "keyframe-interval", 600},
  {"dsph264enc", "keyframe-interval", 600},
  {"dsphdh264enc", "keyframe-interval", 0},
  {NULL, NULL, 0}
};

static void
disable_keyframes (gpointer data, gpointer user_data)
{
  GstElement *element = data;
  GstElementFactory *factory;
  const gchar *factory_name;
  guint i;

  factory = gst_element_get_factory (element);
  if (!factory)
    goto out;

  factory_name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory));
  if (!factory_name)
    goto out;

  for (i = 0; no_keyframe_property[i].element; i++)
    if (!strcmp (no_keyframe_property[i].element, factory_name))
      g_object_set (element, no_keyframe_property[i].property,
          no_keyframe_property[i].value, NULL);

out:
  gst_object_unref (element);
}

static void
fs_rtp_keyunit_manager_disable_keyframes (GstElement *codecbin)
{
  GstIterator *iter;

  iter = gst_bin_iterate_recurse (GST_BIN (codecbin));

  while (gst_iterator_foreach (iter, disable_keyframes, NULL) ==
      GST_ITERATOR_RESYNC)
    gst_iterator_resync (iter);

  gst_iterator_free (iter);
  g_object_unref (codecbin);
}

static void
on_feedback_rtcp (GObject *rtpsession, GstRTCPType type, GstRTCPFBType fbtype,
    guint sender_ssrc, guint media_ssrc, GstBuffer *fci, gpointer user_data)
{
  FsRtpKeyunitManager *self = FS_RTP_KEYUNIT_MANAGER (user_data);
  guint32 local_ssrc;
  GstElement *codecbin;

  if (type != GST_RTCP_TYPE_PSFB)
    return;

  g_object_get (rtpsession, "internal-ssrc", &local_ssrc, NULL);

  /* Let's check if the PLI or FIR is for us */
  if (fbtype == GST_RTCP_PSFB_TYPE_PLI)
  {
    if (media_ssrc != local_ssrc)
      return;
  }
  else if (fbtype == GST_RTCP_PSFB_TYPE_FIR)
  {
    guint position = 0;
    gboolean our_request = FALSE;

    for (position = 0; position < GST_BUFFER_SIZE (fci); position += 8) {
      guint8 *data = GST_BUFFER_DATA (fci) + position;
      guint32 ssrc;

      ssrc = GST_READ_UINT32_BE (data);

      if (ssrc == local_ssrc)
        our_request = TRUE;
      break;
    }
    if (!our_request)
      return;
  }
  else
  {
    return;
  }

  GST_OBJECT_LOCK (self);
  if (self->codecbin)
    codecbin = self->codecbin;
  self->codecbin = NULL;
  if (self->rtcp_feedback_id)
    g_signal_handler_disconnect (self->rtpbin_internal_session,
        self->rtcp_feedback_id);
  self->rtcp_feedback_id = 0;
  GST_OBJECT_UNLOCK (self);

  if (!codecbin)
    return;

  fs_rtp_keyunit_manager_disable_keyframes (codecbin);
}

gboolean
fs_rtp_keyunit_manager_has_key_request_feedback (FsCodec *send_codec)
{
  return !!fs_codec_get_feedback_parameter (send_codec, "nack", "pli", NULL);
}

void
fs_rtp_keyunit_manager_codecbin_changed (FsRtpKeyunitManager *self,
    GstElement *codecbin, FsCodec *send_codec)
{
  GST_OBJECT_LOCK (self);

  if (self->codecbin)
    g_object_unref (self->codecbin);
  self->codecbin = NULL;

  if (fs_rtp_keyunit_manager_has_key_request_feedback (send_codec))
  {
    self->codecbin = g_object_ref (codecbin);
    if (!self->rtcp_feedback_id)
      self->rtcp_feedback_id = g_signal_connect_object (
        self->rtpbin_internal_session, "on-feedback-rtcp",
        G_CALLBACK (on_feedback_rtcp), self, 0);
  }
  else
  {
    if (self->rtcp_feedback_id)
      g_signal_handler_disconnect (self->rtpbin_internal_session,
          self->rtcp_feedback_id);
    self->rtcp_feedback_id = 0;
  }

  GST_OBJECT_UNLOCK (self);
}

/*
 * Farsight2 - Farsight RTP DTMF Event Source
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-dtmf-event-source.c - A Farsight RTP Event Source gobject
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

#include <gst/farsight/fs-base-conference.h>

#include "fs-rtp-dtmf-event-source.h"

#define GST_CAT_DEFAULT fsrtpconference_debug

/**
 * SECTION:fs-rtp-dtmf-event-source
 * @short_description: Class to create the source of DTMF events
 *
 * This class is manages the DTMF Event source and related matters
 *
 */


struct _FsRtpDtmfEventSourcePrivate {
  gboolean disposed;
};

static FsRtpSpecialSourceClass *parent_class = NULL;

G_DEFINE_TYPE(FsRtpDtmfEventSource, fs_rtp_dtmf_event_source,
    FS_TYPE_RTP_SPECIAL_SOURCE);

#define FS_RTP_DTMF_EVENT_SOURCE_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_DTMF_EVENT_SOURCE,             \
   FsRtpDtmfEventSourcePrivate))

static void fs_rtp_dtmf_event_source_dispose (GObject *object);

static FsRtpSpecialSource *fs_rtp_dtmf_event_source_new (
    FsRtpSpecialSourceClass *klass,
    GList *negotiated_sources,
    FsCodec *selected_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    GError **error);
static gboolean fs_rtp_dtmf_event_source_class_want_source (
    FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec);
static GList *fs_rtp_dtmf_event_source_class_add_blueprint (
    FsRtpSpecialSourceClass *klass,
    GList *blueprints);

static void
fs_rtp_dtmf_event_source_class_init (FsRtpDtmfEventSourceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  FsRtpSpecialSourceClass *spsource_class = FS_RTP_SPECIAL_SOURCE_CLASS (klass);
  parent_class = fs_rtp_dtmf_event_source_parent_class;

  gobject_class->dispose = fs_rtp_dtmf_event_source_dispose;

  spsource_class->new = fs_rtp_dtmf_event_source_new;
  spsource_class->want_source = fs_rtp_dtmf_event_source_class_want_source;
  spsource_class->add_blueprint = fs_rtp_dtmf_event_source_class_add_blueprint;

}


static void
fs_rtp_dtmf_event_source_init (FsRtpDtmfEventSource *self)
{
  self->priv = FS_RTP_DTMF_EVENT_SOURCE_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_rtp_dtmf_event_source_dispose (GObject *object)
{
  FsRtpDtmfEventSource *self = FS_RTP_DTMF_EVENT_SOURCE (object);

  if (self->priv->disposed)
    return;

  self->priv->disposed = TRUE;
  G_OBJECT_CLASS (fs_rtp_dtmf_event_source_parent_class)->dispose (object);
}


static GList*
fs_rtp_dtmf_event_source_class_add_blueprint (FsRtpSpecialSourceClass *klass,
    GList *blueprints)
{
  return blueprints;
}

static gboolean
fs_rtp_dtmf_event_source_class_want_source (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec)
{
  return FALSE;
}

static gboolean
fs_rtp_dtmf_event_source_build (FsRtpDtmfEventSource *source,
    GList *negotiated_sources,
    FsCodec *selected_codec,
    GError **error)
{
  return FALSE;
}

static FsRtpSpecialSource *
fs_rtp_dtmf_event_source_new (FsRtpSpecialSourceClass *klass,
    GList *negotiated_sources,
    FsCodec *selected_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    GError **error)
{
  FsRtpDtmfEventSource *source = NULL;

  source = g_object_new (FS_TYPE_RTP_DTMF_EVENT_SOURCE,
      "bin", bin,
      "rtpmuxer", rtpmuxer,
      NULL);
  g_assert (source);

  if (!fs_rtp_dtmf_event_source_build (source, negotiated_sources,
          selected_codec, error))
  {
    g_object_unref (source);
    return NULL;
  }

  return FS_RTP_SPECIAL_SOURCE_CAST (source);
}

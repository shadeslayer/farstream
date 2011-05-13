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

#include "fs-rtp-dtmf-event-source.h"

#include <gst/farsight/fs-conference.h>

#include "fs-rtp-conference.h"
#include "fs-rtp-discover-codecs.h"
#include "fs-rtp-codec-negotiation.h"

#define GST_CAT_DEFAULT fsrtpconference_debug

/*
 * SECTION:fs-rtp-dtmf-event-source
 * @short_description: Class to create the source of DTMF events
 *
 * This class is manages the DTMF Event source and related matters
 *
 */


/* all privates variables are protected by the mutex */
struct _FsRtpDtmfEventSourcePrivate {
  gboolean disposed;
};

G_DEFINE_TYPE (FsRtpDtmfEventSource, fs_rtp_dtmf_event_source,
    FS_TYPE_RTP_SPECIAL_SOURCE);

#define FS_RTP_DTMF_EVENT_SOURCE_GET_PRIVATE(o)                         \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_DTMF_EVENT_SOURCE,     \
   FsRtpDtmfEventSourcePrivate))


static GstElement *
fs_rtp_dtmf_event_source_build (FsRtpSpecialSource *source,
    GList *negotiated_codec_associations,
    FsCodec *selected_codec);


static GList *fs_rtp_dtmf_event_source_class_add_blueprint (
    FsRtpSpecialSourceClass *klass,
    GList *blueprints);
static GList *fs_rtp_dtmf_event_source_negotiation_filter (
    FsRtpSpecialSourceClass *klass,
    GList *codec_associations);
static  FsCodec *fs_rtp_dtmf_event_source_get_codec (
    FsRtpSpecialSourceClass *klass,
    GList *negotiated_codec_associations,
    FsCodec *codec);

static void
fs_rtp_dtmf_event_source_class_init (FsRtpDtmfEventSourceClass *klass)
{
  FsRtpSpecialSourceClass *spsource_class = FS_RTP_SPECIAL_SOURCE_CLASS (klass);

  spsource_class->build = fs_rtp_dtmf_event_source_build;
  spsource_class->add_blueprint = fs_rtp_dtmf_event_source_class_add_blueprint;
  spsource_class->negotiation_filter =
    fs_rtp_dtmf_event_source_negotiation_filter;
  spsource_class->get_codec = fs_rtp_dtmf_event_source_get_codec;

  g_type_class_add_private (klass, sizeof (FsRtpDtmfEventSourcePrivate));
}

static void
fs_rtp_dtmf_event_source_init (FsRtpDtmfEventSource *self)
{
  self->priv = FS_RTP_DTMF_EVENT_SOURCE_GET_PRIVATE (self);
}

/**
 * fs_rtp_dtmf_event_source_class_add_blueprint:
 *
 * Add one blueprint for telephone-event for each different clock-rate that
 * exists in the request
 */

static GList*
fs_rtp_dtmf_event_source_class_add_blueprint (FsRtpSpecialSourceClass *klass,
    GList *blueprints)
{
  GList *item;
  GList *already_done = NULL;
  GstElementFactory *fact = NULL;
  GList *new_blueprints = NULL;

  fact = gst_element_factory_find ("rtpdtmfsrc");
  if (fact)
  {
    gst_object_unref (fact);
  }
  else
  {
    GST_CAT_WARNING (fsrtpconference_disco,
        "Could not find rtpdtmfsrc, will not offer DTMF events");
    return blueprints;
  }

  fact = gst_element_factory_find ("rtpdtmfdepay");
  if (!fact)
    GST_CAT_WARNING (fsrtpconference_disco,
        "Could not find rtpdtmfdepay, will not be able to receive DTMF events");

  for (item = g_list_first (blueprints);
       item;
       item = g_list_next (item))
  {
    CodecBlueprint *bp = item->data;
    GList *done_item = NULL;
    gboolean skip = FALSE;
    CodecBlueprint *new_bp = NULL;

    if (bp->codec->media_type != FS_MEDIA_TYPE_AUDIO)
      continue;

    if (!g_ascii_strcasecmp (bp->codec->encoding_name, "telephone-event"))
      continue;

    if (bp->codec->clock_rate == 0)
      continue;

    for (done_item = g_list_first (already_done);
         done_item;
         done_item = g_list_next (done_item))
    {
      if (GPOINTER_TO_UINT (done_item->data) == bp->codec->clock_rate)
      {
        skip = TRUE;
        break;
      }
    }
    if (skip)
      continue;

    new_bp = g_slice_new0 (CodecBlueprint);

    new_bp->codec = fs_codec_new (FS_CODEC_ID_ANY, "telephone-event",
        FS_MEDIA_TYPE_AUDIO, bp->codec->clock_rate);
    fs_codec_add_optional_parameter (new_bp->codec, "events", "0-15");
    new_bp->rtp_caps = fs_codec_to_gst_caps (new_bp->codec);
    new_bp->media_caps = gst_caps_new_any ();

    if (fact)
      new_bp->receive_pipeline_factory = g_list_prepend (NULL,
          g_list_prepend (NULL, gst_object_ref (fact)));

    new_blueprints = g_list_append (new_blueprints, new_bp);

    already_done = g_list_prepend (already_done,
        GUINT_TO_POINTER (bp->codec->clock_rate));
  }

  if (fact)
    gst_object_unref (fact);

  g_list_free (already_done);

  blueprints = g_list_concat (blueprints, new_blueprints);

  return blueprints;
}

static gboolean
_is_telephony_codec (CodecAssociation *ca, gpointer user_data)
{
  guint clock_rate = GPOINTER_TO_UINT (user_data);

  if (codec_association_is_valid_for_sending (ca, FALSE) &&
      ca->codec->media_type == FS_MEDIA_TYPE_AUDIO &&
      !g_ascii_strcasecmp (ca->codec->encoding_name, "telephone-event") &&
      ca->codec->clock_rate == clock_rate)
    return TRUE;
  else
    return FALSE;
}

/**
 * fs_rtp_dtmf_event_source_get_codec:
 * @negotiated_codec_associations: a #GList of currently negotiated
 *   #CodecAssociation
 * @selected_codec: The current #FsCodec
 *
 * Find the telephone-event codec with the proper clock rate in the list
 *
 * Returns: The #FsCodec of type "telephone-event" with the requested clock-rate
 *   from the list, or %NULL
 */
static  FsCodec *
fs_rtp_dtmf_event_source_get_codec (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codec_associations, FsCodec *selected_codec)
{
  CodecAssociation *ca = NULL;

  if (selected_codec->media_type != FS_MEDIA_TYPE_AUDIO)
    return NULL;

  ca = lookup_codec_association_custom (negotiated_codec_associations,
      _is_telephony_codec, GUINT_TO_POINTER (selected_codec->clock_rate));

  if (ca)
    return ca->send_codec;
  else
    return NULL;
}

static GstElement *
fs_rtp_dtmf_event_source_build (FsRtpSpecialSource *source,
    GList *negotiated_codec_associations,
    FsCodec *selected_codec)
{
  FsCodec *telephony_codec = NULL;
  GstCaps *caps = NULL;
  GstPad *pad = NULL;
  GstElement *dtmfsrc = NULL;
  GstElement *capsfilter = NULL;
  GstPad *ghostpad = NULL;
  GstElement *bin = NULL;

  telephony_codec = fs_rtp_dtmf_event_source_get_codec (
      FS_RTP_SPECIAL_SOURCE_GET_CLASS(source), negotiated_codec_associations,
      selected_codec);

  g_return_val_if_fail (telephony_codec, NULL);

  source->codec = fs_codec_copy (telephony_codec);

  bin = gst_bin_new (NULL);

  GST_DEBUG ("Creating telephone-event source for " FS_CODEC_FORMAT,
      FS_CODEC_ARGS (telephony_codec));

  dtmfsrc = gst_element_factory_make ("rtpdtmfsrc", NULL);
  if (!dtmfsrc)
  {
    GST_ERROR ("Could not make rtpdtmfsrc");
    goto error;
  }
  if (!gst_bin_add (GST_BIN (bin), dtmfsrc))
  {
    GST_ERROR ("Could not add rtpdtmfsrc to bin");
    gst_object_unref (dtmfsrc);
    goto error;
  }

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  if (!capsfilter)
  {
    GST_ERROR ("Could not make capsfilter");
    goto error;
  }
  if (!gst_bin_add (GST_BIN (bin), capsfilter))
  {
    GST_ERROR ("Could not add capsfilter to bin");
    gst_object_unref (capsfilter);
    goto error;
  }

  caps = fs_codec_to_gst_caps (telephony_codec);
  g_object_set (capsfilter, "caps", caps, NULL);
  {
    gchar *str = gst_caps_to_string (caps);
    GST_DEBUG ("Using caps %s for dtmf", str);
    g_free (str);
  }
  gst_caps_unref (caps);

  if (!gst_element_link_pads (dtmfsrc, "src", capsfilter, "sink"))
  {
    GST_ERROR ("Could not link the rtpdtmfsrc and its capsfilter");
    goto error;
  }

  pad = gst_element_get_static_pad (capsfilter, "src");
  if (!pad)
  {
    GST_ERROR ("Could not get \"src\" pad from capsfilter");
    goto error;
  }
  ghostpad = gst_ghost_pad_new ("src", pad);
  if (!ghostpad)
  {
    GST_ERROR ("Could not create a ghostpad for capsfilter src pad for"
        " rtpdtmfsrc");
    goto error;
  }
  if (!gst_element_add_pad (bin, ghostpad))
  {
    GST_ERROR ("Could not get \"src\" ghostpad to dtmf source bin");
    gst_object_unref (pad);
    goto error;
  }
  gst_object_unref (pad);

  return bin;

 error:
  gst_object_unref (bin);

  return NULL;
}

/*
 * This looks if there is a non-disabled codec with the requested clock rate
 * other than telephone-event.
 */

static gboolean
has_rate (CodecAssociation *ca, gpointer user_data)
{
  guint clock_rate = GPOINTER_TO_UINT (user_data);

  if (ca->codec->clock_rate == clock_rate &&
      !ca->recv_only &&
      g_ascii_strcasecmp (ca->codec->encoding_name, "telephone-event"))
    return TRUE;
  else
    return FALSE;
}

static GList *
fs_rtp_dtmf_event_source_negotiation_filter (FsRtpSpecialSourceClass *klass,
      GList *codec_associations)
{
  GList *tmp;

  for (tmp = codec_associations; tmp; tmp = g_list_next (tmp))
  {
    CodecAssociation *ca = tmp->data;

    /* Ignore disabled or non telephone-event codecs*/
    if (ca->disable || ca->reserved || ca->recv_only ||
        g_ascii_strcasecmp (ca->codec->encoding_name, "telephone-event"))
      continue;

    /* Lets disable telephone-event codecs where we don't find */
    if (!lookup_codec_association_custom (codec_associations, has_rate,
            GUINT_TO_POINTER (ca->codec->clock_rate)))
      ca->disable = TRUE;
  }

  return codec_associations;
}

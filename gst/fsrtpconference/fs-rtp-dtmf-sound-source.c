/*
 * Farstream - Farstream RTP DTMF Sound Source
 *
 * Copyright 2007-2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2009 Nokia Corp.
 *
 * fs-rtp-dtmf-sound-source.c - A Farstream RTP Sound Source gobject
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

#include "fs-rtp-dtmf-sound-source.h"

#include <farstream/fs-conference.h>

#include "fs-rtp-conference.h"
#include "fs-rtp-discover-codecs.h"
#include "fs-rtp-codec-negotiation.h"
#include "fs-rtp-codec-specific.h"

#define GST_CAT_DEFAULT fsrtpconference_debug

/*
 * SECTION:fs-rtp-dtmf-sound-source
 * @short_description: Class to create the source of DTMF sounds
 *
 * This class is manages the DTMF Sound source and related matters
 *
 */


/* all privates variables are protected by the mutex */
struct _FsRtpDtmfSoundSourcePrivate {
  gboolean disposed;
};

G_DEFINE_TYPE(FsRtpDtmfSoundSource, fs_rtp_dtmf_sound_source,
    FS_TYPE_RTP_SPECIAL_SOURCE);

#define FS_RTP_DTMF_SOUND_SOURCE_GET_PRIVATE(o)                         \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_DTMF_SOUND_SOURCE,     \
   FsRtpDtmfSoundSourcePrivate))


static GstElement *
fs_rtp_dtmf_sound_source_build (FsRtpSpecialSource *source,
    GList *negotiated_codec_associations,
    FsCodec *selected_codec);


static FsCodec *fs_rtp_dtmf_sound_source_get_codec (
    FsRtpSpecialSourceClass *klass,
    GList *negotiated_codec_associations,
    FsCodec *selected_codec);


static void
fs_rtp_dtmf_sound_source_class_init (FsRtpDtmfSoundSourceClass *klass)
{
  FsRtpSpecialSourceClass *spsource_class = FS_RTP_SPECIAL_SOURCE_CLASS (klass);

  spsource_class->build = fs_rtp_dtmf_sound_source_build;
  spsource_class->get_codec = fs_rtp_dtmf_sound_source_get_codec;

  g_type_class_add_private (klass, sizeof (FsRtpDtmfSoundSourcePrivate));
}

static void
fs_rtp_dtmf_sound_source_init (FsRtpDtmfSoundSource *self)
{
  self->priv = FS_RTP_DTMF_SOUND_SOURCE_GET_PRIVATE (self);
}

static gboolean
_is_law_codec (CodecAssociation *ca, gpointer user_data)
{
  if (codec_association_is_valid_for_sending (ca, FALSE) &&
      (ca->codec->id == 0 || ca->codec->id == 8))
    return TRUE;
  else
    return FALSE;
}

/**
 * get_telephone_sound_codec:
 * @codecs: a #GList of #FsCodec
 *
 * Find the first occurence of PCMA or PCMU codecs
 *
 * Returns: The #FsCodec of type PCMA/U from the list or %NULL
 */
static FsCodec *
get_pcm_law_sound_codec (GList *codecs,
    gchar **encoder_name,
    gchar **payloader_name,
    CodecAssociation **out_ca)
{
  CodecAssociation *ca = NULL;

  ca = lookup_codec_association_custom (codecs, _is_law_codec, NULL);

  if (!ca)
    return NULL;

  if (ca->codec->id == 0)
  {
    if (encoder_name)
      *encoder_name = "mulawenc";
    if (payloader_name)
      *payloader_name = "rtppcmupay";
  }
  else if (ca->codec->id == 8)
  {
    if (encoder_name)
      *encoder_name = "alawenc";
    if (payloader_name)
      *payloader_name = "rtppcmapay";
  }

  if (out_ca)
    *out_ca = ca;

  return ca->send_codec;
}

static gboolean
_check_element_factory (gchar *name)
{
  GstElementFactory *fact = NULL;

  g_return_val_if_fail (name, FALSE);

  fact = gst_element_factory_find (name);
  if (fact)
    gst_object_unref (fact);

  return (fact != NULL);
}

static CodecAssociation *
_get_main_codec_association (GList *codec_associations, FsCodec *codec)
{
  CodecAssociation *ca = lookup_codec_association_by_codec_for_sending (
      codec_associations, codec);

  if (ca && codec_association_is_valid_for_sending (ca, TRUE) &&
      codec_blueprint_has_factory (ca->blueprint, TRUE))
    return ca;
  else
    return NULL;
}

static FsCodec *
fs_rtp_dtmf_sound_source_get_codec (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codec_associations,
    FsCodec *selected_codec)
{
  FsCodec *codec = NULL;
  gchar *encoder_name = NULL;
  gchar *payloader_name = NULL;
  CodecAssociation *ca;

  if (selected_codec->media_type != FS_MEDIA_TYPE_AUDIO)
    return NULL;

  if (!_check_element_factory ("dtmfsrc"))
    return NULL;

  if (selected_codec->clock_rate == 8000)
  {
    codec = get_pcm_law_sound_codec (negotiated_codec_associations,
        &encoder_name, &payloader_name, NULL);
    if (codec) {
      if (!_check_element_factory (encoder_name))
        return NULL;
      if (!_check_element_factory (payloader_name))
        return NULL;
      return codec;
    }
  }

  ca = _get_main_codec_association (negotiated_codec_associations,
      selected_codec);

  if (ca)
    return ca->send_codec;
  else
    return NULL;
}

static GstElement *
fs_rtp_dtmf_sound_source_build (FsRtpSpecialSource *source,
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
  GstElement *encoder = NULL;
  GstElement *payloader = NULL;
  gchar *encoder_name = NULL;
  gchar *payloader_name = NULL;
  CodecAssociation *ca = NULL;


  if (selected_codec->clock_rate == 8000)
    telephony_codec = get_pcm_law_sound_codec (negotiated_codec_associations,
        &encoder_name, &payloader_name, &ca);

  if (!telephony_codec)
  {
    ca = _get_main_codec_association (negotiated_codec_associations,
        selected_codec);
    if (ca)
      telephony_codec = ca->send_codec;
  }

  g_return_val_if_fail (telephony_codec, NULL);

  source->codec = fs_codec_copy (telephony_codec);

  GST_DEBUG ("Creating dtmf sound source for " FS_CODEC_FORMAT,
      FS_CODEC_ARGS (telephony_codec));

  bin = gst_bin_new (NULL);

  dtmfsrc = gst_element_factory_make ("dtmfsrc", NULL);
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

  pad = gst_element_get_static_pad (capsfilter, "src");
  if (!pad)
  {
    GST_ERROR ("Could not get \"src\" pad from capsfilter");
    goto error;
  }
  ghostpad = gst_ghost_pad_new ("src", pad);
  if (!ghostpad)
  {
    GST_ERROR ("Could not create a ghostpad for capsfilter src pad"
        " for dtmfsrc");
    goto error;
  }
  if (!gst_element_add_pad (bin, ghostpad))
  {
    GST_ERROR ("Could not get \"src\" ghostpad to dtmf sound source bin");
    gst_object_unref (pad);
    goto error;
  }
  gst_object_unref (pad);


  if (ca)
  {
    gchar *codec_bin_name = g_strdup_printf ("dtmf_send_codecbin_%d",
        telephony_codec->id);
    GError *error = NULL;
    GstElement *codecbin = create_codec_bin_from_blueprint (
        telephony_codec, ca->blueprint, codec_bin_name, TRUE, &error);

    if (!codecbin)
    {
      GST_ERROR ("Could not make %s: %s", codec_bin_name,
          error ? error->message : "No error message!");
      g_clear_error (&error);
      g_free (codec_bin_name);
      goto error;
    }

    if (!gst_bin_add (GST_BIN (bin), codecbin))
    {
      GST_ERROR ("Could not add %s to bin", codec_bin_name);
      gst_object_unref (codecbin);
      g_free (codec_bin_name);
      goto error;
    }

    if (!gst_element_link_pads (dtmfsrc, "src", codecbin, "sink"))
    {
      GST_ERROR ("Could not link the rtpdtmfsrc and %s", codec_bin_name);
      g_free (codec_bin_name);
      goto error;
    }

    if (!gst_element_link_pads (codecbin, "src", capsfilter, "sink"))
    {
      GST_ERROR ("Could not link the %s and its capsfilter", codec_bin_name);
      g_free (codec_bin_name);
      goto error;
    }

    g_free (codec_bin_name);
  }
  else
  {
    encoder = gst_element_factory_make (encoder_name, NULL);
    if (!encoder)
    {
      GST_ERROR ("Could not make %s", encoder_name);
      goto error;
    }
    if (!gst_bin_add (GST_BIN (bin), encoder))
    {
      GST_ERROR ("Could not add %s to bin", encoder_name);
      gst_object_unref (encoder);
      goto error;
    }

    if (!gst_element_link_pads (dtmfsrc, "src", encoder, "sink"))
    {
      GST_ERROR ("Could not link the rtpdtmfsrc and %s", encoder_name);
      goto error;
    }

    payloader = gst_element_factory_make (payloader_name, NULL);
    if (!payloader)
    {
      GST_ERROR ("Could not make %s", payloader_name);
      goto error;
    }
    if (!gst_bin_add (GST_BIN (bin), payloader))
    {
      GST_ERROR ("Could not add %s to bin", payloader_name);
      gst_object_unref (payloader);
      goto error;
    }

    if (!gst_element_link_pads (encoder, "src", payloader, "sink"))
    {
      GST_ERROR ("Could not link the %s and %s", encoder_name, payloader_name);
      goto error;
    }

    if (!gst_element_link_pads (payloader, "src", capsfilter, "sink"))
    {
      GST_ERROR ("Could not link the %s and its capsfilter", payloader_name);
      goto error;
    }
  }

  return bin;

 error:
  gst_object_unref (bin);

  return NULL;
}


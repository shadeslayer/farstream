/*
 * Farsight2 - Farsight RTP DTMF Sound Source
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-dtmf-sound-source.c - A Farsight RTP Sound Source gobject
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

#include "fs-rtp-conference.h"
#include "fs-rtp-discover-codecs.h"
#include "fs-rtp-codec-negotiation.h"

#include "fs-rtp-dtmf-sound-source.h"

#define GST_CAT_DEFAULT fsrtpconference_debug

/**
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

static FsRtpSpecialSourceClass *parent_class = NULL;

G_DEFINE_TYPE(FsRtpDtmfSoundSource, fs_rtp_dtmf_sound_source,
    FS_TYPE_RTP_SPECIAL_SOURCE);

#define FS_RTP_DTMF_SOUND_SOURCE_GET_PRIVATE(o)                         \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_DTMF_SOUND_SOURCE,     \
   FsRtpDtmfSoundSourcePrivate))


static GstElement *
fs_rtp_dtmf_sound_source_build (FsRtpSpecialSource *source,
    GList *negotiated_codecs,
    FsCodec *selected_codec,
    GError **error);


static gboolean fs_rtp_dtmf_sound_source_class_want_source (
    FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec);

static void
fs_rtp_dtmf_sound_source_class_init (FsRtpDtmfSoundSourceClass *klass)
{
  FsRtpSpecialSourceClass *spsource_class = FS_RTP_SPECIAL_SOURCE_CLASS (klass);
  parent_class = fs_rtp_dtmf_sound_source_parent_class;

  spsource_class->build = fs_rtp_dtmf_sound_source_build;
  spsource_class->want_source = fs_rtp_dtmf_sound_source_class_want_source;

  g_type_class_add_private (klass, sizeof (FsRtpDtmfSoundSourcePrivate));
}

static void
fs_rtp_dtmf_sound_source_init (FsRtpDtmfSoundSource *self)
{
  FsRtpSpecialSource *source = FS_RTP_SPECIAL_SOURCE (self);

  self->priv = FS_RTP_DTMF_SOUND_SOURCE_GET_PRIVATE (self);

  source->order = 2;
}

static gboolean
_is_law_codec (CodecAssociation *ca, gpointer user_data)
{
  if (ca->codec->id == 0 || ca->codec->id == 8)
    return TRUE;
  else return FALSE;
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
    gchar **payloader_name)
{
  CodecAssociation *ca = NULL;

  ca = codec_association_find_custom (codecs, _is_law_codec, NULL);

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

  return ca->codec;
}

static gboolean
_check_element_factory (gchar *name)
{
  GstElementFactory *fact = NULL;

  g_return_val_if_fail (name, FALSE);

  fact = gst_element_factory_find ("dtmfsrc");
  if (fact)
    gst_object_unref (fact);

  return (fact != NULL);
}

static gboolean
fs_rtp_dtmf_sound_source_class_want_source (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec)
{
  FsCodec *codec = NULL;
  gchar *encoder_name = NULL;
  gchar *payloader_name = NULL;

  if (selected_codec->media_type != FS_MEDIA_TYPE_AUDIO)
    return FALSE;

  codec = get_pcm_law_sound_codec (negotiated_codecs,
      &encoder_name, &payloader_name);
  if (!codec)
    return FALSE;


  if (!_check_element_factory ("dtmfsrc"))
    return FALSE;

  if (!_check_element_factory (encoder_name))
    return FALSE;
  if (!_check_element_factory (payloader_name))
      return FALSE;

  return TRUE;
}

static GstElement *
fs_rtp_dtmf_sound_source_build (FsRtpSpecialSource *source,
    GList *negotiated_codecs,
    FsCodec *selected_codec,
    GError **error)
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

  telephony_codec = get_pcm_law_sound_codec (negotiated_codecs,
      &encoder_name, &payloader_name);

  if (!telephony_codec)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Could not find a pcma/pcmu to send dtmf on");
    return NULL;
  }

  bin = gst_bin_new (NULL);

  dtmfsrc = gst_element_factory_make ("dtmfsrc", NULL);
  if (!dtmfsrc)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not make rtpdtmfsrc");
    goto error;
  }
  if (!gst_bin_add (GST_BIN (bin), dtmfsrc))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add rtpdtmfsrc to bin");
    gst_object_unref (dtmfsrc);
    goto error;
  }

  encoder = gst_element_factory_make (encoder_name, NULL);
  if (!encoder)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not make %s", encoder_name);
    goto error;
  }
  if (!gst_bin_add (GST_BIN (bin), encoder))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add %s to bin", encoder_name);
    gst_object_unref (dtmfsrc);
    goto error;
  }

  if (!gst_element_link_pads (dtmfsrc, "src", encoder, "sink"))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the rtpdtmfsrc and %s", encoder_name);
    goto error;
  }

  payloader = gst_element_factory_make (payloader_name, NULL);
  if (!payloader)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not make %s", payloader_name);
    goto error;
  }
  if (!gst_bin_add (GST_BIN (bin), payloader))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add %s to bin", payloader_name);
    gst_object_unref (dtmfsrc);
    goto error;
  }

  if (!gst_element_link_pads (encoder, "src", payloader, "sink"))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the %s and %s", encoder_name, payloader_name);
    goto error;
  }

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  if (!capsfilter)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not make capsfilter");
    goto error;
  }
  if (!gst_bin_add (GST_BIN (bin), capsfilter))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add capsfilter to bin");
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

  if (!gst_element_link_pads (payloader, "src", capsfilter, "sink"))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the %s and its capsfilter", payloader_name);
    goto error;
  }

  pad = gst_element_get_static_pad (capsfilter, "src");
  if (!pad)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not get \"src\" pad from capsfilter");
    goto error;
  }
  ghostpad = gst_ghost_pad_new ("src", pad);
  if (!ghostpad)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create a ghostpad for capsfilter src pad for dtmfsrc");
    goto error;
  }
  if (!gst_element_add_pad (bin, ghostpad))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not get \"src\" ghostpad to dtmf sound source bin");
    gst_object_unref (pad);
    goto error;
  }
  gst_object_unref (pad);

  return bin;

 error:
  gst_object_unref (bin);

  return NULL;
}


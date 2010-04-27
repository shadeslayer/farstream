/*
 * fs-rtp-codec-specific.c - Per-codec SDP negotiation
 *
 * Farsight RTP/AVP/SAVP/AVPF Module
 * Copyright (C) 2007-2010 Collabora Ltd.
 * Copyright (C) 2007-2010 Nokia Corporation
 *   @author Olivier Crete <olivier.crete@collabora.co.uk>
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

#include "fs-rtp-codec-specific.h"

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <gst/gst.h>

#include "fs-rtp-conference.h"

#define GST_CAT_DEFAULT fsrtpconference_nego

/*
 * This must be kept to the maximum number of parameters + 1
 */
#define MAX_PARAMS 20

struct SdpParam {
  gchar *name;
  /* The param type tell us if they should be added to the send
     or recv pipelines or both */
  FsParamType paramtype;
  gboolean (*negotiate_param) (const struct SdpParam *sdp_param,
      FsCodec *local_codec, FsCodecParameter *local_param,
      FsCodec *remote_codec, FsCodecParameter *remote_param,
      FsCodec *negotiated_codec);
  const gchar *default_value;
};

struct SdpNegoFunction {
  FsMediaType media_type;
  const gchar *encoding_name;
  FsCodec * (* sdp_negotiate_codec) (FsCodec *local_codec,
      FsParamType local_paramtypes,
      FsCodec *remote_codec,
      FsParamType remote_paramtypes,
      const struct SdpNegoFunction *nf);
  const struct SdpParam params[MAX_PARAMS];
};

struct SdpParamMinMax {
  const gchar *encoding_name;
  const gchar *param_name;
  gint min;
  gint max;
};


static FsCodec *
sdp_negotiate_codec_default (
    FsCodec *local_codec, FsParamType local_paramtypes,
    FsCodec *remote_codec, FsParamType remote_paramtypes,
    const struct SdpNegoFunction *nf);

static FsCodec *
sdp_negotiate_codec_h263_2000 (
    FsCodec *local_codec, FsParamType local_paramtypes,
    FsCodec *remote_codec, FsParamType remote_paramtypes,
    const struct SdpNegoFunction *nf);
static FsCodec *
sdp_negotiate_codec_telephone_event (
    FsCodec *local_codec, FsParamType local_paramtypes,
    FsCodec *remote_codec, FsParamType remote_paramtypes,
    const struct SdpNegoFunction *nf);


/* Generic param negotiation functions */

static gboolean param_minimum (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec);
static gboolean param_maximum (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec);
static gboolean param_equal_or_ignore (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec);
static gboolean param_equal_or_reject (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec);
static gboolean param_list_commas (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec);
static gboolean param_copy (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec);

/* Codec specific negotiation functions */

static gboolean param_ilbc_mode (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec);
static gboolean param_h263_1998_custom (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec);
static gboolean param_h263_1998_cpcf (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec);

const static struct SdpParamMinMax sdp_min_max_params[] = {
  {"H263-1998", "sqcif", 1, 32},
  {"H263-1998", "qcif", 1, 32},
  {"H263-1998", "cif", 1, 32},
  {"H263-1998", "cif4", 1, 32},
  {"H263-1998", "cif16", 1, 32},
  {"H263-1998", "bpp", 1, 65536},
  {"H263-2000", "level", 0, 100},
  {NULL, NULL}
};

static const struct SdpNegoFunction sdp_nego_functions[] = {
  /* iLBC: RFC 3959 */
  {FS_MEDIA_TYPE_AUDIO, "iLBC", sdp_negotiate_codec_default,
   {
     {"mode", FS_PARAM_TYPE_BOTH, param_ilbc_mode},
     {NULL, 0, NULL}
   }
  },
  /* H263-1998 and H263-2000: RFC 4629 */
  {FS_MEDIA_TYPE_VIDEO, "H263-1998", sdp_negotiate_codec_default,
   {
     {"sqcif", FS_PARAM_TYPE_SEND, param_maximum},
     {"qcif", FS_PARAM_TYPE_SEND, param_maximum},
     {"cif", FS_PARAM_TYPE_SEND, param_maximum},
     {"cif4", FS_PARAM_TYPE_SEND, param_maximum},
     {"cif16", FS_PARAM_TYPE_SEND, param_maximum},
     {"custom", FS_PARAM_TYPE_SEND, param_h263_1998_custom},
     {"f", FS_PARAM_TYPE_SEND, param_equal_or_ignore},
     {"i", FS_PARAM_TYPE_SEND, param_equal_or_ignore},
     {"j", FS_PARAM_TYPE_SEND, param_equal_or_ignore},
     {"t", FS_PARAM_TYPE_SEND, param_equal_or_ignore},
     {"k", FS_PARAM_TYPE_SEND, param_equal_or_ignore},
     {"n", FS_PARAM_TYPE_SEND, param_equal_or_ignore},
     {"p", FS_PARAM_TYPE_SEND, param_list_commas},
     {"par", FS_PARAM_TYPE_SEND, param_equal_or_ignore},
     {"cpcf", FS_PARAM_TYPE_SEND, param_h263_1998_cpcf},
     {"bpp", FS_PARAM_TYPE_SEND, param_minimum},
     {"hrd", FS_PARAM_TYPE_SEND, param_equal_or_ignore},
     {"interlace", FS_PARAM_TYPE_SEND, param_equal_or_ignore},
     {NULL, 0, NULL}
   }
  },
  {FS_MEDIA_TYPE_VIDEO, "H263-2000", sdp_negotiate_codec_h263_2000,
   {
     /* Add H263-1998 params here */
     {"profile", FS_PARAM_TYPE_BOTH, param_equal_or_reject, "0"},
     {"level", FS_PARAM_TYPE_SEND, param_minimum},
     {NULL, 0, NULL}
   }
  },
  /* VORBIS: RFC 5215 */
  {FS_MEDIA_TYPE_AUDIO, "VORBIS", sdp_negotiate_codec_default,
   {
     {"configuration", FS_PARAM_TYPE_CONFIG, param_copy},
     {NULL, 0, NULL}
   }
  },
  /* THEORA: as an extension from vorbis using RFC 5215 */
  {FS_MEDIA_TYPE_VIDEO, "THEORA", sdp_negotiate_codec_default,
   {
     {"configuration", FS_PARAM_TYPE_CONFIG, param_copy},
     {NULL, 0, NULL}
   }
  },
  {FS_MEDIA_TYPE_AUDIO, "G729", sdp_negotiate_codec_default,
   {
     {"annexb", FS_PARAM_TYPE_SEND, param_equal_or_ignore, "yes"},
     {NULL, 0, NULL}
   }
  },
  {FS_MEDIA_TYPE_VIDEO, "H264", sdp_negotiate_codec_default,
   {
     {NULL, 0, NULL}
   }
  },
#if 0
   {"sprop-parameter-sets", "sprop-interleaving-depth", "sprop-deint-buf-req",
    "sprop-init-buf-time", "sprop-max-don-diff", NULL}},
#endif
  {FS_MEDIA_TYPE_AUDIO, "telephone-event", sdp_negotiate_codec_telephone_event,
   {{NULL, 0, NULL}}
  },
  {0, NULL, NULL}
};

static const struct SdpNegoFunction *
get_sdp_nego_function (FsMediaType media_type, const gchar *encoding_name)
{
  int i;

  for (i = 0; sdp_nego_functions[i].sdp_negotiate_codec; i++)
    if (sdp_nego_functions[i].media_type == media_type &&
        !g_ascii_strcasecmp (sdp_nego_functions[i].encoding_name,
            encoding_name))
      return &sdp_nego_functions[i];

  return NULL;
}


/*
 * This function currently returns %TRUE if any configuration parameter is there
 * if some codecs require something more complicated, we will need a custom
 * functions for each codec
 */

gboolean
codec_needs_config (FsCodec *codec)
{
  const struct SdpNegoFunction *nf;
  gboolean has_config_param = FALSE;
  int i;

  g_return_val_if_fail (codec, FALSE);

  nf = get_sdp_nego_function (codec->media_type, codec->encoding_name);

  if (!nf)
    return FALSE;

  for (i = 0; nf->params[i].name; i++)
  {
    if (nf->params[i].paramtype & FS_PARAM_TYPE_CONFIG)
    {
      has_config_param = TRUE;
      if (fs_codec_get_optional_parameter (codec, nf->params[i].name, NULL))
        return FALSE;
    }
  }

  return has_config_param;
}


static gboolean
codec_has_config_data_named_internal (const struct SdpNegoFunction *nf,
    const gchar *param_name)
{
  gint i;

  if (!nf)
    return FALSE;

  for (i = 0; nf->params[i].name; i++)
    if (nf->params[i].paramtype & FS_PARAM_TYPE_CONFIG &&
        !g_ascii_strcasecmp (nf->params[i].name, param_name))
      return TRUE;

  return FALSE;
}


gboolean
codec_has_config_data_named (FsCodec *codec, const gchar *param_name)
{
  const struct SdpNegoFunction *nf;

  g_return_val_if_fail (codec, FALSE);
  g_return_val_if_fail (param_name, FALSE);

  nf = get_sdp_nego_function (codec->media_type, codec->encoding_name);

  if (nf)
    return codec_has_config_data_named_internal (nf, param_name);
  else
    return FALSE;
}

/**
 * codec_copy_without_config:
 * @codec: a #FsCodec
 *
 * Makes a copy of a #FsCodec, but removes all configuration parameters
 *
 * Returns: the newly-allocated #FsCodec
 */

FsCodec *
codec_copy_without_config (FsCodec *codec)
{
  FsCodec *copy = fs_codec_copy (codec);
  GList *item = NULL;
  const struct SdpNegoFunction *nf;

  nf = get_sdp_nego_function (codec->media_type, codec->encoding_name);

  if (nf)
  {
    for (item = copy->optional_params; item;)
    {
      FsCodecParameter *param = item->data;
      GList *next = g_list_next (item);

      if (codec_has_config_data_named_internal (nf, param->name))
        fs_codec_remove_optional_parameter (copy, param);

      item = next;
    }
  }

  return copy;
}

/**
 * sdp_negotiate_codec:
 *
 * This function performs SDP offer-answer negotiation on a codec, it compares
 * the local codec (the one that would be sent in an offer) and the remote
 * codec (the one that would be received from the other side)  and tries to see
 * if they can be negotiated into a new codec (what would be sent in a reply).
 * If such a codec can be created, it returns it, otherwise it returns NULL.
 *
 * RFC 3264
 */

FsCodec *
sdp_negotiate_codec (FsCodec *local_codec, FsCodec *remote_codec)
{
  FsParamType local_paramtypes =
    FS_PARAM_TYPE_BOTH | FS_PARAM_TYPE_SEND_AVOID_NEGO | FS_PARAM_TYPE_CONFIG;
  FsParamType remote_paramtypes = FS_PARAM_TYPE_BOTH | FS_PARAM_TYPE_CONFIG;
  const struct SdpNegoFunction *nf;

  g_return_val_if_fail (local_codec, NULL);
  g_return_val_if_fail (remote_codec, NULL);

  if (local_codec->media_type != remote_codec->media_type)
  {
    GST_LOG ("Wrong media type, local: %s, remote: %s",
        fs_media_type_to_string (local_codec->media_type),
        fs_media_type_to_string (remote_codec->media_type));
    return NULL;
  }
  if (g_ascii_strcasecmp (local_codec->encoding_name,
        remote_codec->encoding_name))
  {
    GST_LOG ("Encoding names dont match, local: %s, remote: %s",
        local_codec->encoding_name, remote_codec->encoding_name);
    return NULL;
  }

  if (local_codec->clock_rate && remote_codec->clock_rate &&
      local_codec->clock_rate != remote_codec->clock_rate)
  {
    GST_LOG ("Clock rates differ local=%u remote=%u", local_codec->clock_rate,
        remote_codec->clock_rate);
    return NULL;
  }

  nf = get_sdp_nego_function (local_codec->media_type,
      local_codec->encoding_name);

  if (nf)
    return nf->sdp_negotiate_codec (local_codec, local_paramtypes,
        remote_codec, remote_paramtypes, nf);
  else
    return sdp_negotiate_codec_default (local_codec, local_paramtypes,
        remote_codec, remote_paramtypes, NULL);
}

static const struct SdpParam *
get_sdp_param (const struct SdpNegoFunction *nf, const gchar *param_name)
{
  gint i;

  for (i = 0; nf->params[i].name; i++)
    if (!g_ascii_strcasecmp (param_name, nf->params[i].name))
      return &nf->params[i];

  return NULL;
}


static gboolean
param_negotiate (const struct SdpNegoFunction *nf, const gchar *param_name,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsParamType local_paramtypes,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsParamType remote_paramtypes,
    FsCodec *negotiated_codec)
{
  const struct SdpParam *sdp_param = NULL;

  if (nf)
    sdp_param = get_sdp_param (nf, param_name);

  if (sdp_param)
  {
    if ((sdp_param->paramtype & FS_PARAM_TYPE_BOTH) != FS_PARAM_TYPE_BOTH)
    {
      if (!(sdp_param->paramtype & local_paramtypes))
        local_param = NULL;
      if (!(sdp_param->paramtype & remote_paramtypes))
        remote_param = NULL;
    }

    if (local_param || remote_param)
      return sdp_param->negotiate_param (sdp_param,
          local_codec, local_param, remote_codec,
          remote_param, negotiated_codec);
    else
      return TRUE;
  }
  else
  {
    /* Assume unknown parameters are of type SEND */
    if (!((remote_paramtypes | local_paramtypes) & FS_PARAM_TYPE_SEND))
      return TRUE;

    if (local_param && remote_param)
    {
      /* Only accept codecs where unknown parameters are IDENTICAL if
       * they are present on both sides */
      if (!g_ascii_strcasecmp (local_param->value, remote_param->value))
        fs_codec_add_optional_parameter (negotiated_codec, local_param->name,
            local_param->value);
      else
        return FALSE;
    }
    else if (local_param)
    {
      fs_codec_add_optional_parameter (negotiated_codec, local_param->name,
          local_param->value);
    }
    else if (remote_param)
    {
      fs_codec_add_optional_parameter (negotiated_codec, remote_param->name,
          remote_param->value);
    }
  }

  return TRUE;
}

static FsCodec *
sdp_negotiate_codec_default (FsCodec *local_codec, FsParamType local_paramtypes,
    FsCodec *remote_codec, FsParamType remote_paramtypes,
    const struct SdpNegoFunction *nf)
{
  FsCodec *negotiated_codec = NULL;
  FsCodec *local_codec_copy = NULL;
  GList *local_param_e = NULL, *remote_param_e = NULL;

  GST_LOG ("Using default codec negotiation function for %s",
      local_codec->encoding_name);

  if (local_codec->channels && remote_codec->channels &&
      local_codec->channels != remote_codec->channels)
  {
    GST_LOG ("Channel counts differ local=%u remote=%u",
        local_codec->channels,
        remote_codec->channels);
    return NULL;
  }

  negotiated_codec = fs_codec_copy (remote_codec);
  while (negotiated_codec->optional_params)
    fs_codec_remove_optional_parameter (negotiated_codec,
        negotiated_codec->optional_params->data);


  /* Lets fix here missing clock rates and channels counts */
  if (negotiated_codec->channels == 0 && local_codec->channels)
    negotiated_codec->channels = local_codec->channels;
  if (negotiated_codec->clock_rate == 0)
    negotiated_codec->clock_rate = local_codec->clock_rate;


  if (local_paramtypes & FS_PARAM_TYPE_SEND_AVOID_NEGO)
  {
    negotiated_codec->ABI.ABI.ptime = local_codec->ABI.ABI.ptime;
    negotiated_codec->ABI.ABI.maxptime = local_codec->ABI.ABI.maxptime;
  }
  else if (remote_paramtypes & FS_PARAM_TYPE_SEND_AVOID_NEGO)
  {
    negotiated_codec->ABI.ABI.ptime = remote_codec->ABI.ABI.ptime;
    negotiated_codec->ABI.ABI.maxptime = remote_codec->ABI.ABI.maxptime;
  }

  local_codec_copy = fs_codec_copy (local_codec);

  for (remote_param_e = remote_codec->optional_params;
       remote_param_e;
       remote_param_e = g_list_next (remote_param_e))
  {
    FsCodecParameter *remote_param = remote_param_e->data;
    FsCodecParameter *local_param =  fs_codec_get_optional_parameter (
        local_codec_copy, remote_param->name, NULL);

    if (!param_negotiate (nf, remote_param->name,
            local_codec, local_param, local_paramtypes,
            remote_codec, remote_param, remote_paramtypes,
            negotiated_codec))
      goto non_matching_codec;

    if (local_param)
      fs_codec_remove_optional_parameter (local_codec_copy, local_param);
  }

  for (local_param_e = local_codec_copy->optional_params;
       local_param_e;
       local_param_e = g_list_next (local_param_e))
  {
    FsCodecParameter *local_param = local_param_e->data;

    if (!param_negotiate (nf, local_param->name,
            local_codec, local_param, local_paramtypes,
            NULL, NULL, 0, negotiated_codec))
      goto non_matching_codec;
  }

  fs_codec_destroy (local_codec_copy);

  return negotiated_codec;

non_matching_codec:

  GST_LOG ("Codecs don't really match");
  fs_codec_destroy (local_codec_copy);
  fs_codec_destroy (negotiated_codec);
  return NULL;
}

/*
 * sdp_negotiate_codec_h263_2000:
 *
 * For H263-2000, the "profile" must be exactly the same. If it is not,
 * it must be rejected. If there is none, we assume its 0.
 *
 * If profile or level is used, no other parameter should be there.
 *
 * RFC 4629
 */

static FsCodec *
sdp_negotiate_codec_h263_2000 (
    FsCodec *local_codec, FsParamType local_paramtypes,
    FsCodec *remote_codec, FsParamType remote_paramtypes,
    const struct SdpNegoFunction *nf)
{
  const struct SdpNegoFunction *h263_1998_nf;
  GST_DEBUG ("Using H263-2000 negotiation function");

  if ((fs_codec_get_optional_parameter (remote_codec, "profile", NULL) ||
          fs_codec_get_optional_parameter (remote_codec, "level", NULL)) &&
      (fs_codec_get_optional_parameter (local_codec, "profile", NULL) ||
          fs_codec_get_optional_parameter (local_codec, "level", NULL)))
    return sdp_negotiate_codec_default (local_codec, local_paramtypes,
        remote_codec, remote_paramtypes, nf);


  h263_1998_nf = get_sdp_nego_function (FS_MEDIA_TYPE_VIDEO, "H263-1998");

  return sdp_negotiate_codec_default (local_codec, local_paramtypes,
      remote_codec, remote_paramtypes, h263_1998_nf);
}

struct event_range {
  int first;
  int last;
};

static gint
event_range_cmp (gconstpointer a, gconstpointer b)
{
  const struct event_range *era = a;
  const struct event_range *erb = b;

  return era->first - erb->first;
}

static GList *
parse_events (const gchar *events)
{
  gchar **ranges_strv;
  GList *ranges = NULL;
  int i;

  ranges_strv = g_strsplit (events, ",", 0);

  for (i = 0; ranges_strv[i]; i++)
  {
    struct event_range *er = g_slice_new (struct event_range);

    er->first = atoi (ranges_strv[i]);
    if (index (ranges_strv[i], '-'))
      er->last = atoi (index (ranges_strv[i], '-') + 1);
    else
      er->last = er->first;

    ranges = g_list_insert_sorted (ranges, er, event_range_cmp);
  }

  g_strfreev (ranges_strv);

  return ranges;
}

static void
event_range_free (gpointer data)
{
  g_slice_free (struct event_range, data);
}

static gchar *
event_intersection (const gchar *remote_events, const gchar *local_events)
{
  GList *remote_ranges = NULL;
  GList *local_ranges = NULL;
  GList *intersected_ranges = NULL;
  GList *item;
  GString *intersection_gstr;

  if (!g_regex_match_simple ("^[0-9]+(-[0-9]+)?(,[0-9]+(-[0-9]+)?)*$",
          remote_events, 0, 0))
  {
    GST_WARNING ("Invalid remote events (events=%s)", remote_events);
    return NULL;
  }

  if (!g_regex_match_simple ("^[0-9]+(-[0-9]+)?(,[0-9]+(-[0-9]+)?)*$",
          local_events, 0, 0))
  {
    GST_WARNING ("Invalid local events (events=%s)", local_events);
    return NULL;
  }

  remote_ranges = parse_events (remote_events);
  local_ranges = parse_events (local_events);

  while ((item = remote_ranges) != NULL)
  {
    struct event_range *er1 = item->data;
    GList *item2;

    item2 = local_ranges;
    while (item2)
    {
      struct event_range *er2 = item2->data;

      if (er1->last < er2->first) {
        break;
      }

      if (er1->first < er2->last)
      {
        struct event_range *new_er = g_slice_new (struct event_range);

        new_er->first = MAX (er1->first, er2->first);
        new_er->last = MIN (er1->last, er2->last);
        intersected_ranges = g_list_append (intersected_ranges, new_er);
      }

      item2 = item2->next;
      if (er2->last <= er1->last)
      {
        local_ranges = g_list_remove (local_ranges, er2);
        event_range_free (er2);
      }
    }

    remote_ranges = g_list_delete_link (remote_ranges, item);
    event_range_free (er1);
  }

  while (local_ranges)
  {
    event_range_free (local_ranges->data);
    local_ranges = g_list_delete_link (local_ranges, local_ranges);
  }

  if (!intersected_ranges)
  {
    GST_DEBUG ("There is no intersection before the events %s and %s",
        remote_events, local_events);
    return NULL;
  }

  intersection_gstr = g_string_new ("");

  while ((item = intersected_ranges) != NULL)
  {
    struct event_range *er = item->data;

    if (intersection_gstr->len)
      g_string_append_c (intersection_gstr, ',');

    if (er->first == er->last)
      g_string_append_printf (intersection_gstr, "%d", er->first);
    else
      g_string_append_printf (intersection_gstr, "%d-%d", er->first, er->last);

    intersected_ranges = g_list_delete_link (intersected_ranges, item);
    event_range_free (er);
  }

  return g_string_free (intersection_gstr, FALSE);
}


/**
 * sdp_negotiate_codec_telephone_event:
 *
 * For telephone events, it finds the list of events that are the same.
 * So it tried to intersect both lists to come up with a list of events that
 * both sides support.
 *
 * RFC  4733
 */

static FsCodec *
sdp_negotiate_codec_telephone_event (
    FsCodec *local_codec, FsParamType local_paramtypes,
    FsCodec *remote_codec, FsParamType remote_paramtypes,
    const struct SdpNegoFunction *nf)
{
  FsCodec *negotiated_codec = NULL;
  GList *local_param_e = NULL, *negotiated_param_e = NULL;

  GST_LOG ("Using telephone-event codec negotiation function");

  if (local_codec->clock_rate && remote_codec->clock_rate &&
      local_codec->clock_rate != remote_codec->clock_rate)
  {
    GST_LOG ("Clock rates differ local=%u remote=%u", local_codec->clock_rate,
        remote_codec->clock_rate);
    return NULL;
  }

  negotiated_codec = codec_copy_without_config (remote_codec);

  negotiated_codec->ABI.ABI.ptime = local_codec->ABI.ABI.ptime;
  negotiated_codec->ABI.ABI.maxptime = local_codec->ABI.ABI.maxptime;

  /* Lets fix here missing clock rates and channels counts */
  if (negotiated_codec->channels == 0 && local_codec->channels)
    negotiated_codec->channels = local_codec->channels;
  if (negotiated_codec->clock_rate == 0)
    negotiated_codec->clock_rate = local_codec->clock_rate;

  for (local_param_e = local_codec->optional_params;
       local_param_e;
       local_param_e = g_list_next (local_param_e))
  {
    FsCodecParameter *local_param = local_param_e->data;
    gboolean got_events = FALSE;

    for (negotiated_param_e = negotiated_codec->optional_params;
         negotiated_param_e;
         negotiated_param_e = g_list_next (negotiated_param_e))
    {
      FsCodecParameter *negotiated_param = negotiated_param_e->data;

      if (!strcmp (negotiated_param->name, ""))
      {
        g_free (negotiated_param->name);
        negotiated_param->name = g_strdup ("events");
      }

      if (!g_ascii_strcasecmp (local_param->name, negotiated_param->name))
      {
        if (!strcmp (local_param->value, negotiated_param->value))
        {
          break;
        }
        else if (!g_ascii_strcasecmp (negotiated_param->name, "events"))
        {
          gchar *events = event_intersection (negotiated_param->value,
              local_param->value);
          if (!events)
          {
            GST_LOG ("Non-intersecting values for %s, local=%s remote=%s",
                local_param->name, local_param->value, negotiated_param->value);
            fs_codec_destroy (negotiated_codec);
            return NULL;
          }
          g_free (negotiated_param->value);
          negotiated_param->value = events;
          got_events = TRUE;
        }
        else
        {
          GST_LOG ("Different values for %s, local=%s remote=%s",
              local_param->name, local_param->value, negotiated_param->value);
          fs_codec_destroy (negotiated_codec);
          return NULL;
        }
      }
    }

    /* Let's add the local param to the negotiated codec if it does not exist in
     * the remote codec */
    if (!negotiated_param_e && !got_events)
      fs_codec_add_optional_parameter (negotiated_codec, local_param->name,
          local_param->value);
  }

  return negotiated_codec;
}

/**
 * param_min_max:
 *
 * Expects both parameters to have numerical values.
 * If the type is known, verifies that is is valid. If it is, puts the
 * minimum or maximum depending on the gboolean value in the result.
 */

static gboolean
param_min_max (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec, gboolean min)
{
  guint local_value = 0;
  gboolean local_valid = FALSE;
  guint remote_value = 0;
  gboolean remote_valid = FALSE;
  gchar *encoding_name =
    remote_codec ? remote_codec->encoding_name : local_codec->encoding_name;
  gchar *param_name =
    remote_param ? remote_param->name : local_param->name;
  int i;

  if (local_param)
  {
    local_value = strtol (local_param->value, NULL, 10);
    if (local_value || errno != EINVAL)
      local_valid = TRUE;
  }

  if (remote_param)
  {
    remote_value = strtol (remote_param->value, NULL, 10);
    if (remote_value || errno != EINVAL)
      remote_valid = TRUE;
  }

  /* If the remote value is invalid, lets ignore it entirely */
  if (!remote_valid)
    return TRUE;

  /* Validate values against min/max from table */
  for (i = 0; sdp_min_max_params[i].encoding_name; i++)
  {
    if (!g_ascii_strcasecmp (encoding_name,
            sdp_min_max_params[i].encoding_name) &&
        !g_ascii_strcasecmp (param_name,
            sdp_min_max_params[i].param_name))
    {
      if (local_valid && (local_value < sdp_min_max_params[i].min ||
              local_value > sdp_min_max_params[i].max))
        local_valid = FALSE;

      if (remote_valid && (remote_value < sdp_min_max_params[i].min ||
              remote_value > sdp_min_max_params[i].max))
        return TRUE;

      break;
    }
  }

  if (local_valid && remote_valid)
  {
    gchar *tmp = g_strdup_printf ("%d",
        min ? MIN (local_value, remote_value):MAX (local_value, remote_value));

    fs_codec_add_optional_parameter (negotiated_codec, param_name, tmp);
    g_free (tmp);
  }
  else if (remote_valid && !local_valid)
  {
    fs_codec_add_optional_parameter (negotiated_codec, param_name,
        remote_param->value);
  }

  return TRUE;
}

/**
 * param_equal_or_ignore:
 *
 * If both params are equal, it is the result, otherwise they are removed.
 * Otherwise the result is nothing
 */

static gboolean
param_equal_or_ignore (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec)
{
  if (local_param && remote_param &&
      !strcmp (local_param->value, remote_param->value))
    fs_codec_add_optional_parameter (negotiated_codec, remote_param->name,
        remote_param->value);

  return TRUE;
}

/**
 * param_minimum:
 *
 * Expects both parameters to have numerical values.
 * If the type is known, verifies that is is valid. If it is, puts the
 * minimum value in the result.
 */

static gboolean
param_minimum (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec)
{
  return param_min_max (sdp_param, local_codec, local_param, remote_codec,
      remote_param, negotiated_codec, TRUE);
}

/**
 * param_maximum:
 *
 * Expects both parameters to have numerical values.
 * If the type is known, verifies that is is valid. If it is, puts the
 * maximum value in the result.
 */

static gboolean
param_maximum (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec)
{
  return param_min_max (sdp_param, local_codec, local_param, remote_codec,
      remote_param, negotiated_codec, FALSE);
}

/**
 * param_equal_or_reject:
 *
 * Reject the codec if both params are not equal (taking into account the
 * default value if there is one).
 *
 */

static gboolean
param_equal_or_reject (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec)
{
  const gchar *local_value = NULL;
  const gchar *remote_value = NULL;

  if (local_param)
    local_value = local_param->value;
  else if (sdp_param->default_value)
    local_value = sdp_param->default_value;

  if (remote_param)
    remote_value = remote_param->value;
  else if (sdp_param->default_value)
    remote_value = sdp_param->default_value;

  g_return_val_if_fail (local_value && remote_value, FALSE);

  if (strcmp (local_value, remote_value))
  {
    GST_DEBUG ("Local value and remove value differ (%s != %s)", local_value,
        remote_value);
    return FALSE;
  }

  if (remote_param)
    fs_codec_add_optional_parameter (negotiated_codec, remote_param->name,
        remote_param->value);

  return TRUE;
}

/**
 * param_list_commas:
 *
 * Does the intersection of two comma separated lists, returns a list
 * of elements that are in both.
 */

static gboolean
param_list_commas (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec)
{
  gchar **remote_strv = NULL;
  gchar **local_strv = NULL;
  GString *result = NULL;
  gint i;

  /* If one of them does not have it, just ignore it */
  if (!remote_param || !local_param)
    return TRUE;

  remote_strv = g_strsplit (remote_param->value, ",", -1);
  local_strv = g_strsplit (local_param->value, ",", -1);

  for (i = 0; remote_strv[i]; i++)
  {
    gint j;

    for (j = 0; local_strv[j]; j++)
    {
      if (!g_ascii_strcasecmp (remote_strv[i], local_strv[j]))
      {
        if (!result)
          result = g_string_new (remote_strv[i]);
        else
          g_string_append_printf (result, ",%s", remote_strv[i]);
      }
    }
  }

  if (result)
  {
    fs_codec_add_optional_parameter (negotiated_codec, remote_param->name,
        result->str);
    g_string_free (result, TRUE);
  }

  g_strfreev (remote_strv);
  g_strfreev (local_strv);

  return TRUE;
}


/**
 * param_copy:
 *
 * Copies the incoming parameter
 */

static gboolean
param_copy (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec)
{
  if (remote_param)
    fs_codec_add_optional_parameter (negotiated_codec, remote_param->name,
        remote_param->value);
  else if (local_param)
    fs_codec_add_optional_parameter (negotiated_codec, local_param->name,
        local_param->value);

  return TRUE;
}

/**
 * param_ilbc_mode:
 *
 * For iLBC, the mode is 20 is both sides agree on 20, otherwise it is 30.
 *
 * RFC 3952
 */

static gboolean
param_ilbc_mode (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec)
{
  if (local_param && strcmp (local_param->value, "20")
      && strcmp (local_param->value, "30"))
  {
    GST_DEBUG ("local iLBC has mode that is not 20 or 30 but %s",
        local_param->value);
    return FALSE;
  }

  if (remote_param && strcmp (remote_param->value, "20")
      && strcmp (remote_param->value, "30"))
  {
    GST_DEBUG ("remote iLBC has mode that is not 20 or 30 but %s",
        remote_param->value);
    return FALSE;
  }

  /* Only do mode=20 if both have it */
  if (!local_param || !remote_param)
    return TRUE;

  if (!strcmp (local_param->value, "20")
      && !strcmp (remote_param->value, "20"))
    fs_codec_add_optional_parameter (negotiated_codec, "mode", "20");
  else
    fs_codec_add_optional_parameter (negotiated_codec, "mode", "30");

  return TRUE;
}

/**
 * param_h263_1998_custom
 *
 * If the first two params (x/y) are the same, it takes the maximum of the
 * 3rd params.
 */

static gboolean
param_h263_1998_custom (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec)
{
  guint remote_x, remote_y;
  gchar *match_string;
  guint match_len;
  guint final_mpi;
  GList *elem;
  gboolean got_one = FALSE;
  gchar *tmp;

  if (!remote_param || !local_param)
    return TRUE;

  /* Invalid param, can't parse, ignore it */
  if (sscanf (remote_param->value, "%u,%u,%u", &remote_x, &remote_y,
          &final_mpi) != 3)
    return TRUE;

  match_string = g_strdup_printf ("%u,%u,", remote_x, remote_y);
  match_len = strlen (match_string);

  for (elem = local_codec->optional_params; elem; elem = g_list_next (elem))
  {
    FsCodecParameter *local_param = elem->data;
    if (!g_ascii_strcasecmp (local_param->name, remote_param->name))
    {
      if (!strncmp (local_param->value, match_string, match_len))
      {
        guint local_x, local_y, local_mpi;

        if (sscanf (local_param->value, "%u,%u,%u", &local_x, &local_y,
                &local_mpi) == 3 &&
            local_x == remote_x && local_y == remote_y)
        {
          final_mpi = MAX (final_mpi, local_mpi);
          got_one = TRUE;
        }
      }
    }
  }

  g_free (match_string);

  if (got_one)
  {
    tmp = g_strdup_printf ("%u,%u,%u", remote_x, remote_y, final_mpi);
    fs_codec_add_optional_parameter (negotiated_codec, remote_param->name,
        tmp);
    g_free (tmp);
  }

  return TRUE;
}


/**
 * param_h263_1998_cpcf
 *
 * If the first two params (x/y) are the same, it takes the maximum of the
 * 6 other params
 */

static gboolean
param_h263_1998_cpcf (const struct SdpParam *sdp_param,
    FsCodec *local_codec, FsCodecParameter *local_param,
    FsCodec *remote_codec, FsCodecParameter *remote_param,
    FsCodec *negotiated_codec)
{
  guint remote_cd, remote_cf;
  gchar *match_string;
  guint match_len;
  guint final_sqcif, final_qcif, final_cif, final_4cif, final_16cif,
    final_custom;
  GList *elem;
  gboolean got_one = FALSE;
  gchar *tmp;

  if (!remote_param || !local_param)
    return TRUE;

  /* Invalid param, can't parse, ignore it */
  if (sscanf (remote_param->value, "%u,%u,%u,%u,%u,%u,%u,%u",
          &remote_cd, &remote_cf,
          &final_sqcif, &final_qcif, &final_cif, &final_4cif, &final_16cif,
          &final_custom) != 8)
    return TRUE;

  match_string = g_strdup_printf ("%u,%u,", remote_cd, remote_cf);
  match_len = strlen (match_string);

  for (elem = local_codec->optional_params; elem; elem = g_list_next (elem))
  {
    FsCodecParameter *local_param = elem->data;
    if (!g_ascii_strcasecmp (local_param->name, remote_param->name))
    {
      if (!strncmp (local_param->value, match_string, match_len))
      {
        guint local_cd, local_cf, local_sqcif, local_qcif, local_cif,
          local_4cif, local_16cif, local_custom;

        if (sscanf (local_param->value, "%u,%u,%u,%u,%u,%u,%u,%u",
                &local_cd, &local_cf,
                &local_sqcif, &local_qcif, &local_cif, &local_4cif,
                &local_16cif, &local_custom) == 8 &&
            local_cd == remote_cd && local_cf == remote_cf)
        {
          final_sqcif = MAX(final_sqcif, local_sqcif);
          final_qcif = MAX (final_qcif, local_qcif);
          final_cif = MAX(final_cif, local_cif);
          final_4cif = MAX(final_4cif, local_4cif);
          final_16cif = MAX(final_16cif, local_16cif);
          final_custom = MAX(final_custom, local_custom);
          got_one = TRUE;
        }
      }
    }
  }

  g_free (match_string);

  if (got_one)
  {
    tmp = g_strdup_printf ("%u,%u,%u,%u,%u,%u,%u,%u", remote_cd, remote_cf,
        final_sqcif, final_qcif, final_cif, final_4cif, final_16cif,
        final_custom);
    fs_codec_add_optional_parameter (negotiated_codec, remote_param->name,
        tmp);
    g_free (tmp);
  }

  return TRUE;
}

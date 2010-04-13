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
 * This must be kept to the maximum number of config parameters + 1
 */
#define MAX_CONFIG_PARAMS 6

struct SdpCompatCheck {
  FsMediaType media_type;
  const gchar *encoding_name;
  FsCodec * (* sdp_negotiate_codec) (FsCodec *local_codec,
      FsCodec *remote_codec);
  gchar *config_param[MAX_CONFIG_PARAMS];
};


static FsCodec *
sdp_negotiate_codec_default (FsCodec *local_codec, FsCodec *remote_codec);

static FsCodec *
sdp_negotiate_codec_ilbc (FsCodec *local_codec, FsCodec *remote_codec);
static FsCodec *
sdp_negotiate_codec_h263_2000 (FsCodec *local_codec, FsCodec *remote_codec);
static FsCodec *
sdp_negotiate_codec_telephone_event (FsCodec *local_codec,
    FsCodec *remote_codec);

static struct SdpCompatCheck sdp_compat_checks[] = {
  {FS_MEDIA_TYPE_AUDIO, "iLBC", sdp_negotiate_codec_ilbc,
   {NULL}},
  {FS_MEDIA_TYPE_VIDEO, "H263-2000", sdp_negotiate_codec_h263_2000,
   {NULL}},
  {FS_MEDIA_TYPE_AUDIO, "VORBIS", sdp_negotiate_codec_default,
   {"configuration", NULL}},
  {FS_MEDIA_TYPE_VIDEO, "THEORA", sdp_negotiate_codec_default,
   {"configuration", NULL}},
  {FS_MEDIA_TYPE_VIDEO, "H264", sdp_negotiate_codec_default,
   {"sprop-parameter-sets", "sprop-interleaving-depth", "sprop-deint-buf-req",
    "sprop-init-buf-time", "sprop-max-don-diff", NULL}},
  {FS_MEDIA_TYPE_AUDIO, "telephone-event", sdp_negotiate_codec_telephone_event,
   {NULL}},
  {0, NULL, NULL}
};

/*
 * This function currently returns %TRUE if any configuration parameter is there
 * if some codecs require something more complicated, we will need a custom
 * functions for each codec
 */

gboolean
codec_needs_config (FsCodec *codec)
{
  gint i,j;

  g_return_val_if_fail (codec, FALSE);

  for (i = 0; sdp_compat_checks[i].sdp_negotiate_codec; i++)
    if (sdp_compat_checks[i].media_type == codec->media_type &&
        !g_ascii_strcasecmp (sdp_compat_checks[i].encoding_name,
            codec->encoding_name))
    {
      GList *item;
      if (sdp_compat_checks[i].config_param[0] == NULL)
        return FALSE;

      for (item = codec->optional_params; item; item = g_list_next (item))
      {
        FsCodecParameter *param = item->data;
        for (j = 0; sdp_compat_checks[i].config_param[j]; j++)
          if (!g_ascii_strcasecmp (sdp_compat_checks[i].config_param[j],
                  param->name))
            return FALSE;
      }
      return TRUE;
    }

  return FALSE;
}


gboolean
codec_has_config_data_named (FsCodec *codec, const gchar *name)
{
  gint i, j;

  g_return_val_if_fail (codec, FALSE);

  for (i = 0; sdp_compat_checks[i].sdp_negotiate_codec; i++)
    if (sdp_compat_checks[i].media_type == codec->media_type &&
        !g_ascii_strcasecmp (sdp_compat_checks[i].encoding_name,
            codec->encoding_name))
    {
      for (j = 0; sdp_compat_checks[i].config_param[j]; j++)
        if (!g_ascii_strcasecmp (sdp_compat_checks[i].config_param[j], name))
          return TRUE;
      return FALSE;
    }

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

  for (item = copy->optional_params; item;)
  {
    FsCodecParameter *param = item->data;
    GList *next = g_list_next (item);

    if (codec_has_config_data_named (codec, param->name))
      fs_codec_remove_optional_parameter (copy, param);

    item = next;
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
  gint i;

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

  for (i = 0; sdp_compat_checks[i].sdp_negotiate_codec; i++) {
    if (sdp_compat_checks[i].media_type == remote_codec->media_type &&
        !g_ascii_strcasecmp (sdp_compat_checks[i].encoding_name,
            remote_codec->encoding_name))
    {
      return sdp_compat_checks[i].sdp_negotiate_codec (local_codec,
          remote_codec);
    }
  }

  return sdp_negotiate_codec_default (local_codec, remote_codec);
}

static FsCodec *
sdp_negotiate_codec_default (FsCodec *local_codec, FsCodec *remote_codec)
{
  FsCodec *negotiated_codec = NULL;
  GList *local_param_list = NULL, *negotiated_param_list = NULL;

  GST_LOG ("Using default codec negotiation function");

  if (local_codec->clock_rate && remote_codec->clock_rate &&
      local_codec->clock_rate != remote_codec->clock_rate)
  {
    GST_LOG ("Clock rates differ local=%u remote=%u", local_codec->clock_rate,
        remote_codec->clock_rate);
    return NULL;
  }

  if (local_codec->channels && remote_codec->channels &&
      local_codec->channels != remote_codec->channels)
  {
    GST_LOG ("Channel counts differ local=%u remote=%u",
        local_codec->channels,
        remote_codec->channels);
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

  for (local_param_list = local_codec->optional_params;
       local_param_list;
       local_param_list = g_list_next (local_param_list))
  {
    FsCodecParameter *local_param = local_param_list->data;

    for (negotiated_param_list = negotiated_codec->optional_params;
         negotiated_param_list;
         negotiated_param_list = g_list_next (negotiated_param_list))
    {
      FsCodecParameter *negotiated_param = negotiated_param_list->data;
      if (!g_ascii_strcasecmp (local_param->name, negotiated_param->name))
      {
        if (!strcmp (local_param->value, negotiated_param->value))
        {
          break;
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
    if (!negotiated_param_list)
      fs_codec_add_optional_parameter (negotiated_codec, local_param->name,
          local_param->value);
  }

  return negotiated_codec;
}

/**
 * sdp_negotiate_codec_ilbc:
 *
 * For iLBC, the mode is 20 is both sides agree on 20, otherwise it is 30.
 *
 * RFC 3952
 */

static FsCodec *
sdp_negotiate_codec_ilbc (FsCodec *local_codec, FsCodec *remote_codec)
{
  FsCodec *negotiated_codec = NULL;
  FsCodec *remote_codec_copy = NULL;

  GST_DEBUG ("Using ilbc negotiation function");

  remote_codec_copy = fs_codec_copy (remote_codec);

  if (!fs_codec_get_optional_parameter (remote_codec_copy, "mode", NULL))
    fs_codec_add_optional_parameter (remote_codec_copy, "mode", "30");

  negotiated_codec =  sdp_negotiate_codec_default (local_codec,
      remote_codec_copy);

  fs_codec_destroy (remote_codec_copy);

  return negotiated_codec;
}

/*
 * sdp_negotiate_codec_h263_2000:
 *
 * For H263-2000, the "profile" must be exactly the same. If it is not,
 * it must be rejected.
 *
 * RFC 4629
 */

static FsCodec *
sdp_negotiate_codec_h263_2000 (FsCodec *local_codec, FsCodec *remote_codec)
{
  GList *mylistitem = NULL, *remote_param_list = NULL;
  FsCodecParameter *profile = NULL;

  GST_DEBUG ("Using H263-2000 negotiation function");

  if (remote_codec->clock_rate != 90000)
  {
    GST_WARNING ("Remote clock rate is %d which is not 90000",
        remote_codec->clock_rate);
    return NULL;
  }


  if (remote_codec->channels > 1)
  {
    GST_WARNING ("Channel count  %d > 1", remote_codec->channels);
    return NULL;
  }

  /* First lets check if there is a profile, it MUST be the same
   * as ours
   */

  for (remote_param_list = remote_codec->optional_params;
       remote_param_list;
       remote_param_list = g_list_next (remote_param_list))
  {
    FsCodecParameter *remote_param = remote_param_list->data;

    if (!g_ascii_strcasecmp (remote_param->name, "profile"))
    {

      if (profile)
      {
        GST_WARNING ("The remote codecs contain the profile item more than"
            " once, ignoring");
        return NULL;
      }
      else
      {
        profile = remote_param;
      }

      for (mylistitem = local_codec->optional_params;
           mylistitem;
           mylistitem = g_list_next (mylistitem))
      {
        FsCodecParameter *local_param = mylistitem->data;

        if (!g_ascii_strcasecmp (local_param->name, "profile"))
        {

          if (g_ascii_strcasecmp (local_param->value, remote_param->value))
          {
            GST_LOG ("Local (%s) and remote (%s) profiles are different",
                local_param->value, remote_param->value);
            return NULL;
          }
          else
          {
            GST_LOG ("We have the same profile, lets return the remote codec");
            return fs_codec_copy (local_codec);
          }
        }
      }
        GST_DEBUG ("Profile (%s) is unknown locally, rejecting",
            remote_param->value);
            return NULL;
    }
  }


  return fs_codec_copy (remote_codec);
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
sdp_negotiate_codec_telephone_event (FsCodec *local_codec,
    FsCodec *remote_codec)
{
  FsCodec *negotiated_codec = NULL;
  GList *local_param_list = NULL, *negotiated_param_list = NULL;

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

  for (local_param_list = local_codec->optional_params;
       local_param_list;
       local_param_list = g_list_next (local_param_list))
  {
    FsCodecParameter *local_param = local_param_list->data;
    gboolean got_events = FALSE;

    for (negotiated_param_list = negotiated_codec->optional_params;
         negotiated_param_list;
         negotiated_param_list = g_list_next (negotiated_param_list))
    {
      FsCodecParameter *negotiated_param = negotiated_param_list->data;

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
    if (!negotiated_param_list && !got_events)
      fs_codec_add_optional_parameter (negotiated_codec, local_param->name,
          local_param->value);
  }

  return negotiated_codec;
}

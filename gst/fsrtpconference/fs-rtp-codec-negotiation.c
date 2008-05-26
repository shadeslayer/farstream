/*
 * Farsight2 - Farsight RTP Codec Negotiation
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-discover-codecs.h - A Farsight RTP Codec Negotiation
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

#include "fs-rtp-codec-negotiation.h"
#include "fs-rtp-specific-nego.h"
#include "fs-rtp-conference.h"

#include <string.h>

#define GST_CAT_DEFAULT fsrtpconference_nego

static CodecAssociation *
lookup_codec_association_by_pt_list (GList *codec_associations, gint pt,
    gboolean want_empty);

static CodecAssociation *
codec_association_copy (CodecAssociation *ca);

static CodecAssociation *
lookup_codec_association_custom_intern (GList *codec_associations,
    gboolean want_disabled, CAFindFunc func, gpointer user_data);

/**
 * validate_codecs_configuration:
 * @media_type: The #FsMediaType these codecs should be for
 * @blueprints: A #GList of #CodecBlueprints to validate the codecs agsint
 * @codecs: a #GList of #FsCodec that represent the preferences
 *
 * This function validates a GList of passed FarsightCodec structures
 * against the valid discovered payloaders
 * It removes all "invalid" codecs from the list, it modifies the list
 * passed in as an argument.
 *
 * Returns: the #GList of #FsCodec minus the invalid ones
 */
GList *
validate_codecs_configuration (FsMediaType media_type, GList *blueprints,
  GList *codecs)
{
  GList *codec_e = codecs;

  while (codec_e)
  {
    FsCodec *codec = codec_e->data;
    GList *blueprint_e = NULL;

    /* Check if codec is for the wrong media_type.. this would be wrong
     */
    if (media_type != codec->media_type)
      goto remove_this_codec;

    if (codec->id >= 0 && codec->id < 128 && codec->encoding_name &&
        !g_ascii_strcasecmp (codec->encoding_name, "reserve-pt"))
      goto accept_codec;


    for (blueprint_e = g_list_first (blueprints);
         blueprint_e;
         blueprint_e = g_list_next (blueprint_e))
    {
      CodecBlueprint *blueprint = blueprint_e->data;
      GList *codecparam_e = NULL;

      /* First, lets check the encoding name */
      if (g_ascii_strcasecmp (blueprint->codec->encoding_name,
              codec->encoding_name))
        continue;
      /* If both have a clock_rate, it must be the same */
      if (blueprint->codec->clock_rate && codec->clock_rate &&
          blueprint->codec->clock_rate != codec->clock_rate)
        continue;
        /* At least one needs to have a clockrate */
      else if (!blueprint->codec->clock_rate && !codec->clock_rate)
        continue;

      /* Now lets check that all params that are present in both
       * match
       */
      for (codecparam_e = codec->optional_params;
           codecparam_e;
           codecparam_e = g_list_next (codecparam_e))
      {
        FsCodecParameter *codecparam = codecparam_e->data;
        GList *bpparam_e = NULL;
        for (bpparam_e = blueprint->codec->optional_params;
             bpparam_e;
             bpparam_e = g_list_next (bpparam_e))
        {
          FsCodecParameter *bpparam = bpparam_e->data;
          if (!g_ascii_strcasecmp (codecparam->name, bpparam->name))
          {
            /* If the blueprint and the codec specify the value
             * of a parameter, they should be the same
             */
            if (g_ascii_strcasecmp (codecparam->value, bpparam->value))
              goto next_blueprint;
            break;
          }
        }
      }
      break;
    next_blueprint:
      continue;
    }

    /* If no blueprint was found */
    if (blueprint_e == NULL)
      goto remove_this_codec;

  accept_codec:
    codec_e = g_list_next (codec_e);

    continue;
 remove_this_codec:
    {
      GList *nextcodec_e = g_list_next (codec_e);
      gchar *tmp = fs_codec_to_string (codec);
      GST_DEBUG ("Preferred codec %s could not be matched with a blueprint",
          tmp);
      g_free (tmp);
      fs_codec_destroy (codec);
      codecs = g_list_delete_link (codecs, codec_e);
      codec_e = nextcodec_e;
    }
  }

  return codecs;
}


static void
_codec_association_destroy (CodecAssociation *ca)
{
  if (!ca)
    return;

  fs_codec_destroy (ca->codec);
  g_slice_free (CodecAssociation, ca);
}


static CodecBlueprint *
_find_matching_blueprint (FsCodec *codec, GList *blueprints)
{
  GList *item = NULL;
  GstCaps *caps = NULL;

  caps = fs_codec_to_gst_caps (codec);

  if (!caps)
  {
    gchar *tmp = fs_codec_to_string (codec);
    GST_WARNING ("Could not transform codec into caps: %s", tmp);
    g_free (tmp);
    return NULL;
  }

  for (item = g_list_first (blueprints); item; item = g_list_next (item))
  {
    CodecBlueprint *bp = item->data;
    GstCaps *intersectedcaps = NULL;
    gboolean ok = FALSE;

    intersectedcaps = gst_caps_intersect (caps, bp->rtp_caps);

    if (!gst_caps_is_empty (intersectedcaps))
      ok = TRUE;

    gst_caps_unref (intersectedcaps);

    if (ok)
      break;
  }

  gst_caps_unref (caps);

  if (item)
    return item->data;
  else
    return NULL;
}

static gint
_find_first_empty_dynamic_entry (
    GList *new_codec_associations,
    GList *old_codec_associations)
{
  int id;

  for (id = 96; id < 128; id++)
  {
    if (lookup_codec_association_by_pt_list (new_codec_associations, id, TRUE))
      continue;
    if (lookup_codec_association_by_pt_list (old_codec_associations, id, TRUE))
      continue;
    return id;
  }

  return -1;
}

static gboolean
_is_disabled (GList *codec_prefs, CodecBlueprint *bp)
{
  GList *item = NULL;

  for (item = g_list_first (codec_prefs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    GstCaps *intersectedcaps = NULL;
    GstCaps *caps = NULL;
    gboolean ok = FALSE;

    /* Only check for DISABLE entries */
    if (codec->id != FS_CODEC_ID_DISABLE)
      continue;

    caps = fs_codec_to_gst_caps (codec);
    if (!caps)
      continue;

    intersectedcaps = gst_caps_intersect (caps, bp->rtp_caps);

    if (!gst_caps_is_empty (intersectedcaps))
      ok = TRUE;

    gst_caps_unref (intersectedcaps);
    gst_caps_unref (caps);

    if (ok)
      return TRUE;
  }

  return FALSE;
}

/*
 * This function should return TRUE if the codec pref is a "base" of the
 * negotiated codec, but %FALSE otherwise.
 */

static gboolean
match_original_codec_and_codec_pref (CodecAssociation *ca, gpointer user_data)
{
  FsCodec *codec_pref = user_data;
  FsCodec *tmpcodec = NULL;

  tmpcodec = sdp_is_compat (codec_pref, ca->codec);

  if (tmpcodec)
    fs_codec_destroy (tmpcodec);

  return (tmpcodec != NULL);
}

/**
 * create_local_codec_associations:
 * @blueprints: The #GList of CodecBlueprint
 * @codec_pref: The #GList of #FsCodec representing codec preferences
 * @current_codec_associations: The #GList of current #CodecAssociation
 *
 * This function creates a list of codec associations from installed codecs
 * and the preferences. It also takes into account the currently negotiated
 * codecs to keep the same payload types and optional parameters.
 *
 * Returns: a #GList of #CodecAssociation
 */

GList *
create_local_codec_associations (
    GList *blueprints,
    GList *codec_prefs,
    GList *current_codec_associations)
{
  GList *codec_associations = NULL;
  GList *bp_e = NULL;
  GList *codec_pref_e = NULL;
  GList *lca_e = NULL;

  if (blueprints == NULL)
    return NULL;

  /* First, lets create the original table by looking at our preferred codecs */
  for (codec_pref_e = codec_prefs;
       codec_pref_e;
       codec_pref_e = g_list_next (codec_pref_e))
  {
    FsCodec *codec_pref = codec_pref_e->data;
    CodecBlueprint *bp = _find_matching_blueprint (codec_pref, blueprints);
    CodecAssociation *ca = NULL;
    GList *bp_param_e = NULL;

    /* If its a negative pref, ignore it in this stage */
    if (codec_pref->id == FS_CODEC_ID_DISABLE)
      continue;

    /* If we want to disable a codec ID, we just insert a NULL in the table */
    if (codec_pref->id >= 0 && codec_pref->id < 128 &&
        codec_pref->encoding_name &&
        !g_ascii_strcasecmp (codec_pref->encoding_name, "reserve-pt"))
    {
      CodecAssociation *ca = g_slice_new0 (CodecAssociation);
      ca->codec = fs_codec_copy (codec_pref);
      ca->reserved = TRUE;
      codec_associations = g_list_append (codec_associations, ca);
      continue;
    }

    /* No matching blueprint, can't use this codec */
    if (!bp)
    {
      GST_LOG ("Could not find matching blueprint for preferred codec %s/%s",
          fs_media_type_to_string (codec_pref->media_type),
          codec_pref->encoding_name);
      continue;
    }

    /* Now lets see if there is an existing codec that matches this preference
     */

    {
      CodecAssociation *oldca = NULL;

      if (codec_pref->id == FS_CODEC_ID_ANY)
      {
        oldca = lookup_codec_association_custom_intern (
            current_codec_associations, TRUE,
            match_original_codec_and_codec_pref, codec_pref);
      }
      else
      {
        oldca = lookup_codec_association_by_pt_list (current_codec_associations,
            codec_pref->id, FALSE);
        if (oldca->reserved)
          oldca = NULL;
      }

      /* In this case, we have a matching codec association, lets keep it */
      if (oldca)
      {
        FsCodec *codec = sdp_is_compat (codec_pref, oldca->codec);
        if (codec)
        {
          ca = g_slice_new (CodecAssociation);
          memcpy (ca, oldca, sizeof (CodecAssociation));
          ca->codec = codec;
          codec_associations = g_list_append (codec_associations, ca);
          continue;
        }
      }
    }

    ca = g_slice_new0 (CodecAssociation);
    ca->blueprint = bp;
    ca->codec = fs_codec_copy (codec_pref);

    /* Codec pref does not come with a number, but
     * The blueprint has its own id, lets use it */
    if (ca->codec->id == FS_CODEC_ID_ANY &&
        (bp->codec->id >= 0 || bp->codec->id < 128))
      ca->codec->id = bp->codec->id;

    if (ca->codec->clock_rate == 0)
      ca->codec->clock_rate = bp->codec->clock_rate;

    if (ca->codec->channels == 0)
      ca->codec->channels = bp->codec->channels;

    for (bp_param_e = bp->codec->optional_params;
         bp_param_e;
         bp_param_e = g_list_next (bp_param_e))
    {
      GList *pref_param_e = NULL;
      FsCodecParameter *bp_param = bp_param_e->data;
      for (pref_param_e = ca->codec->optional_params;
           pref_param_e;
           pref_param_e = g_list_next (pref_param_e))
      {
        FsCodecParameter *pref_param = pref_param_e->data;
        if (!g_ascii_strcasecmp (bp_param->name, pref_param->name))
          break;
      }
      if (!pref_param_e)
        fs_codec_add_optional_parameter (ca->codec, bp_param->name,
            bp_param->value);
    }

    {
      gchar *tmp = fs_codec_to_string (ca->codec);
      GST_LOG ("Added preferred codec %s", tmp);
      g_free (tmp);
    }

    codec_associations = g_list_append (codec_associations, ca);
  }

  /* Now, only codecs with specified ids are here,
   * the rest are dynamic
   * Lets attribute them here */
  for (lca_e = codec_associations;
       lca_e;
       lca_e = g_list_next (lca_e))
  {
    CodecAssociation *lca = lca_e->data;

    if (lookup_codec_association_by_pt_list (current_codec_associations,
            lca->codec->id, TRUE) ||
        lca->codec->id < 0)
    {
      lca->codec->id = _find_first_empty_dynamic_entry (
          current_codec_associations, codec_associations);
      if (lca->codec->id < 0)
      {
        GST_ERROR ("We've run out of dynamic payload types");
        goto error;
      }
    }
  }

  /* Now, lets add all other codecs from the blueprints */
  for (bp_e = g_list_first (blueprints); bp_e; bp_e = g_list_next (bp_e)) {
    CodecBlueprint *bp = bp_e->data;
    CodecAssociation *ca = NULL;
    GList *tmpca_e;

    /* Lets skip codecs that dont have all of the required informations */
    if (bp->codec->clock_rate == 0)
      continue;

    /* Check if its already used */
    for (tmpca_e = codec_associations;
         tmpca_e;
         tmpca_e = g_list_next (tmpca_e))
    {
      CodecAssociation *tmpca = tmpca_e->data;
      if (tmpca->blueprint == bp)
        break;
    }
    if (tmpca_e)
      continue;

    /* Check if it is disabled in the list of preferred codecs */
    if (_is_disabled (codec_prefs, bp))
    {
      gchar *tmp = fs_codec_to_string (bp->codec);
      GST_DEBUG ("Codec %s disabled by config", tmp);
      g_free (tmp);
      continue;
    }

    ca = g_slice_new0 (CodecAssociation);
    ca->blueprint = bp;
    ca->codec = fs_codec_copy (bp->codec);

    if (ca->codec->id < 0)
    {
      ca->codec->id = _find_first_empty_dynamic_entry (
          current_codec_associations, codec_associations);
      if (ca->codec->id < 0)
      {
        GST_WARNING ("We've run out of dynamic payload types");
        goto error;
      }
    }

    codec_associations = g_list_append (codec_associations, ca);
  }

  return codec_associations;

 error:
  codec_association_list_destroy (codec_associations);

  return NULL;
}

/**
 * negotiate_stream_codecs:
 * @remote_codecs: Remote codecs for the stream
 * @current_codec_assocations: The current list of #CodecAssociation
 * @use_local_ids: Whether to use local or remote PTs if they dont match (%TRUE
 *  for local, %FALSE for remote)
 *
 * This function performs codec negotiation for a single stream. It does an
 * intersection of the current codecs and the remote codecs.
 *
 * Returns: a #GList of #CodecAssociation
 */

GList *
negotiate_stream_codecs (
    const GList *remote_codecs,
    GList *current_codec_associations,
    gboolean use_local_ids)
{
  GList *new_codec_associations = NULL;
  const GList *rcodec_e = NULL;
  GList *item = NULL;

  for (rcodec_e = remote_codecs;
       rcodec_e;
       rcodec_e = g_list_next (rcodec_e)) {
    FsCodec *remote_codec = rcodec_e->data;
    FsCodec *nego_codec = NULL;
    CodecAssociation *ca = NULL;

    gchar *tmp = fs_codec_to_string (remote_codec);
    GST_DEBUG ("Remote codec %s", tmp);
    g_free (tmp);

    /* First lets try the codec that is in the same PT */

    ca = lookup_codec_association_by_pt_list (current_codec_associations,
        remote_codec->id, FALSE);

    if (ca) {
      GST_DEBUG ("Have local codec in the same PT, lets try it first");
      nego_codec = sdp_is_compat (ca->codec, remote_codec);
    }

    if (!nego_codec) {
      GList *item = NULL;

      for (item = current_codec_associations;
           item;
           item = g_list_next (item))
      {
        ca = item->data;

        nego_codec = sdp_is_compat (ca->codec, remote_codec);

        if (nego_codec)
        {
          nego_codec->id = ca->codec->id;
          break;
        }
      }
    }

    if (nego_codec) {
      CodecAssociation *new_ca = g_slice_new0 (CodecAssociation);
      gchar *tmp;

      new_ca->codec = nego_codec;
      new_ca->blueprint = ca->blueprint;
      tmp = fs_codec_to_string (nego_codec);
      GST_DEBUG ("Negotiated codec %s", tmp);
      g_free (tmp);

      new_codec_associations = g_list_append (new_codec_associations,
          new_ca);
    } else {
      gchar *tmp = fs_codec_to_string (remote_codec);
      CodecAssociation *ca = g_slice_new0 (CodecAssociation);
      GST_DEBUG ("Could not find a valid intersection... for codec %s",
          tmp);
      g_free (tmp);

      ca->codec = fs_codec_copy (remote_codec);
      ca->disable = TRUE;

      new_codec_associations = g_list_append (new_codec_associations, ca);
    }
  }

  /*
   * Check if there is a non-disabled codec left
   */
  for (item = new_codec_associations;
       item;
       item = g_list_next (item))
  {
    CodecAssociation *ca = item->data;

    if (!ca->disable && !ca->reserved && !ca->recv_only)
      return new_codec_associations;
  }

  /* Else we destroy when and return NULL.. ie .. an error */
  codec_association_list_destroy (new_codec_associations);

  return NULL;
}

/**
 * finish_codec_negotiation:
 * @old_codec_associations: The previous list of negotiated #CodecAssociation
 * @new_codec_associations: The new list of negotiated #CodecAssociation,
 *   will be modified by the negotiation
 *
 * This function performs the last step of the codec negotiation after the
 * intersection will all of the remote codecs has been done. It will keep
 * old codecs in case the other end does the non-standard thing and sends
 * using the PT we offered instead of using the negotiated result.
 * It also adds a marker to the list for every previously disabled codec so
 * they're not re-used.
 *
 * Returns: a modified list of #CodecAssociation
 */

GList *
finish_codec_negotiation (
    GList *old_codec_associations,
    GList *new_codec_associations)
{
  int i;

  /* Now, lets fill all of the PTs that were previously used in the session
   * even if they are not currently used, so they can't be re-used
   */

  for (i=0; i < 128; i++)
  {
    CodecAssociation *local_ca = NULL;

    /* We can skip ids where something already exists */
    if (lookup_codec_association_by_pt_list (new_codec_associations, i, TRUE))
      continue;

    /* We check if our local table (our offer) and if we offered
     * something, we add it. Some broken implementation (like Tandberg's)
     * send packets on PTs that they did not put in their response
     */
    local_ca = lookup_codec_association_by_pt_list (old_codec_associations,
        i, FALSE);
    if (local_ca) {
      CodecAssociation *new_ca = codec_association_copy (local_ca);
      new_ca->recv_only = TRUE;
      new_codec_associations = g_list_append (new_codec_associations, new_ca);
    }
  }

  return new_codec_associations;
}


static CodecAssociation *
lookup_codec_association_by_pt_list (GList *codec_associations, gint pt,
                                     gboolean want_disabled)
{
  while (codec_associations)
  {
    if (codec_associations->data)
    {
      CodecAssociation *ca = codec_associations->data;
      if (ca->codec->id == pt &&
          (want_disabled || (!ca->disable && !ca->reserved)))
        return ca;
    }
    codec_associations = g_list_next (codec_associations);
  }

  return NULL;
}


/**
 * lookup_codec_association_by_codec:
 * @codec_associations: a #GList of CodecAssociation
 * @pt: a payload-type number
 *
 * Finds the first #CodecAssociation that matches the payload type
 *
 * Returns: a #CodecAssociation
 */

CodecAssociation *
lookup_codec_association_by_pt (GList *codec_associations, gint pt)
{
  return lookup_codec_association_by_pt_list (codec_associations, pt, FALSE);
}

/**
 * lookup_codec_association_by_codec:
 * @codec_associations: a #GList of #CodecAssociation
 * @codec: The #FsCodec to look for
 *
 * Finds the first #CodecAssociation that matches the #FsCodec
 *
 * Returns: a #CodecAssociation
 */

CodecAssociation *
lookup_codec_association_by_codec (GList *codec_associations, FsCodec *codec)
{
  while (codec_associations)
  {
    if (codec_associations->data)
    {
      CodecAssociation *ca = codec_associations->data;
      if (fs_codec_are_equal (ca->codec, codec))
        return ca;
    }
    codec_associations = g_list_next (codec_associations);
  }

  return NULL;
}

/**
 * codec_association_list_destroy:
 * @list: a #GList of #CodecAssociation
 *
 * Frees a #GList of #CodecAssociation
 */

void
codec_association_list_destroy (GList *list)
{
  g_list_foreach (list, (GFunc) _codec_association_destroy, NULL);
  g_list_free (list);
}


static CodecAssociation *
codec_association_copy (CodecAssociation *ca)
{
  CodecAssociation *newca = g_slice_new (CodecAssociation);

  g_return_val_if_fail (ca, NULL);

  memcpy (newca, ca, sizeof(CodecAssociation));
  newca->codec = fs_codec_copy (ca->codec);

  return newca;
}

/**
 * codec_associations_to_codecs:
 * @codec_associations: a #GList of #CodecAssociation
 *
 * Returns a #GList of the #FsCodec that are inside the list of associations
 * excluding those that are disabled or otherwise receive-only. It copies
 * the #FsCodec structures.
 *
 * Returns: a #GList of #FsCodec
 */
GList *
codec_associations_to_codecs (GList *codec_associations)
{
  GList *codecs = NULL;
  GList *item = NULL;

  for (item = g_list_first (codec_associations);
       item;
       item = g_list_next (item))
  {
    CodecAssociation *ca = item->data;
    if (!ca->disable && !ca->reserved && !ca->recv_only && ca->codec)
    {
      codecs = g_list_append (codecs,
          fs_codec_copy (ca->codec));
    }
  }

  return codecs;
}

gboolean
codec_association_is_valid_for_sending (CodecAssociation *ca)
{
  if (!ca->disable &&
      !ca->reserved &&
      !ca->recv_only &&
      ca->blueprint && ca->blueprint->send_pipeline_factory)
    return TRUE;
  else
    return FALSE;
}


static CodecAssociation *
lookup_codec_association_custom_intern (GList *codec_associations,
    gboolean want_disabled, CAFindFunc func, gpointer user_data)
{
  GList *item;

  g_return_val_if_fail (func, NULL);

  for (item = codec_associations;
       item;
       item = g_list_next (item))
  {
    CodecAssociation *ca = item->data;
    if ((ca->disable && !want_disabled) || ca->reserved)
      continue;

    if (func (ca, user_data))
      return ca;
  }

  return NULL;
}


CodecAssociation *
lookup_codec_association_custom (GList *codec_associations,
    CAFindFunc func, gpointer user_data)
{

  return lookup_codec_association_custom_intern (codec_associations, FALSE,
      func, user_data);
}


/**
 * codec_association_list_are_equal
 * @list1: a #GList of #FsCodec
 * @list2: a #GList of #FsCodec
 *
 * Compares the non-disabled #FsCodec of two lists of #CodecAssociation
 *
 * Returns: TRUE if they are identical, FALSE otherwise
 */

gboolean
codec_associations_list_are_equal (GList *list1, GList *list2)
{
  for (;list1 && list2;
       list1 = g_list_next (list1), list2 = g_list_next (list2))
  {
    CodecAssociation *ca1 = NULL;
    CodecAssociation *ca2 = NULL;

    /* Skip disabled codecs */
    while (list1) {
      ca1 = list1->data;
      if (!ca1->disable || !ca1->reserved)
        break;
      list1 = g_list_next (list1);
    }
    while (list2) {
      ca2 = list2->data;
      if (!ca2->disable || !ca2->reserved)
        break;
      list2 = g_list_next (list2);
    }

    if (list1 == NULL || list2 == NULL)
      break;

    if (!fs_codec_are_equal (ca1->codec, ca2->codec))
      return FALSE;
  }

  if (list1 == NULL && list2 == NULL)
    return TRUE;
  else
    return FALSE;
}

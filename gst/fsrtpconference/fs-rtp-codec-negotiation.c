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

#define GST_CAT_DEFAULT fsrtpconference_nego

static CodecAssociation *
lookup_codec_association_by_pt_list (GList *codec_associations, gint pt,
    gboolean want_empty);

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

  while (codec_e) {
    FsCodec *codec = codec_e->data;
    GList *blueprint_e = NULL;

    /* Check if codec is for the wrong media_type.. this would be wrong
     */
    if (media_type != codec->media_type) {
      goto remove_this_codec;
    }

    if (codec->id >= 0 && codec->id < 128 && codec->encoding_name &&
        g_ascii_strcasecmp (codec->encoding_name, "reserve-pt"))
      goto accept_codec;


    for (blueprint_e = g_list_first (blueprints);
         blueprint_e;
         blueprint_e = g_list_next (blueprint_e)) {
      CodecBlueprint *blueprint = blueprint_e->data;
      GList *codecparam_e = NULL;

      /* First, lets check the encoding name */
      if (g_ascii_strcasecmp (blueprint->codec->encoding_name,
              codec->encoding_name))
        continue;
      /* If both have a clock_rate, it must be the same */
      if (blueprint->codec->clock_rate && codec->clock_rate &&
          blueprint->codec->clock_rate != codec->clock_rate) {
        continue;
        /* At least one needs to have a clockrate */
      } else if (!blueprint->codec->clock_rate && !codec->clock_rate) {
        continue;
      }

      /* Now lets check that all params that are present in both
       * match
       */
      for (codecparam_e = codec->optional_params;
           codecparam_e;
           codecparam_e = g_list_next (codecparam_e)) {
        FsCodecParameter *codecparam = codecparam_e->data;
        GList *bpparam_e = NULL;
        for (bpparam_e = blueprint->codec->optional_params;
             bpparam_e;
             bpparam_e = g_list_next (bpparam_e)) {
          FsCodecParameter *bpparam = bpparam_e->data;
          if (!g_ascii_strcasecmp (codecparam->name, bpparam->name)) {
            /* If the blueprint and the codec specify the value
             * of a parameter, they should be the same
             */
            if (g_ascii_strcasecmp (codecparam->value, bpparam->value)) {
              goto next_blueprint;
            }
            break;
          }
        }
      }
      break;
    next_blueprint:
      continue;
    }

    /* If no blueprint was found */
    if (blueprint_e == NULL) {
      goto remove_this_codec;
    }

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

  for (item = g_list_first (blueprints); item; item = g_list_next (item)) {
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

  for (id = 96; id < 128; id++) {
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

  for (item = g_list_first (codec_prefs); item; item = g_list_next (item)) {
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
       codec_pref_e = g_list_next (codec_pref_e)) {
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
        g_ascii_strcasecmp (codec_pref->encoding_name, "reserve-pt"))
    {
      CodecAssociation *ca = g_slice_new0 (CodecAssociation);
      ca->codec = fs_codec_copy (codec_pref);
      ca->disable = TRUE;
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

    ca = g_slice_new0 (CodecAssociation);
    ca->blueprint = bp;
    ca->codec = fs_codec_copy (codec_pref);

    /* Codec pref does not come with a number, but
     * The blueprint has its own id, lets use it */
    if (ca->codec->id == FS_CODEC_ID_ANY &&
        (bp->codec->id >= 0 || bp->codec->id < 128)) {
        ca->codec->id = bp->codec->id;
    }

    if (ca->codec->clock_rate == 0) {
      ca->codec->clock_rate = bp->codec->clock_rate;
    }

    if (ca->codec->channels == 0) {
      ca->codec->channels = bp->codec->channels;
    }

    for (bp_param_e = bp->codec->optional_params;
         bp_param_e;
         bp_param_e = g_list_next (bp_param_e)) {
      GList *pref_param_e = NULL;
      FsCodecParameter *bp_param = bp_param_e->data;
      for (pref_param_e = ca->codec->optional_params;
           pref_param_e;
           pref_param_e = g_list_next (pref_param_e)) {
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
        lca->codec->id < 0) {
      lca->codec->id = _find_first_empty_dynamic_entry (
          current_codec_associations, codec_associations);
      if (lca->codec->id < 0) {
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
    if (bp->codec->clock_rate == 0) {
      continue;
    }

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
    if (_is_disabled (codec_prefs, bp)) {
      gchar *tmp = fs_codec_to_string (bp->codec);
      GST_DEBUG ("Codec %s disabled by config", tmp);
      g_free (tmp);
      continue;
    }

    ca = g_slice_new0 (CodecAssociation);
    ca->blueprint = bp;
    ca->codec = fs_codec_copy (bp->codec);

    if (ca->codec->id < 0) {
      ca->codec->id = _find_first_empty_dynamic_entry (
          current_codec_associations, codec_associations);
      if (ca->codec->id < 0) {
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
 * negotiate_codecs:
 * @remote_codecs: The list of remote codecs passed from the other side
 * @local_codec_associations: The list of local codec associations
 * @use_local_ids: Wheter to use local or remote PTs if they dont match (%TRUE
 *  for local, %FALSE for remote)
 * @negotiated_codecs_out: A pointer to a pointer to a #GList where the ordered
 *  GList of negotiated codecs can be stored (its not touched if no codec could
 *  be negotiated)
 *
 * This function performs the codec negotiation.
 *
 * Returns: a #GHashTable of (guint pt) => (CodecAssociation*) or %NULL no codec could be negotiated
 */

GHashTable *
negotiate_codecs (const GList *remote_codecs,
    GHashTable *negotiated_codec_associations,
    GList *local_codec_associations,
    gboolean use_local_ids,
    GList **negotiated_codecs_out)
{
  GHashTable *new_codec_associations = NULL;
  GList *new_negotiated_codecs = NULL;
  const GList *rcodec_e = NULL;
  int i;

  g_return_val_if_fail (remote_codecs, NULL);
  g_return_val_if_fail (local_codec_associations, NULL);

  new_codec_associations = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) _codec_association_destroy);

  for (rcodec_e = remote_codecs;
       rcodec_e;
       rcodec_e = g_list_next (rcodec_e)) {
    FsCodec *remote_codec = rcodec_e->data;
    FsCodec *nego_codec = NULL;
    CodecAssociation *local_ca = NULL;

    gchar *tmp = fs_codec_to_string (remote_codec);
    GST_DEBUG ("Remote codec %s", tmp);
    g_free (tmp);

    /* First lets try the codec that is in the same PT */

    local_ca = lookup_codec_association_by_pt_list (local_codec_associations,
        remote_codec->id, FALSE);

    if (local_ca) {
      GST_DEBUG ("Have local codec in the same PT, lets try it first");
      nego_codec = sdp_is_compat (local_ca->blueprint->rtp_caps,
          local_ca->codec, remote_codec);
    }

    if (!nego_codec) {
      GList *item = NULL;

      for (item = local_codec_associations;
           item;
           item = g_list_next (item))
      {
        local_ca = item->data;

        nego_codec = sdp_is_compat (local_ca->blueprint->rtp_caps,
            local_ca->codec, remote_codec);

        if (nego_codec)
        {
          nego_codec->id = local_ca->codec->id;
          break;
        }
      }
    }

    if (nego_codec) {
      CodecAssociation *new_ca = g_slice_new0 (CodecAssociation);
      gchar *tmp;

      new_ca->codec = nego_codec;
      new_ca->blueprint = local_ca->blueprint;
      tmp = fs_codec_to_string (nego_codec);
      GST_DEBUG ("Negotiated codec %s", tmp);
      g_free (tmp);

      g_hash_table_insert (new_codec_associations,
          GINT_TO_POINTER (remote_codec->id), new_ca);
      new_negotiated_codecs = g_list_append (new_negotiated_codecs,
          fs_codec_copy (new_ca->codec));
    } else {
      gchar *tmp = fs_codec_to_string (remote_codec);
      GST_DEBUG ("Could not find a valid intersection... for codec %s",
                 tmp);
      g_free (tmp);
      g_hash_table_insert (new_codec_associations,
          GINT_TO_POINTER (remote_codec->id), NULL);
    }
  }

  /* If no intersection was found, lets return NULL */
  if (!new_negotiated_codecs)
  {
    g_hash_table_destroy (new_codec_associations);
    return NULL;
  }

  /* Now, lets fill all of the PTs that were previously used in the session
   * even if they are not currently used, so they can't be re-used
   */
  for (i=0; i < 128; i++) {
    CodecAssociation *local_ca = NULL;

    /* We can skip those currently in use */
    if (g_hash_table_lookup_extended (new_codec_associations,
            GINT_TO_POINTER (i), NULL, NULL))
      continue;

    /* We check if our local table (our offer) and if we offered
       something, we add it. Some broken implementation (like Tandberg's)
       send packets on PTs that they did not put in their response
    */
    local_ca = lookup_codec_association_by_pt_list (local_codec_associations,
        i, FALSE);
    if (local_ca) {
      CodecAssociation *new_ca = g_slice_new0 (CodecAssociation);
      new_ca->codec = fs_codec_copy (local_ca->codec);
      new_ca->blueprint = local_ca->blueprint;

      g_hash_table_insert (new_codec_associations,
          GINT_TO_POINTER (i), new_ca);
      /*
       * We dont insert it into the list, because the list is used for offers
       * and answers.. and we shouldn't offer/answer with codecs that
       * were not in the remote codecs
       */
      //new_negotiated_codecs = g_list_append (new_negotiated_codecs, new_ca->codec);
      continue;
    }

    /* We check in our local table (our offer) and in the old negotiated
     * table (the result of previous negotiations). And kill all of the
     * PTs used in there
     */
    if (lookup_codec_association_by_pt_list (local_codec_associations, i, TRUE)
        || (negotiated_codec_associations &&
            g_hash_table_lookup_extended (negotiated_codec_associations,
                GINT_TO_POINTER (i), NULL, NULL))) {
      g_hash_table_insert (new_codec_associations,
          GINT_TO_POINTER (i), NULL);
    }

  }

#if 0
  /*
   * BIG hack, we have to manually add CN
   * because we can send it, but not receive it yet
   * This is because there is no blueprint for them
   */

  if (new_negotiated_codecs) {
    new_negotiated_codecs = add_cn_type (new_negotiated_codecs,
        new_codec_associations);
  }
#endif

  *negotiated_codecs_out = new_negotiated_codecs;
  return new_codec_associations;
}


CodecAssociation *
lookup_codec_association_by_pt (GHashTable *codec_associations, gint pt)
{
  if (!codec_associations)
    return NULL;

  return g_hash_table_lookup (codec_associations, GINT_TO_POINTER (pt));
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
      if (ca->codec->id == pt && (want_disabled || !ca->disable))
        return ca;
    }
    codec_associations = g_list_next (codec_associations);
  }

  return NULL;
}

void
codec_association_list_destroy (GList *list)
{
  g_list_foreach (list, (GFunc) _codec_association_destroy, NULL);
  g_list_free (list);
}

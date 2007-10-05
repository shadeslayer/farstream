/*
 * Farsight2 - Farsight Candidate
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-candidate.c - A Farsight candidate
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "fs-candidate.h"

/**
 * SECTION:FsCandidate
 * @short_description: Structure representing a transport candidate. This
 * description is compatible with ICE-13.
 */

/**
 * fs_candidate_destroy:
 * @cand: a #FsCandidate to delete
 *
 * Frees a #FsCandidate and all its contents
 */
void
fs_candidate_destroy (FsCandidate * cand)
{
  if (cand->candidate_id)
    g_free ((gchar *)cand->candidate_id);
  if (cand->ip)
    g_free ((gchar *)cand->ip);
  if (cand->base_ip)
    g_free ((gchar *)cand->base_ip);
  if (cand->proto_subtype)
    g_free ((gchar *)cand->proto_subtype);
  if (cand->proto_profile)
    g_free ((gchar *)cand->proto_profile);
  if (cand->username)
    g_free ((gchar *)cand->username);
  if (cand->password)
    g_free ((gchar *)cand->password);

  g_free (cand);
}

/**
 * fs_candidate_copy:
 * @cand: a #FsCandidate to copy
 *
 * Copies a #FsCandidate and its contents.
 *
 * Returns: a new #FsCandidate
 */
FsCandidate *
fs_candidate_copy (const FsCandidate * cand)
{
  FsCandidate *copy = g_new0 (FsCandidate, 1);

  copy->component_id = cand->component_id;
  copy->port = cand->port;
  copy->base_port = cand->base_port;
  copy->proto = cand->proto;
  copy->priority = cand->priority;
  copy->type = cand->type;
  copy->foundation = cand->foundation;

  if (cand->candidate_id)
    copy->candidate_id = g_strdup (cand->candidate_id);
  else
    copy->candidate_id = NULL;

  if (cand->ip)
    copy->ip = g_strdup (cand->ip);
  else
    copy->ip = NULL;

  if (cand->base_ip)
    copy->base_ip = g_strdup (cand->base_ip);
  else
    copy->base_ip = NULL;

  if (cand->proto_subtype)
    copy->proto_subtype = g_strdup (cand->proto_subtype);
  else
    copy->proto_subtype = NULL;

  if (cand->proto_profile)
    copy->proto_profile = g_strdup (cand->proto_profile);
  else
    copy->proto_profile = NULL;

  if (cand->username)
    copy->username = g_strdup (cand->username);
  else
    copy->username = NULL;

  if (cand->password)
    copy->password = g_strdup (cand->password);
  else
    copy->password = NULL;

  return copy;
}

/**
 * fs_candidate_list_destroy:
 * @candidate_list: A GList of #FsCandidate
 *
 * Deletes a GList of #FsCandidate and its contents
 */
void
fs_candidate_list_destroy (GList *candidate_list)
{
  GList *lp;
  FsCandidate *cand;

  for (lp = candidate_list; lp; lp = g_list_next (lp)) {
    cand = (FsCandidate *) lp->data;
    fs_candidate_destroy(cand);
    lp->data = NULL;
  }
  g_list_free (candidate_list);
}

/**
 * fs_candidate_list_copy:
 * @candidate_list: A GList of #FsCandidate
 *
 * Copies a GList of #FsCandidate and its contents
 *
 * Returns: a new GList of #FsCandidate
 */
GList *
fs_candidate_list_copy (const GList *candidate_list)
{
  GList *copy = NULL;
  const GList *lp;
  FsCandidate *cand;

  for (lp = candidate_list; lp; lp = g_list_next (lp)) {
    cand = (FsCandidate *) lp->data;
    /* prepend then reverse the list for efficiency */
    copy = g_list_prepend (copy, fs_candidate_copy (cand));
  }
  copy= g_list_reverse (copy);
  return copy;
}

/**
 * fs_candidate_get_by_id:
 * @candidate_list: a list of #FsCandidate
 * @candidate_id: the id of the candidate to extract
 *
 * Searches in candidate_list for the candidate indentified by candidate_id
 *
 * Returns: a #FsCandidate or NULL if not found
 */
FsCandidate *
fs_candidate_get_by_id (const GList *candidate_list,
                        const gchar *candidate_id)
{
  FsCandidate *cand = NULL;
  const GList *lp;
  FsCandidate *cand_copy = NULL;

  for (lp = candidate_list; lp; lp = g_list_next (lp)) {
    cand = (FsCandidate *) lp->data;
    if (g_ascii_strcasecmp(cand->candidate_id, candidate_id) == 0)
    {
      cand_copy = fs_candidate_copy (cand);
      g_print("%p\n", cand_copy);
      break;
    }
  }
  return cand_copy;
}

/**
 * fs_candidate_are_equal:
 * @cand1: first #FsCandidate to compare
 * @cand2: second #FsCandidate to compare
 *
 * Compares two #FsCandidate to see if they are equivalent.
 *
 * Returns: True if equivalent.
 */
gboolean fs_candidate_are_equal (const FsCandidate *cand1,
                                 const FsCandidate *cand2)
{
  /* TODO we compare just the ip and port for now 
   * is this enough ? think about it some more */
  if ((g_ascii_strcasecmp(cand1->ip, cand2->ip) == 0) &&
      (cand1->port == cand2->port))
    return TRUE;
  else
    return FALSE;
}

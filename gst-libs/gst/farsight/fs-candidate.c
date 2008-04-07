/*
 * Farsight2 - Farsight Candidate
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-candidate.c - A Farsight candidate
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

#include "fs-candidate.h"

/**
 * SECTION:fs-candidate
 * @short_description: Structure describing a transport candidate.
 *
 * An FsCandidate is a way to exchange candidate information between the client
 * and Farsight. This description is compatible with ICE-13. It can also be a
 * multicast address. Candidates are linked to streams. The information
 * specified in this structure is usually representative of the codec
 * information exchanged in the signaling.
 */

GType
fs_candidate_get_type (void)
{
  static GType candidate_type = 0;
  if (candidate_type == 0)
  {
    candidate_type = g_boxed_type_register_static (
        "FsCandidate",
        (GBoxedCopyFunc)fs_candidate_copy,
        (GBoxedFreeFunc)fs_candidate_destroy);
  }

  return candidate_type;
}

GType
fs_candidate_list_get_type (void)
{
  static GType candidate_list_type = 0;
  if (candidate_list_type == 0)
  {
    candidate_list_type = g_boxed_type_register_static (
        "FsCandidateList",
        (GBoxedCopyFunc)fs_candidate_list_copy,
        (GBoxedFreeFunc)fs_candidate_list_destroy);
  }

  return candidate_list_type;
}

/**
 * fs_candidate_destroy:
 * @cand: a #FsCandidate to delete
 *
 * Frees a #FsCandidate and all its contents
 */
void
fs_candidate_destroy (FsCandidate * cand)
{
  g_free ((gchar *) cand->foundation);
  g_free ((gchar *) cand->candidate_id);
  g_free ((gchar *) cand->ip);
  g_free ((gchar *) cand->base_ip);
  g_free ((gchar *) cand->username);
  g_free ((gchar *) cand->password);

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
  copy->ttl = cand->ttl;

  copy->foundation = g_strdup (cand->foundation);
  copy->candidate_id = g_strdup (cand->candidate_id);
  copy->ip = g_strdup (cand->ip);
  copy->base_ip = g_strdup (cand->base_ip);
  copy->username = g_strdup (cand->username);
  copy->password = g_strdup (cand->password);

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
    fs_candidate_destroy (cand);
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
 * fs_candidate_new:
 * @id: The id of the candidate (must be unique for ICE)
 * @component_id: The component this candidate is for
 * @type: The type of candidate
 * @proto: The protocol this component is for
 * @ip: The IP address of this component (can be NULL for local candidate to
 *     mean any address)
 * @port: the UDP/TCP port
 *
 * Allocates a new #FsCandidate, the rest of the fields can be optionally
 * filled manually.
 *
 * Returns: a newly-allocated #FsCandidate
 */

FsCandidate *
fs_candidate_new (
    const gchar *id,
    guint component_id,
    FsCandidateType type,
    FsNetworkProtocol proto,
    const gchar *ip,
    guint port)
{
  FsCandidate *candidate = g_new0 (FsCandidate, 1);

  candidate->candidate_id = g_strdup (id);
  candidate->component_id = component_id;
  candidate->type = type;
  candidate->proto = proto;
  candidate->ip = g_strdup (ip);
  candidate->port = port;

  return candidate;
}

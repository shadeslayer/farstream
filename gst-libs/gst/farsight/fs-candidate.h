/*
 * Farsight2 - Farsight Candidate
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-candidate.h - A Farsight candidate
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

#ifndef __FS_CANDIDATE_H__
#define __FS_CANDIDATE_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define FS_TYPE_CANDIDATE \
  (fs_candidate_get_type())


#define FS_TYPE_CANDIDATE_LIST \
  (fs_candidate_list_get_type())

/**
 * FsCandidateType:
 * @FS_CANDIDATE_TYPE_HOST: A host candidate (local)
 * @FS_CANDIDATE_TYPE_SRFLX: A server reflexive candidate.
 * @FS_CANDIDATE_TYPE_PRFLX: A peer reflexive candidate
 * @FS_CANDIDATE_TYPE_RELAY: An relay candidate
 *
 * An enum for the type of candidate used/reported
 */
typedef enum
{
  FS_CANDIDATE_TYPE_HOST,
  FS_CANDIDATE_TYPE_SRFLX,
  FS_CANDIDATE_TYPE_PRFLX,
  FS_CANDIDATE_TYPE_RELAY    /* An external stream relay */
} FsCandidateType;

/**
 * FsNetworkProtocol:
 * @FS_NETWORK_PROTOCOL_UDP: A UDP based protocol
 * @FS_NETWORK_PROTOCOL_TCP: A TCP based protocol
 *
 * An enum for the base IP protocol
 */
typedef enum
{
  FS_NETWORK_PROTOCOL_UDP,
  FS_NETWORK_PROTOCOL_TCP
} FsNetworkProtocol;

typedef struct _FsCandidate FsCandidate;

/**
 * FsCandidate:
 * @candidate_id: string identifier of the candidate
 * @foundation: a string representing the foundation of this candidate (maximum 32 chars)
 * @component_id: value between 1 and 256 indicating which component this candidate represents (1 is RTP, 2 is RTCP)
 * @ip: IP in dotted format
 * @port: Port to use
 * @base_ip: IP of base in dotted format as defined in ICE-19.
 * @base_port: Port of base as defined in ICE-19.
 * @proto: #FsNetworkProtocol for ip protocol to use as candidate
 * @proto_subtype: a string specifying subtype of this protocol type if needed
 * @proto_profile: a string specifying a profile type for this protocol, if applicable
 * @priority: Value between 0 and (2^31 - 1) representing the priority
 * @type: The #FsCandidateType of the candidate
 * @username: Username to use to connect to client if necessary,
 *            NULL otherwise
 * @password: Username to use to connect to client if necessary,
 *            NULL otherwise
 *
 * Struct to hold information about ICE-19 compliant candidates
 */
struct _FsCandidate
{
  /* TODO We need some sort of way for the transmitter to know just from this
   * structure what kind of element to create (RAW udp, STUN udp, ICE, etc).
   * Maybe use FsNetworkProtocol if it is not required by ICE */
  /* TODO Should this be made into a GstStructure? */
  const gchar *candidate_id;
  gchar *foundation;
  guint component_id;
  const gchar *ip;
  guint16 port;
  const gchar *base_ip;
  guint16 base_port;
  FsNetworkProtocol proto;
  const gchar *proto_subtype;
  const gchar *proto_profile;
  gint priority;
  FsCandidateType type;
  const gchar *username;
  const gchar *password;
  /*< private >*/
  gpointer _padding[4];
};

GType fs_candidate_get_type (void);
GType fs_candidate_list_get_type (void);

void fs_candidate_destroy (FsCandidate *cand);

FsCandidate *fs_candidate_copy (const FsCandidate *cand);

void fs_candidate_list_destroy (GList *candidate_list);

GList *fs_candidate_list_copy (const GList *candidate_list);

FsCandidate *fs_candidate_get_by_id (const GList *candidate_list,
                                     const gchar *candidate_id);

gboolean fs_candidate_are_equal (const FsCandidate *cand1,
                                 const FsCandidate *cand2);

G_END_DECLS
#endif /* __FS_CANDIDATE_H__ */

/*
 * Farsight2 - Farsight RTP Participant
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-participant.c - A RTP Farsight Participant gobject
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

/**
 * SECTION:fs-rtp-participant
 * @short_description: A RTP participant in a #FsRtpConference
 *
 * This object represents one participant or person in a RTP conference
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtp-participant.h"

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
};

G_DEFINE_TYPE(FsRtpParticipant, fs_rtp_participant, FS_TYPE_PARTICIPANT);

/*
struct _FsRtpParticipantPrivate
{
};

#define FS_RTP_PARTICIPANT_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_PARTICIPANT, \
   FsRtpParticipantPrivate))
*/

static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_rtp_participant_class_init (FsRtpParticipantClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  // g_type_class_add_private (klass, sizeof (FsRtpParticipantPrivate));
}

static void
fs_rtp_participant_init (FsRtpParticipant *self)
{
  /* member init */
  // self->priv = FS_RTP_PARTICIPANT_GET_PRIVATE (self);
}

FsRtpParticipant *fs_rtp_participant_new (gchar *cname)
{
  return g_object_new (FS_TYPE_RTP_PARTICIPANT, "cname", cname, NULL);
}

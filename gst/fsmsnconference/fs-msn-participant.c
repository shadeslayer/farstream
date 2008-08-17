/*
 * Farsight2 - Farsight MSN Participant
 *
 *  @author: Richard Spiers <richard.spiers@gmail.com>
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-participant.c - A MSN Farsight Participant gobject
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
 * SECTION:fs-msn-participant
 * @short_description: A MSN participant in a #FsMsnConference
 *
 * This object represents one participant or person in a conference
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-msn-participant.h"

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

G_DEFINE_TYPE(FsMsnParticipant, fs_msn_participant, FS_TYPE_PARTICIPANT);


static GObjectClass *parent_class = NULL;


static void
fs_msn_participant_class_init (FsMsnParticipantClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

}

static void
fs_msn_participant_init (FsMsnParticipant *self)
{
}

FsMsnParticipant *fs_msn_participant_new (gchar *cname)
{
  return g_object_new (FS_TYPE_MSN_PARTICIPANT, "cname", cname, NULL);
}

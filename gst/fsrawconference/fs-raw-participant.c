/*
 * Farsight2 - Farsight Raw Participant
 *
 * Copyright 2007,2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Mike Ruprecht <mike.ruprecht@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-raw-participant.c - A Raw Farsight Participant gobject
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
 * SECTION:fs-raw-participant
 * @short_description: A Raw participant in a #FsRawConference
 *
 * This object represents one participant or person in a raw conference
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-raw-participant.h"

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

G_DEFINE_TYPE(FsRawParticipant, fs_raw_participant, FS_TYPE_PARTICIPANT);

/*
struct _FsRawParticipantPrivate
{
};

#define FS_RAW_PARTICIPANT_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_PARTICIPANT, \
   FsRawParticipantPrivate))
*/

// static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_raw_participant_class_init (FsRawParticipantClass *klass)
{
  // g_type_class_add_private (klass, sizeof (FsRawParticipantPrivate));
}

static void
fs_raw_participant_init (FsRawParticipant *self)
{
  /* member init */
  // self->priv = FS_RAW_PARTICIPANT_GET_PRIVATE (self);
}

FsRawParticipant *fs_raw_participant_new (void)
{
  return g_object_new (FS_TYPE_RAW_PARTICIPANT, NULL);
}

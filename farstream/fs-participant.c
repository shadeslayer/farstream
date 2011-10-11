/*
 * Farstream - Farstream Participant
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-participant.c - A Farstream Participant gobject (base implementation)
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
 * SECTION:fs-participant
 * @short_description: A participant in a conference
 *
 * This object is the base implementation of a Farstream Participant. It needs to be
 * derived and implemented by a farstream conference gstreamer element. A
 * participant represents any source of media in a conference. This could be a
 * human-participant or an automaton.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-participant.h"
#include "fs-enumtypes.h"
#include "fs-marshal.h"

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0
};

/*
struct _FsParticipantPrivate
{
};
*/

G_DEFINE_ABSTRACT_TYPE(FsParticipant, fs_participant, GST_TYPE_OBJECT);

#define FS_PARTICIPANT_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_PARTICIPANT, \
   FsParticipantPrivate))

static void fs_participant_finalize (GObject *object);


// static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_participant_class_init (FsParticipantClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fs_participant_finalize;

  // g_type_class_add_private (klass, sizeof (FsParticipantPrivate));
}

static void
fs_participant_init (FsParticipant *self)
{
  //self->priv = FS_PARTICIPANT_GET_PRIVATE (self);
  self->mutex = g_mutex_new ();
}

static void
fs_participant_finalize (GObject *object)
{
  FsParticipant *self = FS_PARTICIPANT (object);
  g_mutex_free (self->mutex);

  G_OBJECT_CLASS (fs_participant_parent_class)->finalize (object);
}

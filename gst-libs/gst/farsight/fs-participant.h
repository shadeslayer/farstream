/*
 * Farsight2 - Farsight Participant
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-participant.h - A Farsight Participant gobject (base implementation)
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

#ifndef __FS_PARTICIPANT_H__
#define __FS_PARTICIPANT_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_PARTICIPANT \
  (fs_participant_get_type())
#define FS_PARTICIPANT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_PARTICIPANT, FsParticipant))
#define FS_PARTICIPANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_PARTICIPANT, FsParticipantClass))
#define FS_IS_PARTICIPANT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_PARTICIPANT))
#define FS_IS_PARTICIPANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_PARTICIPANT))
#define FS_PARTICIPANT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_PARTICIPANT, FsParticipantClass))

typedef struct _FsParticipant FsParticipant;
typedef struct _FsParticipantClass FsParticipantClass;
typedef struct _FsParticipantPrivate FsParticipantPrivate;

struct _FsParticipantClass
{
  GObjectClass parent_class;

  /*virtual functions */

  /*< private >*/
  FsParticipantPrivate *priv;
  gpointer _padding[8];
};

/**
 * FsParticipant:
 *
 */
struct _FsParticipant
{
  GObject parent;
  FsParticipantPrivate *priv;

  /*< private >*/

  gpointer _padding[8];
};

GType fs_participant_get_type (void);

G_END_DECLS

#endif /* __FS_PARTICIPANT_H__ */

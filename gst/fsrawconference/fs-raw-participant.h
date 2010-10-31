/*
 * Farsight2 - Farsight Raw Participant
 *
 * Copyright 2007,2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-raw-participant.h - A Farsight Raw Participant gobject
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

#ifndef __FS_RAW_PARTICIPANT_H__
#define __FS_RAW_PARTICIPANT_H__

#include <gst/farsight/fs-participant.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RAW_PARTICIPANT (fs_raw_participant_get_type())
#define FS_RAW_PARTICIPANT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RAW_PARTICIPANT, \
                              FsRawParticipant))
#define FS_RAW_PARTICIPANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RAW_PARTICIPANT, \
                           FsRawParticipantClass))
#define FS_IS_RAW_PARTICIPANT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RAW_PARTICIPANT))
#define FS_IS_RAW_PARTICIPANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RAW_PARTICIPANT))
#define FS_RAW_PARTICIPANT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RAW_PARTICIPANT, \
                              FsRawParticipantClass))
#define FS_RAW_PARTICIPANT_CAST(obj) ((FsRawParticipant *) (obj))

typedef struct _FsRawParticipant FsRawParticipant;
typedef struct _FsRawParticipantClass FsRawParticipantClass;
typedef struct _FsRawParticipantPrivate FsRawParticipantPrivate;

struct _FsRawParticipantClass
{
  FsParticipantClass parent_class;

  /*virtual functions */

  /*< private >*/
  FsRawParticipantPrivate *priv;
};

/**
 * FsRawParticipant:
 *
 */
struct _FsRawParticipant
{
  FsParticipant parent;
  FsRawParticipantPrivate *priv;

  /*< private >*/
};

GType fs_raw_participant_get_type (void);

FsRawParticipant *fs_raw_participant_new (const gchar *cname);

G_END_DECLS

#endif /* __FS_RAW_PARTICIPANT_H__ */

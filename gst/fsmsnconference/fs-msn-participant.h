/*
 * Farsight2 - Farsight MSN Participant
 *
 *  @author: Richard Spiers <richard.spiers@gmail.com>
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-participant.h - A Farsight MSN Participant gobject
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

#ifndef __FS_MSN_PARTICIPANT_H__
#define __FS_MSN_PARTICIPANT_H__

#include <gst/farsight/fs-participant.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_MSN_PARTICIPANT (fs_msn_participant_get_type())
#define FS_MSN_PARTICIPANT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_MSN_PARTICIPANT, \
                              FsMsnParticipant))
#define FS_MSN_PARTICIPANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_MSN_PARTICIPANT, \
                           FsMsnParticipantClass))
#define FS_IS_MSN_PARTICIPANT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_MSN_PARTICIPANT))
#define FS_IS_MSN_PARTICIPANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_MSN_PARTICIPANT))
#define FS_MSN_PARTICIPANT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_MSN_PARTICIPANT, \
                              FsMsnParticipantClass))
#define FS_Msn_PARTICIPANT_CAST(obj) ((FsMsnParticipant *) (obj))

typedef struct _FsMsnParticipant FsMsnParticipant;
typedef struct _FsMsnParticipantClass FsMsnParticipantClass;
typedef struct _FsMsnParticipantPrivate FsMsnParticipantPrivate;

struct _FsMsnParticipantClass
  {
    FsParticipantClass parent_class;

    /*virtual functions */

    /*< private >*/
    FsMsnParticipantPrivate *priv;
  };

/**
 * FsMsnParticipant:
 *
 */
struct _FsMsnParticipant
  {
    FsParticipant parent;
    FsMsnParticipantPrivate *priv;

  };

GType fs_msn_participant_get_type (void);

FsMsnParticipant *fs_msn_participant_new (gchar *cname);

G_END_DECLS

#endif /* __FS_Msn_PARTICIPANT_H__ */

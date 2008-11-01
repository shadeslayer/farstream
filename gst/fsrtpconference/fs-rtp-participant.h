/*
 * Farsight2 - Farsight RTP Participant
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-participant.h - A Farsight RTP Participant gobject
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

#ifndef __FS_RTP_PARTICIPANT_H__
#define __FS_RTP_PARTICIPANT_H__

#include <gst/farsight/fs-participant.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_PARTICIPANT (fs_rtp_participant_get_type())
#define FS_RTP_PARTICIPANT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_PARTICIPANT, \
                              FsRtpParticipant))
#define FS_RTP_PARTICIPANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_PARTICIPANT, \
                           FsRtpParticipantClass))
#define FS_IS_RTP_PARTICIPANT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_PARTICIPANT))
#define FS_IS_RTP_PARTICIPANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_PARTICIPANT))
#define FS_RTP_PARTICIPANT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_PARTICIPANT, \
                              FsRtpParticipantClass))
#define FS_RTP_PARTICIPANT_CAST(obj) ((FsRtpParticipant *) (obj))

typedef struct _FsRtpParticipant FsRtpParticipant;
typedef struct _FsRtpParticipantClass FsRtpParticipantClass;
typedef struct _FsRtpParticipantPrivate FsRtpParticipantPrivate;

struct _FsRtpParticipantClass
{
  FsParticipantClass parent_class;

  /*virtual functions */

  /*< private >*/
  FsRtpParticipantPrivate *priv;
};

/**
 * FsRtpParticipant:
 *
 */
struct _FsRtpParticipant
{
  FsParticipant parent;
  FsRtpParticipantPrivate *priv;

  /*< private >*/
};

GType fs_rtp_participant_get_type (void);

FsRtpParticipant *fs_rtp_participant_new (const gchar *cname);

G_END_DECLS

#endif /* __FS_RTP_PARTICIPANT_H__ */

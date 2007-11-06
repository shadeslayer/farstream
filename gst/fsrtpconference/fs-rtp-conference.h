/*
 * Farsight2 - Farsight RTP Conference Implementation
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * gstfsrtpconference.h - RTP implementation for Farsight Conference Gstreamer
 *                        Elements
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

#ifndef __FS_RTP_CONFERENCE_H__
#define __FS_RTP_CONFERENCE_H__

#include <gst/farsight/fs-base-conference.h>

G_BEGIN_DECLS

#define FS_TYPE_RTP_CONFERENCE \
  (fs_rtp_conference_get_type())
#define FS_RTP_CONFERENCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),FS_TYPE_RTP_CONFERENCE,FsRtpConference))
#define FS_RTP_CONFERENCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),FS_TYPE_RTP_CONFERENCE,FsRtpConferenceClass))
#define FS_RTP_CONFERENCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),FS_TYPE_RTP_CONFERENCE,FsRtpConferenceClass))
#define GST_IS_RTP_TRANSFORM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),FS_TYPE_RTP_CONFERENCE))
#define GST_IS_RTP_TRANSFORM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),FS_TYPE_RTP_CONFERENCE))
/* since 0.10.4 */
#define FS_RTP_CONFERENCE_CAST(obj) \
  ((FsRtpConference *)(obj))

typedef struct _FsRtpConference FsRtpConference;
typedef struct _FsRtpConferenceClass FsRtpConferenceClass;
typedef struct _FsRtpConferencePrivate FsRtpConferencePrivate;

struct _FsRtpConference
{
  FsBaseConference parent;
  FsRtpConferencePrivate *priv;

  /* Do not modify the pointer */
  GstElement *gstrtpbin;
};

struct _FsRtpConferenceClass
{
  FsBaseConferenceClass parent_class;
};

GType fs_rtp_conference_get_type(void);

G_END_DECLS

#endif /* __FS_RTP_CONFERENCE_H__ */

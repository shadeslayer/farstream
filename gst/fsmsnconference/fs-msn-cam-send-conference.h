/*
 * Farsight2 - Farsight MSN Conference Implementation
 *
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-send-conference.h - MSN implementation for Farsight Conference
 * Gstreamer Elements
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

#ifndef __FS_MSN_CAM_SEND_CONFERENCE_H__
#define __FS_MSN_CAM_SEND_CONFERENCE_H__

#include "fs-msn-conference.h"

G_BEGIN_DECLS

#define FS_TYPE_MSN_CAM_SEND_CONFERENCE (fs_msn_cam_send_conference_get_type ())
#define FS_MSN_CAM_SEND_CONFERENCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_MSN_CAM_SEND_CONFERENCE, \
      FsMsnCamSendConference))
#define FS_MSN_CAM_SEND_CONFERENCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_MSN_CAM_SEND_CONFERENCE, \
      FsMsnCamSendConferenceClass))
#define FS_MSN_CAM_SEND_CONFERENCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), FS_TYPE_MSN_CAM_SEND_CONFERENCE, \
      FsMsnCamSendConferenceClass))
#define FS_IS_MSN_CAM_SEND_CONFERENCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_MSN_CAM_SEND_CONFERENCE))
#define FS_IS_MSN_CAM_SEND_CONFERENCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_MSN_CAM_SEND_CONFERENCE))
#define FS_MSN_CAM_SEND_CONFERENCE_CAST(obj) \
  ((FsMsnCamSendConference *)(obj))

typedef struct _FsMsnCamSendConference FsMsnCamSendConference;
typedef struct _FsMsnCamSendConferenceClass FsMsnCamSendConferenceClass;
typedef struct _FsMsnCamSendConferencePrivate FsMsnCamSendConferencePrivate;

struct _FsMsnCamSendConference
{
  FsMsnConference parent;
};

struct _FsMsnCamSendConferenceClass
{
  FsMsnConferenceClass parent_class;
};

GType fs_msn_cam_send_conference_get_type (void);


G_END_DECLS

#endif /* __FS_MSN_CAM_SEND_CONFERENCE_H__ */

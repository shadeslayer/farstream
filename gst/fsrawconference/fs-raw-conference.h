/*
 * Farsight2 - Farsight Raw Conference Implementation
 *
 * Copyright 2007,2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * gstfsrawconference.h - Raw implementation for Farsight Conference Gstreamer
 *                        Elements
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

#ifndef __FS_RAW_CONFERENCE_H__
#define __FS_RAW_CONFERENCE_H__

#include <gst/farsight/fs-base-conference.h>

G_BEGIN_DECLS

#define FS_TYPE_RAW_CONFERENCE \
  (fs_raw_conference_get_type ())
#define FS_RAW_CONFERENCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),FS_TYPE_RAW_CONFERENCE,FsRawConference))
#define FS_RAW_CONFERENCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),FS_TYPE_RAW_CONFERENCE,FsRawConferenceClass))
#define FS_RAW_CONFERENCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),FS_TYPE_RAW_CONFERENCE,FsRawConferenceClass))
#define FS_IS_RAW_CONFERENCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),FS_TYPE_RAW_CONFERENCE))
#define FS_IS_RAW_CONFERENCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),FS_TYPE_RAW_CONFERENCE))
#define FS_RAW_CONFERENCE_CAST(obj) \
  ((FsRawConference *)(obj))

typedef struct _FsRawConference FsRawConference;
typedef struct _FsRawConferenceClass FsRawConferenceClass;
typedef struct _FsRawConferencePrivate FsRawConferencePrivate;

struct _FsRawConference
{
  FsBaseConference parent;

  /*< private >*/
  FsRawConferencePrivate *priv;
};

struct _FsRawConferenceClass
{
  FsBaseConferenceClass parent_class;
};

GType fs_raw_conference_get_type (void);

GstCaps *fs_codec_to_gst_caps (const FsCodec *codec);

GST_DEBUG_CATEGORY_EXTERN (fsrawconference_debug);

G_END_DECLS

#endif /* __FS_RAW_CONFERENCE_H__ */

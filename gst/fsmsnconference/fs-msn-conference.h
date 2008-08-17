/*
 * Farsight2 - Farsight MSN Conference Implementation
 *
 *  @author: Richard Spiers <richard.spiers@gmail.com>
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * gstfsmsnconference.h - MSN implementation for Farsight Conference Gstreamer
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

#ifndef __FS_MSN_CONFERENCE_H__
#define __FS_MSN_CONFERENCE_H__

#include <gst/farsight/fs-base-conference.h>

G_BEGIN_DECLS

#define FS_TYPE_MSN_CONFERENCE \
  (fs_msn_conference_get_type ())
#define FS_MSN_CONFERENCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),FS_TYPE_MSN_CONFERENCE,FsMsnConference))
#define FS_MSN_CONFERENCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),FS_TYPE_MSN_CONFERENCE,FsMsnConferenceClass))
#define FS_MSN_CONFERENCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),FS_TYPE_MSN_CONFERENCE,FsMsnConferenceClass))
#define FS_IS_MSN_CONFERENCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),FS_TYPE_MSN_CONFERENCE))
#define FS_IS_MSN_CONFERENCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),FS_TYPE_MSN_CONFERENCE))
/* since 0.10.4 */
#define FS_MSN_CONFERENCE_CAST(obj) \
  ((FsMsnConference *)(obj))

typedef struct _FsMsnConference FsMsnConference;
typedef struct _FsMsnConferenceClass FsMsnConferenceClass;
typedef struct _FsMsnConferencePrivate FsMsnConferencePrivate;

struct _FsMsnConference
  {
    FsBaseConference parent;
    FsMsnConferencePrivate *priv;
  };

struct _FsMsnConferenceClass
  {
    FsBaseConferenceClass parent_class;
  };

GType fs_msn_conference_get_type (void);


GST_DEBUG_CATEGORY_EXTERN (fsmsnconference_debug);

G_END_DECLS

#endif /* __FS_MSN_CONFERENCE_H__ */

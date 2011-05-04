/*
 * Farsight Voice+Video library
 *
 *  Copyright 2008 Collabora Ltd,
 *  Copyright 2008 Nokia Corporation
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifndef __FS_RTP_BITRATE_ADAPTER_H__
#define __FS_RTP_BITRATE_ADAPTER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define FS_TYPE_RTP_BITRATE_ADAPTER \
  (fs_rtp_bitrate_adapter_get_type())
#define FS_RTP_BITRATE_ADAPTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
  FS_TYPE_RTP_BITRATE_ADAPTER,FsRtpBitrateAdapter))
#define FS_RTP_BITRATE_ADAPTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
  FS_TYPE_RTP_BITRATE_ADAPTER,FsRtpBitrateAdapterClass))
#define FS_IS_RTP_BITRATE_ADAPTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),FS_TYPE_RTP_BITRATE_ADAPTER))
#define FS_IS_RTP_BITRATE_ADAPTER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),FS_TYPE_RTP_BITRATE_ADAPTER))

typedef struct _FsRtpBitrateAdapter FsRtpBitrateAdapter;
typedef struct _FsRtpBitrateAdapterClass FsRtpBitrateAdapterClass;
typedef struct _FsRtpBitrateAdapterPrivate FsRtpBitrateAdapterPrivate;

struct _FsRtpBitrateAdapter
{
  GstElement parent;

  GstPad *srcpad;
  GstPad *sinkpad;

  GstCaps *caps;
  gboolean new_suggestion;

  GstCaps *last_caps;

  GstClock *system_clock;
  GstClockTime interval;
  GQueue bitrate_history;
  GstClockID clockid;

  GstCaps *rtp_caps;
};

struct _FsRtpBitrateAdapterClass
{
  GstElementClass parent_class;
};

GType fs_rtp_bitrate_adapter_get_type (void);

G_END_DECLS

#endif /* __FS_RTP_BITRATE_ADAPTER_H__ */

/*
 * Farsight2 - Farsight RTP DTMF Event Source
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-dtmf-event-source.h - A Farsight RTP Event Source gobject
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


#ifndef __FS_RTP_DTMF_EVENT_SOURCE_H__
#define __FS_RTP_DTMF_EVENT_SOURCE_H__

#include "fs-rtp-special-source.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_DTMF_EVENT_SOURCE \
  (fs_rtp_dtmf_event_source_get_type())
#define FS_RTP_DTMF_EVENT_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_DTMF_EVENT_SOURCE, \
      FsRtpDtmfEventSource))
#define FS_RTP_DTMF_EVENT_SOURCE_CLASS(klass) \
 (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_DTMF_EVENT_SOURCE, \
     FsRtpDtmfEventSourceClass))
#define FS_IS_RTP_DTMF_EVENT_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_DTMF_EVENT_SOURCE))
#define FS_IS_RTP_DTMF_EVENT_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_DTMF_EVENT_SOURCE))
#define FS_RTP_DTMF_EVENT_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_DTMF_EVENT_SOURCE,   \
    FsRtpDtmfEventSourceClass))
#define FS_RTP_DTMF_EVENT_SOURCE_CAST(obj) ((FsRtpDtmfEventSource*) (obj))

typedef struct _FsRtpDtmfEventSource FsRtpDtmfEventSource;
typedef struct _FsRtpDtmfEventSourceClass FsRtpDtmfEventSourceClass;
typedef struct _FsRtpDtmfEventSourcePrivate FsRtpDtmfEventSourcePrivate;

struct _FsRtpDtmfEventSourceClass
{
  FsRtpSpecialSourceClass parent_class;
};

/**
 * FsRtpDtmfEventSource:
 *
 */
struct _FsRtpDtmfEventSource
{
  FsRtpSpecialSource parent;
  FsRtpDtmfEventSourcePrivate *priv;
};

GType fs_rtp_dtmf_event_source_get_type (void);

G_END_DECLS

#endif /* __FS_RTP_DTMF_EVENT_SOURCE_H__ */

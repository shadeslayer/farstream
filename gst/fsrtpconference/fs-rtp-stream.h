/*
 * Farsight2 - Farsight RTP Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-stream.h - A Farsight RTP Stream
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

#ifndef __FS_RTP_STREAM_H__
#define __FS_RTP_STREAM_H__

#include <gst/farsight/fs-stream.h>
#include <gst/farsight/fs-stream-transmitter.h>

#include "fs-rtp-participant.h"
#include "fs-rtp-session.h"
#include "fs-rtp-substream.h"

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_STREAM \
  (fs_rtp_stream_get_type ())
#define FS_RTP_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_STREAM, FsRtpStream))
#define FS_RTP_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_STREAM, FsRtpStreamClass))
#define FS_IS_RTP_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_STREAM))
#define FS_IS_RTP_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_STREAM))
#define FS_RTP_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_STREAM, FsRtpStreamClass))
#define FS_RTP_STREAM_CAST(obj) ((FsRtpStream*) (obj))

typedef struct _FsRtpStream FsRtpStream;
typedef struct _FsRtpStreamClass FsRtpStreamClass;
typedef struct _FsRtpStreamPrivate FsRtpStreamPrivate;

struct _FsRtpStreamClass
{
  FsStreamClass parent_class;

};

/**
 * FsRtpStream:
 *
 */
struct _FsRtpStream
{
  FsStream parent;

  /*< private >*/

  /* Can only be accessed while holding the FsRtpSession lock */
  /* Dont modify, call set_remote_codecs() */
  GList *remote_codecs;
  GList *negotiated_codecs;

  /* Same as codecs, hold FsRtpSession lock and modify by
   * setting the property
   */
  GList *hdrext;

  /* Dont modify, call add_substream() */
  GList *substreams;

  FsRtpParticipant *participant;

  FsRtpStreamPrivate *priv;
};

GType fs_rtp_stream_get_type (void);

typedef gboolean (*stream_new_remote_codecs_cb) (FsRtpStream *stream,
    GList *codecs, GError **error, gpointer user_data);
typedef void (*stream_known_source_packet_receive_cb) (FsRtpStream *stream,
    guint component, GstBuffer *buffer, gpointer user_data);
typedef void (*stream_sending_changed_locked_cb) (FsRtpStream *stream,
    gboolean sending, gpointer user_data);
typedef void (*stream_ssrc_added_cb) (FsRtpStream *stream, guint32 ssrc,
    gpointer user_data);
typedef FsStreamTransmitter* (*stream_get_new_stream_transmitter_cb) (
  FsRtpStream *stream, const gchar *transmitter_name,
  FsParticipant *participant, GParameter *parameters, guint n_parameters,
  GError **error, gpointer user_data);


FsRtpStream *fs_rtp_stream_new (FsRtpSession *session,
    FsRtpParticipant *participant,
    FsStreamDirection direction,
    FsStreamTransmitter *stream_transmitter,
    stream_new_remote_codecs_cb new_remote_codecs_cb,
    stream_known_source_packet_receive_cb known_source_packet_received_cb,
    stream_sending_changed_locked_cb sending_changed_locked_cb,
    stream_ssrc_added_cb ssrc_added_cb,
    stream_get_new_stream_transmitter_cb get_new_stream_transmitter_cb,
    gpointer user_data_for_cb,
    GError **error);

gboolean fs_rtp_stream_add_substream_unlock (FsRtpStream *stream,
    FsRtpSubStream *substream,
    GError **error);

void
fs_rtp_stream_set_negotiated_codecs_unlock (FsRtpStream *stream,
    GList *codecs);

G_END_DECLS

#endif /* __FS_RTP_STREAM_H__ */

/*
 * Farstream - Farstream Session
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-session.h - A Farstream Session gobject (base implementation)
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

#ifndef __FS_SESSION_H__
#define __FS_SESSION_H__

#include <glib.h>
#include <glib-object.h>

#include <farstream/fs-stream.h>
#include <farstream/fs-participant.h>
#include <farstream/fs-codec.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_SESSION \
  (fs_session_get_type ())
#define FS_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_SESSION, FsSession))
#define FS_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_SESSION, FsSessionClass))
#define FS_IS_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_SESSION))
#define FS_IS_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_SESSION))
#define FS_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_SESSION, FsSessionClass))
#define FS_SESSION_CAST(obj) ((FsSession *) (obj))

typedef struct _FsSession FsSession;
typedef struct _FsSessionClass FsSessionClass;
typedef struct _FsSessionPrivate FsSessionPrivate;

/**
 * FsDTMFEvent:
 *
 * An enum that represents the different DTMF event that can be sent to a
 * #FsSession. The values corresponds those those defined in RFC 4733
 * The rest of the possibles values are in the IANA registry at:
 * http://www.iana.org/assignments/audio-telephone-event-registry
 *
 */
typedef enum _FsDTMFEvent
{
  /*< protected >*/
  FS_DTMF_EVENT_0 = 0,
  FS_DTMF_EVENT_1 = 1,
  FS_DTMF_EVENT_2 = 2,
  FS_DTMF_EVENT_3 = 3,
  FS_DTMF_EVENT_4 = 4,
  FS_DTMF_EVENT_5 = 5,
  FS_DTMF_EVENT_6 = 6,
  FS_DTMF_EVENT_7 = 7,
  FS_DTMF_EVENT_8 = 8,
  FS_DTMF_EVENT_9 = 9,
  FS_DTMF_EVENT_STAR = 10,
  FS_DTMF_EVENT_POUND = 11,
  FS_DTMF_EVENT_A = 12,
  FS_DTMF_EVENT_B = 13,
  FS_DTMF_EVENT_C = 14,
  FS_DTMF_EVENT_D = 15
} FsDTMFEvent;

/**
 * FsDTMFMethod:
 * @FS_DTMF_METHOD_RTP_RFC4733: Send as a special payload type defined by RFC 4733
 * (which obsoletes RFC 2833)
 * @FS_DTMF_METHOD_SOUND: Send as tones as in-band audio sound
 *
 * An enum that represents the different ways a DTMF event can be sent
 *
 */
typedef enum _FsDTMFMethod
{
  FS_DTMF_METHOD_RTP_RFC4733 = 1,
  FS_DTMF_METHOD_SOUND = 2
} FsDTMFMethod;

/**
 * FsSessionClass:
 * @parent_class: Our parent
 * @new_stream: Create a new #FsStream
 * @start_telephony_event: Starts a telephony event
 * @stop_telephony_event: Stops a telephony event
 * @set_send_codec: Forces sending with a specific codec
 * @set_codec_preferences: Specifies the codec preferences
 * @list_transmitters: Returns a list of the available transmitters
 * @get_stream_transmitter_type: Returns the GType of the stream transmitter
 * @codecs_need_resend: Returns the list of codecs that need resending
 *
 * You must override at least new_stream in a subclass.
 */


struct _FsSessionClass
{
  GstObjectClass parent_class;

  /*virtual functions */
  FsStream *(* new_stream) (FsSession *session,
                            FsParticipant *participant,
                            FsStreamDirection direction,
                            GError **error);

  gboolean (* start_telephony_event) (FsSession *session, guint8 event,
                                      guint8 volume);
  gboolean (* stop_telephony_event) (FsSession *session);

  gboolean (* set_send_codec) (FsSession *session, FsCodec *send_codec,
                               GError **error);
  gboolean (* set_codec_preferences) (FsSession *session,
      GList *codec_preferences,
      GError **error);

  gchar** (* list_transmitters) (FsSession *session);

  GType (* get_stream_transmitter_type) (FsSession *session,
                                         const gchar *transmitter);

  GList* (* codecs_need_resend) (FsSession *session, GList *old_codecs,
      GList *new_codecs);

  /*< private >*/
  gpointer _padding[8];
};

/**
 * FsSession:
 *
 * All members are private, access them using methods and properties
 */
struct _FsSession
{
  GstObject parent;
  /*< private >*/

  FsSessionPrivate *priv;


  gpointer _padding[8];
};

GType fs_session_get_type (void);

FsStream *fs_session_new_stream (FsSession *session,
                                 FsParticipant *participant,
                                 FsStreamDirection direction,
                                 GError **error);

gboolean fs_session_start_telephony_event (FsSession *session, guint8 event,
                                           guint8 volume);

gboolean fs_session_stop_telephony_event (FsSession *session);

gboolean fs_session_set_send_codec (FsSession *session, FsCodec *send_codec,
                                    GError **error);

gboolean fs_session_set_codec_preferences (FsSession *session,
    GList *codec_preferences,
    GError **error);

gchar **fs_session_list_transmitters (FsSession *session);

void fs_session_emit_error (FsSession *session,
    gint error_no,
    const gchar *error_msg);

GType fs_session_get_stream_transmitter_type (FsSession *session,
    const gchar *transmitter);

GList* fs_session_codecs_need_resend (FsSession *session,
    GList *old_codecs, GList *new_codecs);

void fs_session_destroy (FsSession *session);


gboolean fs_session_parse_send_codec_changed (FsSession *session,
    GstMessage *message,
    FsCodec **codec,
    GList **secondary_codecs);

gboolean fs_session_parse_codecs_changed (FsSession *session,
    GstMessage *message);

gboolean fs_session_parse_telephony_event_started (FsSession *session,
    GstMessage *message,
    FsDTMFMethod *method,
    FsDTMFEvent *event,
    guint8 *volume);

gboolean fs_session_parse_telephony_event_stopped (FsSession *session,
    GstMessage *message,
    FsDTMFMethod *method);



G_END_DECLS

#endif /* __FS_SESSION_H__ */

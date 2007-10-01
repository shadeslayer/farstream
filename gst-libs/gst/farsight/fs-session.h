/*
 * Farsight2 - Farsight Session
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-session.h - A Farsight Session gobject (base implementation)
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

#ifndef __FS_SESSION_H__
#define __FS_SESSION_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_SESSION \
  (fs_session_get_type())
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
typedef enum
{
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
 * @FS_DTMF_METHOD_AUTO: Send in any possible way
 * @FS_DTMF_METHOD_RTP_RFC4733: Send as a special payload type defined by RFC 4733 
 * (which obsoletes RFC 2833)
 * @FS_DTMF_METHOD_IN_BAND: Send as tones as in-band audio sound
 *
 * An enum that represents the different ways a DTMF event can be sent
 */
typedef enum
{
  FS_DTMF_METHOD_AUTO = 0,
  FS_DTMF_METHOD_RTP_RFC4733,
  FS_DTMF_METHOD_IN_BAND
} FsDTMFMethod;

struct _FsSessionClass
{
  GObjectClass parent_class;

  /*virtual functions */
  FsSessionParticipant *(* fs_session_add_participant)
    (FsSession *session, FsParticipant *participant);

  GstPad *(* fs_session_get_sink_pad) (FsSession *session);

  G_CONST_RETURN GList *(* get_local_codecs) (FsSession *session);

  void (* set_active_codec) (FsSession *session, gint payload_type);
  gint (* get_active_codec) (FsSession *session);

  gboolean (* start_telephony_event) (FsSession *session,
                                      guint8 event,
                                      guint8 volume,
                                      FsDTMFMethod method);
  gboolean (* stop_telephony_event) (FSession *session,
                                     FsDTMFMethod method);

  /*< private >*/
  gpointer _padding[8];
};

/**
 * FsSession:
 *
 */
struct _FsSession
{
  GObject parent;

  /*< private >*/
  gpointer _padding[8];
  FsSessionPrivate *priv;
};

GType fs_session_get_type (void);

G_END_DECLS

#endif /* __FS_SESSION_H__ */

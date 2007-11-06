/*
 * Farsight2 - Farsight RTP Conference Implementation
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-conference.c - RTP implementation for Farsight Conference Gstreamer
 *                       Elements
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

/**
 * SECTION:fs-rtp-conference
 * @short_description: FarsightRTP Conference Gstreamer Elements
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtp-conference.h"
#include "fs-rtp-session.h"
#include "fs-rtp-participant.h"

GST_DEBUG_CATEGORY_STATIC (fs_rtp_conference_debug);
#define GST_CAT_DEFAULT fs_rtp_conference_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0
};


static GstElementDetails fs_rtp_conference_details = {
  "Farsight RTP Conference",
  "Generic/Bin/RTP",
  "A Farsight RTP Conference",
  "Olivier Crete <olivier.crete@collabora.co.uk>"
};



static GstStaticPadTemplate fs_rtp_conference_sink_template =
  GST_STATIC_PAD_TEMPLATE ("sink_%d",
                           GST_PAD_SINK,
                           GST_PAD_SOMETIMES,
                           GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate fs_rtp_conference_src_template =
  GST_STATIC_PAD_TEMPLATE ("src_%d_%d_%d",
                           GST_PAD_SRC,
                           GST_PAD_SOMETIMES,
                           GST_STATIC_CAPS_ANY);


#define FS_RTP_CONFERENCE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), FS_TYPE_RTP_CONFERENCE, FsRtpConferencePrivate))

struct _FsRtpConferencePrivate
{
  GstElement *gstrtpbin;

  gboolean disposed;

  /* Protected by GST_OBJECT_LOCK */
  GList *sessions;
  guint max_session_id;
};

static void fs_rtp_conference_do_init (GType type);


GST_BOILERPLATE_FULL (FsRtpConference, fs_rtp_conference, FsBaseConference,
                      FS_TYPE_BASE_CONFERENCE, fs_rtp_conference_do_init);

static void fs_rtp_conference_finalize (GObject *object);
static FsSession *fs_rtp_conference_new_session (FsBaseConference *conf,
                                                 FsMediaType media_type);
static FsParticipant *fs_rtp_conference_new_participant (FsBaseConference *conf,
                                                         gchar *cname);

static FsRtpSession *fs_rtp_conference_get_session_by_id_locked (
    FsRtpConference *self, guint session_id);
static FsRtpSession *fs_rtp_conference_get_session_by_id (
    FsRtpConference *self, guint session_id);
static GstCaps *fs_rtp_conference_request_pt_map (GstElement *element,
                                                  guint session_id,
                                                  guint pt, gpointer user_data);


static void
fs_rtp_conference_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (fs_rtp_conference_debug, "fsrtpconference", 0,
      "farsight rtp conference element");
}

static void
fs_rtp_conference_dispose (GObject * object)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (object);

  if (self->priv->disposed)
    return;

  if (self->priv->gstrtpbin) {
    gst_object_unref (self->priv->gstrtpbin);
    self->priv->gstrtpbin = NULL;
  }

  self->priv->disposed = TRUE;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
fs_rtp_conference_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
fs_rtp_conference_class_init (FsRtpConferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  FsBaseConferenceClass *baseconf_class = FS_BASE_CONFERENCE_CLASS (klass);
  g_type_class_add_private (klass, sizeof (FsRtpConferencePrivate));

  parent_class = g_type_class_peek_parent (klass);

  baseconf_class->new_session =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_new_session);
  baseconf_class->new_participant =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_new_participant);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (fs_rtp_conference_finalize);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (fs_rtp_conference_dispose);

  gst_element_class_set_details (gstelement_class, &fs_rtp_conference_details);

  gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&fs_rtp_conference_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&fs_rtp_conference_src_template));


}

static void
fs_rtp_conference_base_init (gpointer g_class)
{
}

static void
fs_rtp_conference_init (FsRtpConference *conf,
    FsRtpConferenceClass *bclass)
{
  GST_DEBUG_OBJECT (conf, "fs_rtp_conference_init");

  conf->priv = FS_RTP_CONFERENCE_GET_PRIVATE (conf);

  conf->priv->disposed = FALSE;
  conf->priv->max_session_id = 0;

  conf->priv->gstrtpbin = gst_element_factory_make ("gstrtpbin", NULL);

  if (!conf->priv->gstrtpbin) {
    GST_ERROR_OBJECT (conf, "Could not create GstRtpBin element");
    return;
  }

  if (!gst_bin_add (GST_BIN (conf), conf->priv->gstrtpbin)) {
    GST_ERROR_OBJECT (conf, "Could not create GstRtpBin element");
    gst_object_unref (conf->priv->gstrtpbin);
    return;
  }

  gst_object_ref (conf->priv->gstrtpbin);

  g_signal_connect (conf->priv->gstrtpbin, "request-pt-map",
                    G_CALLBACK (fs_rtp_conference_request_pt_map), conf);
}

static GstCaps *
fs_rtp_conference_request_pt_map (GstElement *element, guint session_id,
                                  guint pt, gpointer user_data)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (user_data);
  FsRtpSession *session = NULL;
  GstCaps *caps = NULL;

  session = fs_rtp_conference_get_session_by_id (self, session_id);

  if (session) {
    caps = fs_rtp_session_request_pt_map (session, pt);
    g_object_unref (session);
  } else {
    GST_WARNING_OBJECT(self,"GstRtpBin %p tried to request the caps for "
                       " payload type %u for non-existent session %u",
                       element, pt, session_id);
  }

  return caps;
}

/**
 * fs_rtp_conference_get_session_by_id_locked
 * @self: The #FsRtpConference
 * @session_id: The session id
 *
 * Gets the #FsRtpSession from a list of sessions or NULL if it doesnt exist
 * You have to hold the GST_OBJECT_LOCK to call this function.
 *
 * Return value: A #FsRtpSession (unref after use) or NULL if it doesn't exist
 */
static FsRtpSession *
fs_rtp_conference_get_session_by_id_locked (FsRtpConference *self,
                                            guint session_id)
{
  GList *item = NULL;

  for (item = g_list_first (self->priv->sessions);
       item;
       item = g_list_next (item)) {
    /*
      Must implement ID

    FsRtpSession *session = item->data;
    if (session->id == session_id) {
      g_object_ref(session);
      break;
    }
    */
  }

  if (item)
    return FS_RTP_SESSION (item->data);
  else
    return NULL;
}

/**
 * fs_rtp_conference_get_session_by_id
 * @self: The #FsRtpConference
 * @session_id: The session id
 *
 * Gets the #FsRtpSession from a list of sessions or NULL if it doesnt exist
 * You have to hold the GST_OBJECT_LOCK to call this function.
 *
 * Return value: A #FsRtpSession (unref after use) or NULL if it doesn't exist
 */
static FsRtpSession *
fs_rtp_conference_get_session_by_id (FsRtpConference *self, guint session_id)
{
  FsRtpSession *session = NULL;

  GST_OBJECT_LOCK (self->priv->sessions);
  session = fs_rtp_conference_get_session_by_id_locked (self, session_id);
  GST_OBJECT_UNLOCK (self->priv->sessions);

  return session;
}

static void
_remove_session (gpointer user_data,
                 GObject *where_the_object_was)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (user_data);

  GST_OBJECT_LOCK (self);
  self->priv->sessions =
    g_list_remove_all (self->priv->sessions, where_the_object_was);
  GST_OBJECT_UNLOCK (self);
}

static FsSession *
fs_rtp_conference_new_session (FsBaseConference *conf,
                               FsMediaType media_type)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (conf);
  FsSession *new_session = NULL;
  guint id;

  GST_OBJECT_LOCK (self);
  do {
    id = self->priv->max_session_id++;
  } while (fs_rtp_conference_get_session_by_id_locked (self, id));
  GST_OBJECT_UNLOCK (self);

  new_session = FS_SESSION_CAST (fs_rtp_session_new (media_type, self, id));

  GST_OBJECT_LOCK (self);
  self->priv->sessions = g_list_append (self->priv->sessions, new_session);
  GST_OBJECT_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_session), _remove_session, self);

  return new_session;
}


static FsParticipant *
fs_rtp_conference_new_participant (FsBaseConference *conf,
                                   gchar *cname)
{
  FsParticipant *new_participant = NULL;

  new_participant = FS_PARTICIPANT_CAST (fs_rtp_participant_new (cname));

  return new_participant;
}

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
#include "fs-rtp-stream.h"
#include "fs-rtp-participant.h"

#include <string.h>

GST_DEBUG_CATEGORY (fsrtpconference_debug);
GST_DEBUG_CATEGORY (fsrtpconference_disco);
GST_DEBUG_CATEGORY (fsrtpconference_nego);
#define GST_CAT_DEFAULT fsrtpconference_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0,
  PROP_SDES_CNAME,
  PROP_SDES_NAME,
  PROP_SDES_EMAIL,
  PROP_SDES_PHONE,
  PROP_SDES_LOCATION,
  PROP_SDES_TOOL,
  PROP_SDES_NOTE,

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
  gboolean disposed;

  /* Protected by GST_OBJECT_LOCK */
  GList *sessions;
  guint max_session_id;

  GList *participants;
};

static void fs_rtp_conference_do_init (GType type);


GST_BOILERPLATE_FULL (FsRtpConference, fs_rtp_conference, FsBaseConference,
                      FS_TYPE_BASE_CONFERENCE, fs_rtp_conference_do_init);

static void fs_rtp_conference_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_rtp_conference_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);

static void fs_rtp_conference_finalize (GObject *object);
static FsSession *fs_rtp_conference_new_session (FsBaseConference *conf,
                                                 FsMediaType media_type,
                                                 GError **error);
static FsParticipant *fs_rtp_conference_new_participant (FsBaseConference *conf,
    gchar *cname,
    GError **error);

static FsRtpSession *fs_rtp_conference_get_session_by_id_locked (
    FsRtpConference *self, guint session_id);
static FsRtpSession *fs_rtp_conference_get_session_by_id (
    FsRtpConference *self, guint session_id);
static GstCaps *_rtpbin_request_pt_map (GstElement *element,
    guint session_id,
    guint pt,
    gpointer user_data);
static void _rtpbin_pad_added (GstElement *rtpbin,
    GstPad *new_pad,
    gpointer user_data);
static void _rtpbin_on_new_ssrc_cname_association (GstElement *rtpbin,
    guint session_id,
    guint ssrc,
    gchar *cname,
    gpointer user_data);


static void
fs_rtp_conference_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (fsrtpconference_debug, "fsrtpconference", 0,
      "Farsight RTP Conference Element");
  GST_DEBUG_CATEGORY_INIT (fsrtpconference_disco, "fsrtpconference_disco",
      0, "Farsight RTP Codec Discovery");
  GST_DEBUG_CATEGORY_INIT (fsrtpconference_nego, "fsrtpconference_nego",
      0, "Farsight RTP Codec Negotiation");
}

static void
fs_rtp_conference_dispose (GObject * object)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (object);

  if (self->priv->disposed)
    return;

  if (self->gstrtpbin) {
    gst_object_unref (self->gstrtpbin);
    self->gstrtpbin = NULL;
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
  gobject_class->set_property =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_set_property);
  gobject_class->get_property =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_get_property);

  gst_element_class_set_details (gstelement_class, &fs_rtp_conference_details);

  gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&fs_rtp_conference_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&fs_rtp_conference_src_template));

  g_object_class_install_property (gobject_class, PROP_SDES_CNAME,
      g_param_spec_string ("sdes-cname", "Canonical name",
          "The CNAME for the RTP sessions",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SDES_NAME,
      g_param_spec_string ("sdes-name", "SDES NAME",
          "The NAME to put in SDES messages of this session",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SDES_EMAIL,
      g_param_spec_string ("sdes-email", "SDES EMAIL",
          "The EMAIL to put in SDES messages of this session",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SDES_PHONE,
      g_param_spec_string ("sdes-phone", "SDES PHONE",
          "The PHONE to put in SDES messages of this session",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SDES_LOCATION,
      g_param_spec_string ("sdes-location", "SDES LOCATION",
          "The LOCATION to put in SDES messages of this session",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SDES_TOOL,
      g_param_spec_string ("sdes-tool", "SDES TOOL",
          "The TOOL to put in SDES messages of this session",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SDES_NOTE,
      g_param_spec_string ("sdes-note", "SDES NOTE",
          "The NOTE to put in SDES messages of this session",
          NULL, G_PARAM_READWRITE));

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
  conf->priv->max_session_id = 1;

  conf->gstrtpbin = gst_element_factory_make ("gstrtpbin", NULL);

  if (!conf->gstrtpbin) {
    GST_ERROR_OBJECT (conf, "Could not create GstRtpBin element");
    return;
  }

  if (!gst_bin_add (GST_BIN (conf), conf->gstrtpbin)) {
    GST_ERROR_OBJECT (conf, "Could not create GstRtpBin element");
    gst_object_unref (conf->gstrtpbin);
    conf->gstrtpbin = NULL;
    return;
  }

  gst_object_ref (conf->gstrtpbin);

  g_signal_connect (conf->gstrtpbin, "request-pt-map",
                    G_CALLBACK (_rtpbin_request_pt_map), conf);
  g_signal_connect (conf->gstrtpbin, "pad-added",
                    G_CALLBACK (_rtpbin_pad_added), conf);
  g_signal_connect (conf->gstrtpbin, "on-new-ssrc-cname-association",
                    G_CALLBACK (_rtpbin_on_new_ssrc_cname_association), conf);
}

static void
fs_rtp_conference_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (object);

  switch (prop_id)
  {
    case PROP_SDES_CNAME:
      g_object_get_property (G_OBJECT (self->gstrtpbin), "sdes-cname", value);
      break;
    case PROP_SDES_NAME:
      g_object_get_property (G_OBJECT (self->gstrtpbin), "sdes-name", value);
      break;
    case PROP_SDES_EMAIL:
      g_object_get_property (G_OBJECT (self->gstrtpbin), "sdes-email", value);
      break;
    case PROP_SDES_PHONE:
      g_object_get_property (G_OBJECT (self->gstrtpbin), "sdes-phone", value);
      break;
    case PROP_SDES_LOCATION:
      g_object_get_property (G_OBJECT (self->gstrtpbin), "sdes-location",
          value);
      break;
    case PROP_SDES_TOOL:
      g_object_get_property (G_OBJECT (self->gstrtpbin), "sdes-tool", value);
      break;
    case PROP_SDES_NOTE:
      g_object_get_property (G_OBJECT (self->gstrtpbin), "sdes-note", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
fs_rtp_conference_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (object);

  switch (prop_id)
  {
    case PROP_SDES_CNAME:
      g_object_set_property (G_OBJECT (self->gstrtpbin), "sdes-cname", value);
      break;
    case PROP_SDES_NAME:
      g_object_set_property (G_OBJECT (self->gstrtpbin), "sdes-name", value);
      break;
    case PROP_SDES_EMAIL:
      g_object_set_property (G_OBJECT (self->gstrtpbin), "sdes-email", value);
      break;
    case PROP_SDES_PHONE:
      g_object_set_property (G_OBJECT (self->gstrtpbin), "sdes-phone", value);
      break;
    case PROP_SDES_LOCATION:
      g_object_set_property (G_OBJECT (self->gstrtpbin), "sdes-location",
          value);
      break;
    case PROP_SDES_TOOL:
      g_object_set_property (G_OBJECT (self->gstrtpbin), "sdes-tool", value);
      break;
    case PROP_SDES_NOTE:
      g_object_set_property (G_OBJECT (self->gstrtpbin), "sdes-note", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
_rtpbin_request_pt_map (GstElement *element, guint session_id,
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

static void
_rtpbin_pad_added (GstElement *rtpbin, GstPad *new_pad,
  gpointer user_data)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (user_data);
  gchar *name;

  GST_DEBUG_OBJECT (self, "pad %s added %" GST_PTR_FORMAT,
    GST_PAD_NAME (new_pad), GST_PAD_CAPS (new_pad));

  name = gst_pad_get_name (new_pad);

  if (g_str_has_prefix (name, "recv_rtp_src_")) {
    guint session_id, ssrc, pt;

    if (sscanf (name, "recv_rtp_src_%u_%u_%u",
        &session_id, &ssrc, &pt) == 3 && ssrc <= G_MAXUINT32) {
      FsRtpSession *session =
        fs_rtp_conference_get_session_by_id (self, session_id);

      if (session) {
        fs_rtp_session_new_recv_pad (session, new_pad, ssrc, pt);
        g_object_unref (session);
      }
    }
  }

  g_free (name);
}

static void
_rtpbin_on_new_ssrc_cname_association (GstElement *rtpbin,
    guint session_id,
    guint ssrc,
    gchar *cname,
    gpointer user_data)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (user_data);
  FsRtpSession *session =
    fs_rtp_conference_get_session_by_id (self, session_id);

  if (session) {
    fs_rtp_session_associate_ssrc_cname (session, ssrc, cname);
    g_object_unref (session);
  } else {
    GST_WARNING_OBJECT(self,"GstRtpBin %p announced a new association"
        "for non-existent session %u",
        rtpbin, session_id);
  }
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
    FsRtpSession *session = item->data;

    if (session->id == session_id) {
      g_object_ref(session);
      break;
    }
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

  GST_OBJECT_LOCK (self);
  session = fs_rtp_conference_get_session_by_id_locked (self, session_id);
  GST_OBJECT_UNLOCK (self);

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

static void
_remove_participant (gpointer user_data,
                 GObject *where_the_object_was)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (user_data);

  GST_OBJECT_LOCK (self);
  self->priv->participants =
    g_list_remove_all (self->priv->participants, where_the_object_was);
  GST_OBJECT_UNLOCK (self);
}


static FsSession *
fs_rtp_conference_new_session (FsBaseConference *conf,
                               FsMediaType media_type,
                               GError **error)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (conf);
  FsSession *new_session = NULL;
  guint id;

  GST_OBJECT_LOCK (self);
  do {
    id = self->priv->max_session_id++;
  } while (fs_rtp_conference_get_session_by_id_locked (self, id));
  GST_OBJECT_UNLOCK (self);

  new_session = FS_SESSION_CAST (fs_rtp_session_new (media_type, self, id,
     error));

  if (!new_session) {
    return NULL;
  }

  GST_OBJECT_LOCK (self);
  self->priv->sessions = g_list_append (self->priv->sessions, new_session);
  GST_OBJECT_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_session), _remove_session, self);

  return new_session;
}


static FsParticipant *
fs_rtp_conference_new_participant (FsBaseConference *conf,
    gchar *cname,
    GError **error)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (conf);
  FsParticipant *new_participant = NULL;
  GList *item = NULL;

  if (!cname)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid NULL cname");
    return NULL;
  }

  GST_OBJECT_LOCK (self);
  for (item = g_list_first (self->priv->participants);
       item;
       item = g_list_next (item))
  {
    gchar *lcname;

    g_object_get (item->data, "cname", &lcname, NULL);
    if (!strcmp (lcname, cname))
        break;
  }
  GST_OBJECT_UNLOCK (self);

  if (item)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "There is already a participant with this cname");
    return NULL;
  }


  new_participant = FS_PARTICIPANT_CAST (fs_rtp_participant_new (cname));


  GST_OBJECT_LOCK (self);
  self->priv->participants = g_list_append (self->priv->participants,
      new_participant);
  GST_OBJECT_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_participant), _remove_participant, self);

  return new_participant;
}

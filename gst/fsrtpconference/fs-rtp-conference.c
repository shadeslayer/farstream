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
 * SECTION:element-fsrtpconference
 * @short_description: Farsight RTP Conference Gstreamer Elements
 *
 * This is the core gstreamer element for a RTP conference. It must be added
 * to your pipeline before anything else is done. Then you create the session,
 * participants and streams according to the #FsConference interface.
 *
 * The various sdes-* properties allow you to set the content of the SDES packet
 * in the sent RTCP reports.
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


static const GstElementDetails fs_rtp_conference_details =
GST_ELEMENT_DETAILS (
  "Farsight RTP Conference",
  "Generic/Bin/RTP",
  "A Farsight RTP Conference",
  "Olivier Crete <olivier.crete@collabora.co.uk>");


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
    const gchar *cname,
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
static void _rtpbin_on_bye_ssrc (GstElement *rtpbin,
    guint session_id,
    guint ssrc,
    gpointer user_data);

static void
_remove_session (gpointer user_data,
    GObject *where_the_object_was);
static void
_remove_participant (gpointer user_data,
    GObject *where_the_object_was);


static void fs_rtp_conference_handle_message (
    GstBin * bin,
    GstMessage * message);

static GstStateChangeReturn fs_rtp_conference_change_state (
    GstElement *element,
    GstStateChange transition);


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
  GList *item;

  if (self->priv->disposed)
    return;

  if (self->gstrtpbin) {
    gst_object_unref (self->gstrtpbin);
    self->gstrtpbin = NULL;
  }

  for (item = g_list_first (self->priv->sessions);
       item;
       item = g_list_next (item))
    g_object_weak_unref (G_OBJECT (item->data), _remove_session, self);
  g_list_free (self->priv->sessions);
  self->priv->sessions = NULL;

  for (item = g_list_first (self->priv->participants);
       item;
       item = g_list_next (item))
    g_object_weak_unref (G_OBJECT (item->data), _remove_participant, self);
  g_list_free (self->priv->participants);
  self->priv->participants = NULL;

  self->priv->disposed = TRUE;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
fs_rtp_conference_finalize (GObject * object)
{
  /* Peek will always succeed here because we 'refed the class in the _init */
  g_type_class_unref (g_type_class_peek (FS_TYPE_RTP_SUB_STREAM));

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
fs_rtp_conference_class_init (FsRtpConferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  FsBaseConferenceClass *baseconf_class = FS_BASE_CONFERENCE_CLASS (klass);
  GstBinClass *gstbin_class = GST_BIN_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsRtpConferencePrivate));

  parent_class = g_type_class_peek_parent (klass);

  baseconf_class->new_session =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_new_session);
  baseconf_class->new_participant =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_new_participant);

  gstbin_class->handle_message =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_handle_message);

  gstelement_class->change_state =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_change_state);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (fs_rtp_conference_finalize);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (fs_rtp_conference_dispose);
  gobject_class->set_property =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_set_property);
  gobject_class->get_property =
    GST_DEBUG_FUNCPTR (fs_rtp_conference_get_property);

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
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&fs_rtp_conference_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&fs_rtp_conference_src_template));

  gst_element_class_set_details (gstelement_class, &fs_rtp_conference_details);
}

static void
fs_rtp_conference_init (FsRtpConference *conf,
    FsRtpConferenceClass *bclass)
{
  GST_DEBUG_OBJECT (conf, "fs_rtp_conference_init");

  conf->priv = FS_RTP_CONFERENCE_GET_PRIVATE (conf);

  conf->priv->disposed = FALSE;
  conf->priv->max_session_id = 1;

  conf->gstrtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");

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
  g_signal_connect (conf->gstrtpbin, "on-bye-ssrc",
                    G_CALLBACK (_rtpbin_on_bye_ssrc), conf);

  /* We have to ref the class here because the class initialization
   * in GLib is not thread safe
   * http://bugzilla.gnome.org/show_bug.cgi?id=349410
   * http://bugzilla.gnome.org/show_bug.cgi?id=64764
   */
  g_type_class_ref (FS_TYPE_RTP_SUB_STREAM);
}

static void
rtpbin_get_sdes (FsRtpConference *self, const gchar *prop, GValue *val)
{
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (self->gstrtpbin),
          "sdes"))
  {
    GstStructure *s;
    g_object_get (self->gstrtpbin, "sdes", &s, NULL);
    g_value_copy (gst_structure_get_value (s, prop), val);
    gst_structure_free (s);
  }
  else
  {
    gchar *str = g_strdup_printf ("sdes-%s", prop);
    g_object_get_property (G_OBJECT (self->gstrtpbin), str, val);
    g_free (str);
  }
}

static void
fs_rtp_conference_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (object);

  if (!self->gstrtpbin)
    return;

  switch (prop_id)
  {
    case PROP_SDES_CNAME:
      rtpbin_get_sdes (self, "cname", value);
      break;
    case PROP_SDES_NAME:
      rtpbin_get_sdes (self, "name", value);
      break;
    case PROP_SDES_EMAIL:
      rtpbin_get_sdes (self, "email", value);
      break;
    case PROP_SDES_PHONE:
      rtpbin_get_sdes (self, "phone", value);
      break;
    case PROP_SDES_LOCATION:
      rtpbin_get_sdes (self, "location", value);
      break;
    case PROP_SDES_TOOL:
      rtpbin_get_sdes (self, "tool", value);
      break;
    case PROP_SDES_NOTE:
      rtpbin_get_sdes (self, "note", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
rtpbin_set_sdes (FsRtpConference *self, const gchar *prop, const GValue *val)
{
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (self->gstrtpbin),
          "sdes"))
  {
    GstStructure *s;
    g_object_get (self->gstrtpbin, "sdes", &s, NULL);
    gst_structure_set_value (s, prop, val);
    g_object_set (self->gstrtpbin, "sdes", s, NULL);
    gst_structure_free (s);
  }
  else
  {
    gchar *str = g_strdup_printf ("sdes-%s", prop);
    g_object_set_property (G_OBJECT (self->gstrtpbin), str, val);
    g_free (str);
  }
}

static void
fs_rtp_conference_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (object);

  if (!self->gstrtpbin)
    return;

  switch (prop_id)
  {
    case PROP_SDES_CNAME:
      rtpbin_set_sdes (self, "cname", value);
      break;
    case PROP_SDES_NAME:
      rtpbin_set_sdes (self, "name", value);
      break;
    case PROP_SDES_EMAIL:
      rtpbin_set_sdes (self, "email", value);
      break;
    case PROP_SDES_PHONE:
      rtpbin_set_sdes (self, "phone", value);
      break;
    case PROP_SDES_LOCATION:
      rtpbin_set_sdes (self, "location", value);
      break;
    case PROP_SDES_TOOL:
      rtpbin_set_sdes (self, "tool", value);
      break;
    case PROP_SDES_NOTE:
      rtpbin_set_sdes (self, "note", value);
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
    GST_WARNING_OBJECT (self,"GstRtpBin %p tried to request the caps for "
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

  if (g_str_has_prefix (name, "recv_rtp_src_"))
  {
    guint session_id, ssrc, pt;

    if (sscanf (name, "recv_rtp_src_%u_%u_%u",
            &session_id, &ssrc, &pt) == 3 && ssrc <= G_MAXUINT32)
    {
      FsRtpSession *session =
        fs_rtp_conference_get_session_by_id (self, session_id);

      if (session)
      {
        fs_rtp_session_new_recv_pad (session, new_pad, ssrc, pt);
        g_object_unref (session);
      }
    }
  }

  g_free (name);
}

static void
_rtpbin_on_bye_ssrc (GstElement *rtpbin,
    guint session_id,
    guint ssrc,
    gpointer user_data)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (user_data);
  FsRtpSession *session =
    fs_rtp_conference_get_session_by_id (self, session_id);

  if (session)
  {
    fs_rtp_session_bye_ssrc (session, ssrc);

    g_object_unref (session);
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
      g_object_ref (session);
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

  if (!self->gstrtpbin)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create GstRtpBin");
    return NULL;
  }

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
    const gchar *cname,
    GError **error)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (conf);
  FsParticipant *new_participant = NULL;
  GList *item = NULL;

  if (!self->gstrtpbin)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create GstRtpBin");
    return NULL;
  }

  if (cname)
  {
    GST_OBJECT_LOCK (self);
    for (item = g_list_first (self->priv->participants);
         item;
         item = g_list_next (item))
    {
      gchar *lcname;

      g_object_get (item->data, "cname", &lcname, NULL);
      if (lcname && !strcmp (lcname, cname))
      {
        g_free (lcname);
        break;
      }
      g_free (lcname);
    }
    GST_OBJECT_UNLOCK (self);

    if (item)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "There is already a participant with this cname");
      return NULL;
    }
  }

  new_participant = FS_PARTICIPANT_CAST (fs_rtp_participant_new (cname));


  GST_OBJECT_LOCK (self);
  self->priv->participants = g_list_append (self->priv->participants,
      new_participant);
  GST_OBJECT_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_participant), _remove_participant, self);

  return new_participant;
}

static void
fs_rtp_conference_handle_message (
    GstBin * bin,
    GstMessage * message)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (bin);

  if (!self->gstrtpbin)
    return;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure *s = gst_message_get_structure (message);

      /* we change the structure name and add the session ID to it */
      if (gst_structure_has_name (s, "application/x-rtp-source-sdes") &&
          gst_structure_has_field_typed (s, "session", G_TYPE_UINT) &&
          gst_structure_has_field_typed (s, "ssrc", G_TYPE_UINT) &&
          gst_structure_has_field_typed (s, "cname", G_TYPE_STRING))
      {
        guint session_id;
        guint ssrc;
        const GValue *val;
        FsRtpSession *session;
        const gchar *cname;

        val = gst_structure_get_value (s, "session");
        session_id = g_value_get_uint (val);

        val = gst_structure_get_value (s, "ssrc");
        ssrc = g_value_get_uint (val);

        cname = gst_structure_get_string (s, "cname");

        if (!ssrc || !cname)
        {
          GST_WARNING_OBJECT (self,
              "Got GstRTPBinSDES without a ssrc or a cname (ssrc:%u cname:%p)",
              ssrc, cname);
          break;
        }

        session = fs_rtp_conference_get_session_by_id (self, session_id);

        if (session) {
          fs_rtp_session_associate_ssrc_cname (session, ssrc, cname);
          g_object_unref (session);
        } else {
          GST_WARNING_OBJECT (self,"Our GstRtpBin announced a new association"
              "for non-existent session %u for ssrc: %u and cname %s",
              session_id, ssrc, cname);
        }
      }
      /* fallthrough to forward the modified message to the parent */
    }
    default:
    {
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
  }
}

static GstStateChangeReturn
fs_rtp_conference_change_state (GstElement *element, GstStateChange transition)
{
  FsRtpConference *self = FS_RTP_CONFERENCE (element);
  GstStateChangeReturn result;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!self->gstrtpbin)
      {
        GST_ERROR_OBJECT (element, "Could not create the GstRtpBin subelement");
        result = GST_STATE_CHANGE_FAILURE;
        goto failure;
      }
      break;
    default:
      break;
  }

  if ((result =
          GST_ELEMENT_CLASS (parent_class)->change_state (element,
              transition)) == GST_STATE_CHANGE_FAILURE)
    goto failure;

  return result;

 failure:
  {
    GST_ERROR_OBJECT (element, "parent failed state change");
    return result;
  }
}



/**
 * fs_codec_to_gst_caps
 * @codec: A #FsCodec to be converted
 *
 * This function converts a #FsCodec to a fixed #GstCaps with media type
 * application/x-rtp.
 *
 * Return value: A newly-allocated #GstCaps or %NULL if the codec was %NULL
 */

GstCaps *
fs_codec_to_gst_caps (const FsCodec *codec)
{
  GstCaps *caps;
  GstStructure *structure;
  GList *item;

  if (codec == NULL)
    return NULL;

  structure = gst_structure_new ("application/x-rtp", NULL);

  if (codec->encoding_name)
  {
    gchar *encoding_name = g_ascii_strup (codec->encoding_name, -1);

    gst_structure_set (structure,
        "encoding-name", G_TYPE_STRING, encoding_name,
        NULL);
    g_free (encoding_name);
  }

  if (codec->clock_rate)
    gst_structure_set (structure,
      "clock-rate", G_TYPE_INT, codec->clock_rate, NULL);

  if (fs_media_type_to_string (codec->media_type))
    gst_structure_set (structure, "media", G_TYPE_STRING,
      fs_media_type_to_string (codec->media_type), NULL);

  if (codec->id >= 0 && codec->id < 128)
    gst_structure_set (structure, "payload", G_TYPE_INT, codec->id, NULL);

  if (codec->channels)
    gst_structure_set (structure, "channels", G_TYPE_INT, codec->channels,
      NULL);

  for (item = codec->optional_params;
       item;
       item = g_list_next (item)) {
    FsCodecParameter *param = item->data;
    gchar *lower_name = g_ascii_strdown (param->name, -1);
    gst_structure_set (structure, lower_name, G_TYPE_STRING, param->value,
      NULL);
    g_free (lower_name);
  }

  caps = gst_caps_new_full (structure, NULL);

  return caps;
}


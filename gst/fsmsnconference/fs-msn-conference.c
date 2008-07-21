/*
 * Farsight2 - Farsight MSN Conference Implementation
 *
 *  @author: Richard Spiers <richard.spiers@gmail.com>
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-conference.c - MSN implementation for Farsight Conference Gstreamer
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
 * SECTION:fs-msn-conference
 * @short_description: FarsightMSN Conference Gstreamer Elements
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-msn-conference.h"
#include "fs-msn-session.h"
#include "fs-msn-stream.h"
#include "fs-msn-participant.h"

#include <string.h>

GST_DEBUG_CATEGORY (fsmsnconference_debug);
#define GST_CAT_DEFAULT fsmsnconference_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0,
  PROP_LOCAL_MSNADD,
};


static GstElementDetails fs_msn_conference_details = {
  "Farsight MSN Conference",
  "Generic/Bin/MSN",
  "A Farsight MSN Conference",
  "Richard Spiers <richard.spiers@gmail.com>"
};



static GstStaticPadTemplate fs_msn_conference_sink_template =
  GST_STATIC_PAD_TEMPLATE ("sink_%d",
                           GST_PAD_SINK,
                           GST_PAD_SOMETIMES,
                           GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate fs_msn_conference_src_template =
  GST_STATIC_PAD_TEMPLATE ("src_%d_%d_%d",
                           GST_PAD_SRC,
                           GST_PAD_SOMETIMES,
                           GST_STATIC_CAPS_ANY);


#define FS_MSN_CONFERENCE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), FS_TYPE_MSN_CONFERENCE, FsMsnConferencePrivate))

struct _FsMsnConferencePrivate
{
  gboolean disposed;
	gchar *local_address;
  /* Protected by GST_OBJECT_LOCK */
  GList *sessions;
  guint max_session_id;
  GList *participants;
};

static void fs_msn_conference_do_init (GType type);


GST_BOILERPLATE_FULL (FsMsnConference, fs_msn_conference, FsBaseConference,
                      FS_TYPE_BASE_CONFERENCE, fs_msn_conference_do_init);

static void fs_msn_conference_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_msn_conference_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);

static void fs_msn_conference_finalize (GObject *object);
static FsSession *fs_msn_conference_new_session (FsBaseConference *conf,
                                                 FsMediaType media_type,
                                                 GError **error);
static FsParticipant *fs_msn_conference_new_participant (FsBaseConference *conf,
    gchar *cname,
    GError **error);

static FsMsnSession *fs_msn_conference_get_session_by_id_locked (
    FsMsnConference *self, guint session_id);
static FsMsnSession *fs_msn_conference_get_session_by_id (
    FsMsnConference *self, guint session_id);

static void
_remove_session (gpointer user_data,
    GObject *where_the_object_was);
static void
_remove_participant (gpointer user_data,
    GObject *where_the_object_was);

static void fs_msn_conference_handle_message (
    GstBin * bin,
    GstMessage * message);

static GstStateChangeReturn fs_msn_conference_change_state (
    GstElement *element,
    GstStateChange transition);


static void
fs_msn_conference_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (fsmsnconference_debug, "fsmsnconference", 0,
      "Farsight MSN Conference Element");
}

static void
fs_msn_conference_dispose (GObject * object)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (object);
  GList *item;

  if (self->priv->disposed)
    return;

  GST_OBJECT_LOCK (object);
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
  GST_OBJECT_UNLOCK (object);

  self->priv->disposed = TRUE;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
fs_msn_conference_finalize (GObject * object)
{

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
fs_msn_conference_class_init (FsMsnConferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  FsBaseConferenceClass *baseconf_class = FS_BASE_CONFERENCE_CLASS (klass);
  GstBinClass *gstbin_class = GST_BIN_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsMsnConferencePrivate));

  parent_class = g_type_class_peek_parent (klass);

  baseconf_class->new_session =
    GST_DEBUG_FUNCPTR (fs_msn_conference_new_session);
  baseconf_class->new_participant =
    GST_DEBUG_FUNCPTR (fs_msn_conference_new_participant);

  gstbin_class->handle_message =
    GST_DEBUG_FUNCPTR (fs_msn_conference_handle_message);

  gstelement_class->change_state =
    GST_DEBUG_FUNCPTR (fs_msn_conference_change_state);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (fs_msn_conference_finalize);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (fs_msn_conference_dispose);
  gobject_class->set_property =
    GST_DEBUG_FUNCPTR (fs_msn_conference_set_property);
  gobject_class->get_property =
    GST_DEBUG_FUNCPTR (fs_msn_conference_get_property);

  gst_element_class_set_details (gstelement_class, &fs_msn_conference_details);

  gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&fs_msn_conference_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&fs_msn_conference_src_template));

  g_object_class_install_property (gobject_class,PROP_LOCAL_MSNADD,
      g_param_spec_string ("local_address", "Msn Address",
          "The local contact address for the MSN sessions",
          NULL, G_PARAM_READWRITE));
}

static void
fs_msn_conference_base_init (gpointer g_class)
{
}

static void
fs_msn_conference_init (FsMsnConference *conf,
    FsMsnConferenceClass *bclass)
{
  GST_DEBUG_OBJECT (conf, "fs_msn_conference_init");

  conf->priv = FS_MSN_CONFERENCE_GET_PRIVATE (conf);

  conf->priv->disposed = FALSE;
  conf->priv->max_session_id = 1;

 /* Put my own stuff here ?  conf->gstrtpbin = gst_element_factory_make ("gstrtpbin", NULL);

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
 */
  
  /* We have to ref the class here because the class initialization
   * in GLib is not thread safe
   * http://bugzilla.gnome.org/show_bug.cgi?id=349410
   * http://bugzilla.gnome.org/show_bug.cgi?id=64764
   */

}

static void
fs_msn_conference_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (object);
 
 switch (prop_id)
  {
  	case PROP_LOCAL_MSNADD:
  	g_value_set_string (value,self->priv->local_address);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
fs_msn_conference_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (object);

 switch (prop_id)
  {
  	case PROP_LOCAL_MSNADD:
  		self->priv->local_address = g_value_get_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/**
 * fs_msn_conference_get_session_by_id_locked
 * @self: The #FsMsnConference
 * @session_id: The session id
 *
 * Gets the #FsMsnSession from a list of sessions or NULL if it doesnt exist
 * You have to hold the GST_OBJECT_LOCK to call this function.
 *
 * Return value: A #FsMsnSession (unref after use) or NULL if it doesn't exist
 */
static FsMsnSession *
fs_msn_conference_get_session_by_id_locked (FsMsnConference *self,
                                            guint session_id)
{
  GList *item = NULL;

  for (item = g_list_first (self->priv->sessions);
       item;
       item = g_list_next (item)) {
    FsMsnSession *session = item->data;

    if (session->id == session_id) {
      g_object_ref (session);
      break;
    }
  }

  if (item)
    return FS_MSN_SESSION (item->data);
  else
    return NULL;
}

/**
 * fs_msn_conference_get_session_by_id
 * @self: The #FsMsnConference
 * @session_id: The session id
 *
 * Gets the #FsMsnSession from a list of sessions or NULL if it doesnt exist
 * You have to hold the GST_OBJECT_LOCK to call this function.
 *
 * Return value: A #FsMsnSession (unref after use) or NULL if it doesn't exist
 */
static FsMsnSession *
fs_msn_conference_get_session_by_id (FsMsnConference *self, guint session_id)
{
  FsMsnSession *session = NULL;
  GST_OBJECT_LOCK (self);
  session = fs_msn_conference_get_session_by_id_locked (self, session_id);
  GST_OBJECT_UNLOCK (self);

  return session;
}

static void
_remove_session (gpointer user_data,
                 GObject *where_the_object_was)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (user_data);

  GST_OBJECT_LOCK (self);
  self->priv->sessions =
    g_list_remove_all (self->priv->sessions, where_the_object_was);
  GST_OBJECT_UNLOCK (self);
}

static void
_remove_participant (gpointer user_data,
                 GObject *where_the_object_was)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (user_data);

  GST_OBJECT_LOCK (self);
  self->priv->participants =
    g_list_remove_all (self->priv->participants, where_the_object_was);
  GST_OBJECT_UNLOCK (self);
}


static FsSession *
fs_msn_conference_new_session (FsBaseConference *conf,
                               FsMediaType media_type,
                               GError **error)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (conf);
  FsSession *new_session = NULL;
  guint id;

  GST_OBJECT_LOCK (self);
  do {
    id = self->priv->max_session_id++;
  } while (fs_msn_conference_get_session_by_id_locked (self, id));
  GST_OBJECT_UNLOCK (self);

  new_session = FS_SESSION_CAST (fs_msn_session_new (media_type, self, id,
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
fs_msn_conference_new_participant (FsBaseConference *conf,
    gchar *cname,
    GError **error)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (conf);
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


  new_participant = FS_PARTICIPANT_CAST (fs_msn_participant_new (cname));


  GST_OBJECT_LOCK (self);
  self->priv->participants = g_list_append (self->priv->participants,
      new_participant);
  GST_OBJECT_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_participant), _remove_participant, self);

  return new_participant;
}

static void
fs_msn_conference_handle_message (
    GstBin * bin,
    GstMessage * message)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (bin);

 switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ELEMENT:
    {
    
    }
    default:
    {
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
  }
  self = NULL; // FIXME Place holder to compile with warnings treated as errors
}

static GstStateChangeReturn
fs_msn_conference_change_state (GstElement *element, GstStateChange transition)
{
  FsMsnConference *self = FS_MSN_CONFERENCE (element);
  GstStateChangeReturn result;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
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
  self = NULL; // FIXME Place holder to compile with warnings treated as errors
}

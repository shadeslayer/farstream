/*
 * Farsight2 - Farsight Base Conference Implementation
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-base-conference.c - Base implementation for Farsight Conference Gstreamer
 *                        Elements
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
 * SECTION:fs-base-conference
 * @short_description: Base class for Farsight Conference Gstreamer Elements
 *
 * This base class must be used by all Farsight Conference elements. It makes
 * sure to agreggate the errors and maintain the lifecycles of the instances in
 * the API.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-base-conference.h"
#include "fs-session.h"

GST_DEBUG_CATEGORY_STATIC (fs_base_conference_debug);
#define GST_CAT_DEFAULT fs_base_conference_debug

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

#define FS_BASE_CONFERENCE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), FS_TYPE_BASE_CONFERENCE, FsBaseConferencePrivate))

struct _FsBaseConferencePrivate
{
  /* List of Sessions */
  GPtrArray *session_list;
};

static GstBinClass *parent_class = NULL;

static void fs_base_conference_interface_init (gpointer g_iface,
                                               gpointer iface_data);
static void fs_base_conference_base_init (gpointer g_class);
static void fs_base_conference_class_init (FsBaseConferenceClass *klass);
static void fs_base_conference_init (FsBaseConference *conf,
                                     FsBaseConferenceClass *klass);
static void fs_base_conference_implements_interface_init (
    GstImplementsInterfaceClass * klass);

GType
fs_base_conference_get_type (void)
{
  static GType base_conference_type = 0;

  if (!base_conference_type) {
    static const GTypeInfo base_conference_info = {
      sizeof (FsBaseConferenceClass),
      (GBaseInitFunc) fs_base_conference_base_init,
      NULL,
      (GClassInitFunc) fs_base_conference_class_init,
      NULL,
      NULL,
      sizeof (FsBaseConference),
      0,
      (GInstanceInitFunc) fs_base_conference_init,
    };

    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) fs_base_conference_implements_interface_init,
      NULL,
      NULL,
    };

    static const GInterfaceInfo conference_info = {
      (GInterfaceInitFunc) fs_base_conference_interface_init,
      NULL,
      NULL,
    };



    base_conference_type = g_type_register_static (GST_TYPE_BIN,
        "FsBaseConference", &base_conference_info, G_TYPE_FLAG_ABSTRACT);

    g_type_add_interface_static (base_conference_type,
                                 GST_TYPE_IMPLEMENTS_INTERFACE,
                                 &iface_info);
    g_type_add_interface_static (base_conference_type, FS_TYPE_CONFERENCE,
                                 &conference_info);
  }
  return base_conference_type;
}

static void fs_base_conference_finalize (GObject *object);
static void fs_base_conference_set_property (GObject *object, guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec);
static void fs_base_conference_get_property (GObject *object, guint prop_id,
                                             GValue *value, GParamSpec *pspec);

static FsSession *fs_base_conference_new_session (FsConference *conf,
                                                  FsMediaType media_type,
                                                  GError **error);
static FsParticipant *fs_base_conference_new_participant (FsConference *conf,
                                                          gchar *cname);

void fs_base_conference_error (GObject *signal_src, GObject *error_src,
                               gint error_no, gchar *error_msg,
                               gchar *debug_msg, FsBaseConference *conf);

static void
fs_base_conference_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (fs_base_conference_debug, "fsbaseconference", 0,
      "farsight base conference element");
}

static void
fs_base_conference_finalize (GObject * object)
{
  FsBaseConference *conf;

  conf = FS_BASE_CONFERENCE (object);

  /* Let's check if we have any remaining sessions in this
   * conference, if we do we need to exit since this is a fatal error by the
   * user because it results in unusable children objects */
  if (conf->priv->session_list->len)
  {
    g_error ("You may not unref your Farsight Conference Gstreamer "
             "element without first unrefing all underlying sessions, "
             "and streams! Exiting");
  }

  g_ptr_array_free (conf->priv->session_list, TRUE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
fs_base_conference_class_init (FsBaseConferenceClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsBaseConferencePrivate));

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (fs_base_conference_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (fs_base_conference_get_property);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (fs_base_conference_finalize);
}

static void
fs_base_conference_init (FsBaseConference *conf,
    FsBaseConferenceClass *bclass)
{
  GST_DEBUG ("fs_base_conference_init");

  conf->priv = FS_BASE_CONFERENCE_GET_PRIVATE (conf);

  conf->priv->session_list = g_ptr_array_new();
}

static void
fs_base_conference_interface_init (gpointer g_iface,
                                   gpointer iface_data)
{
  FsConferenceInterface *iface = (FsConferenceInterface *)g_iface;

  iface->new_session = fs_base_conference_new_session;
  iface->new_participant = fs_base_conference_new_participant;
}

static gboolean
fs_base_conference_interface_supported (GstImplementsInterface * iface,
                                         GType type)
{
  g_assert (type == FS_TYPE_CONFERENCE);
  return TRUE;
}

static void
fs_base_conference_implements_interface_init (
    GstImplementsInterfaceClass * klass)
{
  klass->supported = fs_base_conference_interface_supported;
}


void _remove_session_ptr (FsBaseConference *conf, FsSession *session)
{
  if (!g_ptr_array_remove (conf->priv->session_list, session))
  {
    GST_WARNING_OBJECT (conf, "FsSession not found in session ptr array");
  }
}

static FsSession *
fs_base_conference_new_session (FsConference *conf,
                                FsMediaType media_type,
                                GError **error)
{
  FsBaseConferenceClass *klass = FS_BASE_CONFERENCE_GET_CLASS (conf);
  FsBaseConference *base_conf = FS_BASE_CONFERENCE (conf);

  FsSession *new_session = NULL;

  if (klass->new_session) {
    new_session = klass->new_session (base_conf, media_type, error);

    if (!new_session)
      return NULL;

    /* Let's catch all session errors and send them over the GstBus */
    g_signal_connect (new_session, "error",
        G_CALLBACK (fs_base_conference_error), base_conf);

    /* Let's add a ptr to the new session into our ptr array */
    g_ptr_array_add (base_conf->priv->session_list, new_session);

    /* Let's add a weak reference to our new session, this way if it gets
     * unrefed we can remove it from our ptr list */
    g_object_weak_ref (G_OBJECT (new_session), (GWeakNotify)_remove_session_ptr,
        base_conf);
  } else {
    GST_WARNING_OBJECT (conf, "new_session not defined in element");
    g_set_error (error, FS_SESSION_ERROR, FS_SESSION_ERROR_CONSTRUCTION,
      "new_session not defined in element");
  }

  return new_session;
}

void
fs_base_conference_error (GObject *signal_src, GObject *error_src,
                          gint error_no, gchar *error_msg,
                          gchar *debug_msg, FsBaseConference *conf)
{
  GstMessage *gst_msg = NULL;
  GstStructure *error_struct = NULL;

  error_struct = gst_structure_new ("farsight-error",
      "src-object", G_TYPE_OBJECT, error_src,
      "error-no", G_TYPE_INT, error_no,
      "error-msg", G_TYPE_STRING, error_msg,
      "debug-msg", G_TYPE_STRING, debug_msg,
      NULL);

  gst_msg = gst_message_new_custom (GST_MESSAGE_ERROR, GST_OBJECT (conf),
      error_struct);

  if (!gst_element_post_message (GST_ELEMENT (conf), gst_msg))
  {
    GST_WARNING_OBJECT (conf, "Could not post error on bus");
  }
}

static void
fs_base_conference_set_property (GObject *object, guint prop_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
}

static void
fs_base_conference_get_property (GObject *object, guint prop_id,
                                 GValue *value, GParamSpec *pspec)
{
}


static FsParticipant *
fs_base_conference_new_participant (FsConference *conf,
                                    gchar *cname)
{
  FsBaseConference *baseconf = FS_BASE_CONFERENCE (conf);
  FsBaseConferenceClass *klass = FS_BASE_CONFERENCE_GET_CLASS (conf);

  if (klass->new_participant) {
    return klass->new_participant (baseconf, cname);
  } else {
    GST_WARNING_OBJECT (conf, "new_session not defined in element");
  }

  return NULL;
}

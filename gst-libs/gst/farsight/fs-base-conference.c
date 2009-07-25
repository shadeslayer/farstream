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
 * SECTION:fs-base-conference
 * @short_description: Base class for Farsight Conference Gstreamer Elements
 *
 * This base class must be subclassed by all Farsight Conference elements. It makes
 * sure to agreggate the errors and maintain the lifecycles of the instances in
 * the API.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-base-conference.h"

#include "fs-session.h"

#include "fs-private.h"

GST_DEBUG_CATEGORY (fs_base_conference_debug);
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

/*
#define FS_BASE_CONFERENCE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), FS_TYPE_BASE_CONFERENCE, FsBaseConferencePrivate))

struct _FsBaseConferencePrivate
{
};
*/

GST_BOILERPLATE_WITH_INTERFACE (
    FsBaseConference, fs_base_conference,
    GstBin, GST_TYPE_BIN,
    FsConference, FS_TYPE_CONFERENCE, fs_conference);

static FsSession *fs_base_conference_new_session (FsConference *conf,
                                                  FsMediaType media_type,
                                                  GError **error);
static FsParticipant *fs_base_conference_new_participant (FsConference *conf,
    GError **error);

void fs_base_conference_error (GObject *signal_src, GObject *error_src,
                               FsError error_no, gchar *error_msg,
                               gchar *debug_msg, FsBaseConference *conf);

void
_fs_base_conference_init_debug (void)
{
  GST_DEBUG_CATEGORY_INIT (fs_base_conference_debug, "fsbaseconference", 0,
      "farsight base conference library");
}

static void
fs_base_conference_base_init (gpointer g_class)
{
  _fs_base_conference_init_debug ();
}

static void
fs_base_conference_class_init (FsBaseConferenceClass * klass)
{
}

static void
fs_base_conference_init (FsBaseConference *conf,
    FsBaseConferenceClass *bclass)
{
  GST_DEBUG ("fs_base_conference_init");

  // conf->priv = FS_BASE_CONFERENCE_GET_PRIVATE (conf);
}

static void
fs_conference_interface_init (FsConferenceClass *iface)
{
  iface->new_session = fs_base_conference_new_session;
  iface->new_participant = fs_base_conference_new_participant;
}

static gboolean
fs_conference_supported (
    FsBaseConference * self,
    GType type)
{
  g_assert (type == FS_TYPE_CONFERENCE);
  return TRUE;
}

static FsSession *
fs_base_conference_new_session (FsConference *conf,
                                FsMediaType media_type,
                                GError **error)
{
  FsBaseConferenceClass *klass;
  FsBaseConference *baseconf;
  FsSession *new_session = NULL;

  g_return_val_if_fail (conf, NULL);
  g_return_val_if_fail (FS_IS_BASE_CONFERENCE (conf), NULL);
  baseconf = FS_BASE_CONFERENCE (conf);
  klass = FS_BASE_CONFERENCE_GET_CLASS (conf);
  g_return_val_if_fail (klass->new_session, NULL);

  new_session = klass->new_session (baseconf, media_type, error);

  if (!new_session)
    return NULL;

  /* Let's catch all session errors and send them over the GstBus */
  g_signal_connect_object (new_session, "error",
      G_CALLBACK (fs_base_conference_error), baseconf, 0);

  return new_session;
}

void
fs_base_conference_error (GObject *signal_src, GObject *error_src,
                          FsError error_no, gchar *error_msg,
                          gchar *debug_msg, FsBaseConference *conf)
{
  GstMessage *gst_msg = NULL;
  GstStructure *error_struct = NULL;

  if (debug_msg == NULL)
    debug_msg = error_msg;

  error_struct = gst_structure_new ("farsight-error",
      "src-object", G_TYPE_OBJECT, error_src,
      "error-no", FS_TYPE_ERROR, error_no,
      "error-msg", G_TYPE_STRING, error_msg,
      "debug-msg", G_TYPE_STRING, debug_msg,
      NULL);

  gst_msg = gst_message_new_element (GST_OBJECT (conf), error_struct);

  if (!gst_element_post_message (GST_ELEMENT (conf), gst_msg))
  {
    GST_WARNING_OBJECT (conf, "Could not post error on bus");
  }
}


static FsParticipant *
fs_base_conference_new_participant (FsConference *conf,
    GError **error)
{
  FsBaseConference *baseconf;
  FsBaseConferenceClass *klass;

  g_return_val_if_fail (conf, NULL);
  g_return_val_if_fail (FS_IS_BASE_CONFERENCE (conf), NULL);
  baseconf = FS_BASE_CONFERENCE (conf);
  klass = FS_BASE_CONFERENCE_GET_CLASS (conf);
  g_return_val_if_fail (klass->new_participant, NULL);

  return klass->new_participant (baseconf, error);
}

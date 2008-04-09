/*
 * Farsight2 - GStreamer interfaces
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-conference-iface.c - GStreamer interface to be implemented by farsight
 *                         conference elements
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-conference-iface.h"

/**
 * SECTION:fs-conference-iface
 * @short_description: Interface for farsight conference elements
 *
 * This interface is implemented by the FsBaseConference base class element. A
 * Farsight conference is a conversation space that takes place between 2 or
 * more participants. Each conference must have one or more Farsight sessions
 * that are associated to the conference participants. Different protocols
 * simply need to derive from the FsBaseConference class and don't need to
 * implement this interface directly.
 *
 *
 * This will communicate asynchronous events to the user through #GstMessage
 * of type #GST_MESSAGE_ELEMENT sent over the #GstBus.
 * </para>
 * <refsect2><title>The "<literal>farsight-error</literal>" message</title>
 * |[
 * "src-object"       #GObject           The object (#FsConference, #FsSession or #FsStream) that emitted the error
 * "error-no"         #FsError           The Error number
 * "error-msg"        #gchar*            The error message
 * "debug-msg"        #gchar*            The debug string
 * ]|
 * <para>
 * The message is sent on asynchronous errors.
 * </para>
 * </refsect2>
 * <para>
 */

static void fs_conference_iface_init (FsConferenceClass *iface);

GType
fs_conference_get_type (void)
{
  static GType fs_conference_type = 0;

  if (!fs_conference_type) {
    static const GTypeInfo fs_conference_info = {
      sizeof (FsConferenceClass),
      (GBaseInitFunc) fs_conference_iface_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
    };

    fs_conference_type = g_type_register_static (G_TYPE_INTERFACE,
        "FsConference", &fs_conference_info, 0);
    g_type_interface_add_prerequisite (fs_conference_type,
        GST_TYPE_IMPLEMENTS_INTERFACE);
  }

  return fs_conference_type;
}


GQuark
fs_error_quark (void)
{
  return g_quark_from_static_string ("fs-error");
}


static void
fs_conference_iface_init (FsConferenceClass * iface)
{
  /* default virtual functions */
  iface->new_session = NULL;
  iface->new_participant = NULL;
}

/**
 * fs_conference_new_session
 * @conference: #FsConference interface of a #GstElement
 * @media_type: #FsMediaType of the new session
 * @error: location of a #GError, or %NULL if no error occured
 *
 * Create a new Farsight session for the given conference.
 *
 * Returns: the new #FsSession that has been created. The #FsSession must be
 * unref'd by the user when closing the session.
 */
FsSession *
fs_conference_new_session (FsConference *conference, FsMediaType media_type,
                           GError **error)
{
  FsConferenceClass *iface =
      FS_CONFERENCE_GET_IFACE (conference);

  if (iface->new_session) {
    return iface->new_session (conference, media_type, error);
  } else {
    GST_WARNING_OBJECT (conference, "new_session not defined in element");
  }
  return NULL;
}


/**
 * fs_conference_new_participant
 * @conference: #FsConference interface of a #GstElement
 * @cname: The cname of the participant
 * @error: location of a #GError, or %NULL if no error occured
 *
 * Create a new Farsight Participant for the type of the given conference.
 *
 * Returns: the new #FsParticipant that has been created. The #FsParticipant
 * is owned by the user and he must unref it when he is done with it.
 */
FsParticipant *
fs_conference_new_participant (FsConference *conference, gchar *cname,
    GError **error)
{
  FsConferenceClass *iface =
      FS_CONFERENCE_GET_IFACE (conference);

  if (iface->new_session) {
    return iface->new_participant (conference, cname, error);
  } else {
    GST_WARNING_OBJECT (conference, "new_participant not defined in element");
  }
  return NULL;
}

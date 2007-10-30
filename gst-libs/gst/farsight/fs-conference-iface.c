/*
 * Farsight2 - GStreamer interfaces
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-conference.c - GStreamer interface to be implemented by farsight
 *                   conference elements
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
 */

static void fs_conference_iface_init (FsConferenceInterface *iface);

GType
fs_conference_get_type (void)
{
  static GType fs_conference_type = 0;

  if (!fs_conference_type) {
    static const GTypeInfo fs_conference_info = {
      sizeof (FsConferenceInterface),
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

static void
fs_conference_iface_init (FsConferenceInterface * iface)
{
  /* default virtual functions */
  iface->new_session = NULL;
}

/**
 * fs_conference_new_session
 * @conference: #FsConference interface of a #GstElement
 * @media_type: #FsMediaType of the new session
 *
 * Create a new Farsight session for the given conference.
 *
 * Returns: the new #FsSession that has been created. The #FsSession must be
 * unref'd by the user when closing the session.
 */
FsSession *
fs_conference_new_session (FsConference *conference, FsMediaType media_type)
{
  FsConferenceInterface *iface =
      FS_CONFERENCE_GET_IFACE (conference);

  if (iface->new_session) {
    return iface->new_session (conference, media_type);
  } else {
    GST_WARNING ("new_session not defined in element");
  }
  return NULL;
}

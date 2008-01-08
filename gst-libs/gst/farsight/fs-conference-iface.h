/*
 * Farsight2 - GStreamer interfaces
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-conference-iface.h - Header file for gstreamer interface to be
 *                         implemented by farsight conference elements
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

#ifndef __FS_CONFERENCE_H__
#define __FS_CONFERENCE_H__

#include <gst/gst.h>
#include <gst/interfaces/interfaces-enumtypes.h>

#include <gst/farsight/fs-session.h>
#include <gst/farsight/fs-codec.h>

G_BEGIN_DECLS

#define FS_TYPE_CONFERENCE \
  (fs_conference_get_type ())
#define FS_CONFERENCE(obj) \
  (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_CAST ((obj), FS_TYPE_CONFERENCE, FsConference))
#define FS_IS_CONFERENCE(obj) \
  (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_TYPE ((obj), FS_TYPE_CONFERENCE))
#define FS_CONFERENCE_GET_IFACE(inst) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), FS_TYPE_CONFERENCE, FsConferenceClass))

/**
 * FsConference:
 *
 * Opaque #FsConference data structure.
 */
typedef struct _FsConference FsConference;

typedef struct _FsConferenceClass FsConferenceClass;

/**
 * FsConferenceClass:
 * @parent: parent interface type.
 * @new_session: virtual method to create a new conference session
 * @new_participant: virtual method to create a new participant
 *
 * #FsConferenceClass interface.
 */
struct _FsConferenceClass {
  GTypeInterface parent;

  /* virtual functions */
  FsSession *(* new_session) (FsConference *conference, FsMediaType media_type,
                              GError **error);

  FsParticipant *(* new_participant) (FsConference *conference,
      gchar *cname,
      GError **error);

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

GType fs_conference_get_type (void);



/**
 * FsError:
 * @FS_ERROR_CONSTRUCTION: Error constructing some of the sub-elements
 * @FS_ERROR_INVALID_ARGUMENTS: Invalid arguments to the function
 * @FS_ERROR_NETWORK: A network related error
 * @FS_ERROR_INTERNAL: An internal error happened in Farsight
 * @FS_ERROR_NOT_IMPLEMENTED: This functionality is not implemented
 * by this plugins
 * @FS_ERROR_NEGOTIATION_FAILED: The codec negotiation has failed
 * @FS_ERROR_UNKNOWN_CODEC: The codec is unknown
 * @FS_ERROR_UNKNOWN_CNAME: Data was received for an unknown cname
 *
 * This is the enum of error numbers that will come either on the "error"
 * signal or from the Gst Bus.
 */

typedef enum {
  FS_ERROR_CONSTRUCTION,
  FS_ERROR_INVALID_ARGUMENTS,
  FS_ERROR_INTERNAL,
  FS_ERROR_NETWORK,
  FS_ERROR_NOT_IMPLEMENTED,
  FS_ERROR_NEGOTIATION_FAILED,
  FS_ERROR_UNKNOWN_CODEC,
  FS_ERROR_UNKNOWN_CNAME
} FsError;

/**
 * FS_ERROR:
 *
 * This quark is used to denote errors coming from Farsight objects
 */

#define FS_ERROR (fs_error_quark ())

GQuark fs_error_quark (void);

/* virtual class function wrappers */
FsSession *fs_conference_new_session (FsConference *conference,
                                      FsMediaType media_type,
                                      GError **error);

FsParticipant *fs_conference_new_participant (FsConference *conference,
    gchar *cname,
    GError **error);


G_END_DECLS

#endif /* __FS_CONFERENCE_H__ */



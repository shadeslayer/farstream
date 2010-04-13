/*
 * Farsight2 - Farsight MSN Conference Implementation
 *
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-send-conference.c - MSN implementation for Farsight Conference
 *   Gstreamer Elements
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
 * SECTION:element-fsmsncamsendconference
 * @short_description: Farsight MSN send Conference Gstreamer Element
 *
 * This element implements the unidirection webcam feature found in various
 * version of MSN Messenger (tm) and Windows Live Messenger (tm). This is
 * to send the local webcam's video to someone else.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-msn-cam-send-conference.h"

#include "fs-msn-conference.h"
#include "fs-msn-session.h"
#include "fs-msn-stream.h"
#include "fs-msn-participant.h"

#define GST_CAT_DEFAULT fsmsnconference_debug

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


static GstElementDetails fs_msn_cam_send_conference_details =
{
  "Farsight MSN Sending Conference",
  "Generic/Bin/MSN",
  "A Farsight MSN Sending Conference",
  "Richard Spiers <richard.spiers@gmail.com>, "
  "Youness Alaoui <youness.alaoui@collabora.co.uk>, "
  "Olivier Crete <olivier.crete@collabora.co.uk>"
};


static void fs_msn_cam_send_conference_do_init (GType type);

GST_BOILERPLATE_FULL (FsMsnCamSendConference, fs_msn_cam_send_conference,
    FsMsnConference, FS_TYPE_MSN_CONFERENCE, fs_msn_cam_send_conference_do_init);

static void
fs_msn_cam_send_conference_do_init (GType type)
{
}

static void
fs_msn_cam_send_conference_class_init (FsMsnCamSendConferenceClass * klass)
{
}

static void
fs_msn_cam_send_conference_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &fs_msn_cam_send_conference_details);
}

static void
fs_msn_cam_send_conference_init (FsMsnCamSendConference *self,
                        FsMsnCamSendConferenceClass *bclass)
{
  FsMsnConference *conf = FS_MSN_CONFERENCE (self);
  GstElementFactory *fact = NULL;

  GST_DEBUG_OBJECT (conf, "fs_msn_cam_send_conference_init");

  conf->max_direction = FS_DIRECTION_SEND;

  fact = gst_element_factory_find ("mimenc");
  if (fact)
    gst_object_unref (fact);
  else
    g_set_error (&conf->missing_element_error,
        FS_ERROR, FS_ERROR_CONSTRUCTION,
        "mimenc missing");
}


/*
 * Farsight2 - Farsight MSN Conference Implementation
 *
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * fs-msn-recv-conference.c - MSN implementation for Farsight Conference
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
 * SECTION:element-fsmsncamrecvconference
 * @short_description: Farsight MSN Receive Conference Gstreamer Element
 *
 * This element implements the unidirection webcam feature found in various
 * version of MSN Messenger (tm) and Windows Live Messenger (tm). This is
 * to receive someone else's webcam.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-msn-cam-recv-conference.h"
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


static GstElementDetails fs_msn_cam_recv_conference_details =
{
  "Farsight MSN Reception Conference",
  "Generic/Bin/MSN",
  "A Farsight MSN Reception Conference",
  "Richard Spiers <richard.spiers@gmail.com>, "
  "Youness Alaoui <youness.alaoui@collabora.co.uk>, "
  "Olivier Crete <olivier.crete@collabora.co.uk>"
};


static void fs_msn_cam_recv_conference_do_init (GType type);

GST_BOILERPLATE_FULL (FsMsnCamRecvConference, fs_msn_cam_recv_conference,
    FsMsnConference, FS_TYPE_MSN_CONFERENCE, fs_msn_cam_recv_conference_do_init);

static void
fs_msn_cam_recv_conference_do_init (GType type)
{
}

static void
fs_msn_cam_recv_conference_class_init (FsMsnCamRecvConferenceClass * klass)
{
}

static void
fs_msn_cam_recv_conference_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &fs_msn_cam_recv_conference_details);
}

static void
fs_msn_cam_recv_conference_init (FsMsnCamRecvConference *self,
                        FsMsnCamRecvConferenceClass *bclass)
{
  FsMsnConference *conf = FS_MSN_CONFERENCE (self);

  GST_DEBUG_OBJECT (conf, "fs_msn_cam_recv_conference_init");

  conf->max_direction = FS_DIRECTION_RECV;
}


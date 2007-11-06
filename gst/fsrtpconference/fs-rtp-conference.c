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
 * SECTION:fs-rtp-conference
 * @short_description: FarsightRTP Conference Gstreamer Elements
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtp-conference.h"
#include "fs-rtp-participant.h"

GST_DEBUG_CATEGORY_STATIC (fs_rtp_conference_debug);
#define GST_CAT_DEFAULT fs_rtp_conference_debug

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

#define FS_RTP_CONFERENCE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), FS_TYPE_RTP_CONFERENCE, FsRtpConferencePrivate))

struct _FsRtpConferencePrivate
{
};

static GstElementClass *parent_class = NULL;

static void fs_rtp_conference_base_init (gpointer g_class);
static void fs_rtp_conference_class_init (FsRtpConferenceClass *klass);
static void fs_rtp_conference_init (FsRtpConference *conf,
                                     FsRtpConferenceClass *klass);
static void fs_rtp_conference_implements_interface_init (
    GstImplementsInterfaceClass * klass);

GType
fs_rtp_conference_get_type (void)
{
  static GType rtp_conference_type = 0;

  if (!rtp_conference_type) {
    static const GTypeInfo rtp_conference_info = {
      sizeof (FsRtpConferenceClass),
      (GBaseInitFunc) fs_rtp_conference_base_init,
      NULL,
      (GClassInitFunc) fs_rtp_conference_class_init,
      NULL,
      NULL,
      sizeof (FsRtpConference),
      0,
      (GInstanceInitFunc) fs_rtp_conference_init,
    };


    rtp_conference_type = g_type_register_static (FS_TYPE_BASE_CONFERENCE,
        "FsRtpConference", &rtp_conference_info, 0);
  }

  return rtp_conference_type;
}

static void fs_rtp_conference_finalize (GObject *object);
static FsSession *fs_rtp_conference_new_session (FsBaseConference *conf,
                                                 FsMediaType media_type);
static FsParticipant *fs_rtp_conference_new_participant (FsBaseConference *conf,
                                                         gchar *cname);


static void
fs_rtp_conference_rtp_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (fs_rtp_conference_debug, "fsrtpconference", 0,
      "farsight rtp conference element");
}

static void
fs_rtp_conference_finalize (GObject * object)
{
  FsRtpConference *conf = FS_RTP_CONFERENCE (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
fs_rtp_conference_class_init (FsRtpConferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  FsBaseConferenceClass *baseconf_class = FS_BASE_CONFERENCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsRtpConferencePrivate));

  parent_class = g_type_class_peek_parent (klass);

  baseconf_class->new_session = fs_rtp_conference_new_session;
  baseconf_class->new_participant = fs_rtp_conference_new_participant;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (fs_rtp_conference_finalize);
}

static void
fs_rtp_conference_init (FsRtpConference *conf,
    FsRtpConferenceClass *bclass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG ("fs_rtp_conference_init");

  conf->priv = FS_RTP_CONFERENCE_GET_PRIVATE (conf);
}


static FsSession *
fs_rtp_conference_new_session (FsBaseConference *conf,
                               FsMediaType media_type)
{
  FsRtpConference *rtp_conf = FS_RTP_CONFERENCE (conf);

  FsSession *new_session = NULL;

  new_session = FS_SESSION_CAST (fs_rtp_session_new (media_type));

  return new_session;
}


static FsParticipant *
fs_rtp_conference_new_participant (FsBaseConference *conf,
                                   gchar *cname)
{
  FsRtpConference *rtp_conf = FS_RTP_CONFERENCE (conf);

  FsParticipant *new_participant = NULL;

  new_participant = FS_PARTICIPANT_CAST (fs_rtp_participant_new (cname));

  return new_participant;
}

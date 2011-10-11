/*
 * Farstream - Farstream RTP Participant
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-participant.c - A RTP Farstream Participant gobject
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
 * SECTION:fs-rtp-participant
 * @short_description: A RTP participant in a #FsRtpConference
 *
 * This object represents one participant or person in a RTP conference
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtp-participant.h"

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_CNAME
};

G_DEFINE_TYPE(FsRtpParticipant, fs_rtp_participant, FS_TYPE_PARTICIPANT);


struct _FsRtpParticipantPrivate
{
  gchar *cname;
};

#define FS_RTP_PARTICIPANT_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_PARTICIPANT, \
   FsRtpParticipantPrivate))

static void fs_rtp_participant_finalize (GObject *object);

static void fs_rtp_participant_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_rtp_participant_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);

// static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_rtp_participant_class_init (FsRtpParticipantClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = fs_rtp_participant_set_property;
  gobject_class->get_property = fs_rtp_participant_get_property;
  gobject_class->finalize = fs_rtp_participant_finalize;

  /**
   * FsParticipant:cname:
   *
   * A string representing the cname of the current participant.
   * User must free the string after getting it.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_CNAME,
      g_param_spec_string ("cname",
        "The cname of the participant",
        "A string of the cname of the participant",
        NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (klass, sizeof (FsRtpParticipantPrivate));
}

static void
fs_rtp_participant_init (FsRtpParticipant *self)
{
  /* member init */
  self->priv = FS_RTP_PARTICIPANT_GET_PRIVATE (self);
}

static void
fs_rtp_participant_finalize (GObject *object)
{
  FsRtpParticipant *self = FS_RTP_PARTICIPANT (object);

  if (self->priv->cname) {
    g_free (self->priv->cname);
    self->priv->cname = NULL;
  }

  G_OBJECT_CLASS (fs_rtp_participant_parent_class)->finalize (object);
}


static void
fs_rtp_participant_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsRtpParticipant *self = FS_RTP_PARTICIPANT (object);

  switch (prop_id) {
    case PROP_CNAME:
      g_value_set_string (value, self->priv->cname);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rtp_participant_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  FsRtpParticipant *self = FS_RTP_PARTICIPANT (object);

  switch (prop_id) {
    case PROP_CNAME:
      self->priv->cname = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

FsRtpParticipant *fs_rtp_participant_new (void)
{
  return g_object_new (FS_TYPE_RTP_PARTICIPANT, NULL);
}

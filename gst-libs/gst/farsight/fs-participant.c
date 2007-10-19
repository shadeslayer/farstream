/*
 * Farsight2 - Farsight Participant
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-participant.c - A Farsight Participant gobject (base implementation)
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
 * SECTION:fs-participant
 * @short_description: A participant in a conference
 *
 * This object is the base implementation of a Farsight Participant. It needs to be
 * derived and implemented by a farsight conference gstreamer element. A
 * participant represents any source of media in a conference. This could be a
 * human-participant or an automaton.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-participant.h"
#include "fs-marshal.h"

/* Signals */
enum
{
  ERROR,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_CNAME
};

struct _FsParticipantPrivate
{
  gboolean disposed;

  gchar *cname;
};

#define FS_PARTICIPANT_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_PARTICIPANT, \
   FsParticipantPrivate))

static void fs_participant_class_init (FsParticipantClass *klass);
static void fs_participant_init (FsParticipant *self);
static void fs_participant_dispose (GObject *object);
static void fs_participant_finalize (GObject *object);

static void fs_participant_get_property (GObject *object,
                                         guint prop_id,
                                         GValue *value,
                                         GParamSpec *pspec);
static void fs_participant_set_property (GObject *object,
                                         guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_participant_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsParticipantClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_participant_class_init,
      NULL,
      NULL,
      sizeof (FsParticipant),
      0,
      (GInstanceInitFunc) fs_participant_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsParticipant", &info, 0);
  }

  return type;
}

static void
fs_participant_class_init (FsParticipantClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_participant_set_property;
  gobject_class->get_property = fs_participant_get_property;

  /**
   * FsParticipant:cname:
   *
   * A string representing the cname of the current participant. This is a
   * constructor parameter that cannot be changed afterwards. User must free the
   * string after getting it.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_CNAME,
      g_param_spec_string ("cname",
        "The cname of the participant",
        "A string of the cname of the participant",
        NULL,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  /**
   * FsParticipant::error:
   * @self: #FsParticipant that emitted the signal
   * @object: The #Gobject that emitted the signal
   * @errorno: The number of the error 
   * @error_msg: Error message to be displayed to user
   * @dbg_msg: Debugging error message
   *
   * This signal is emitted in any error condition
   */
  signals[ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      fs_marshal_VOID__OBJECT_INT_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_OBJECT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

  gobject_class->dispose = fs_participant_dispose;
  gobject_class->finalize = fs_participant_finalize;

  g_type_class_add_private (klass, sizeof (FsParticipantPrivate));
}

static void
fs_participant_init (FsParticipant *self)
{
  /* member init */
  self->priv = FS_PARTICIPANT_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_participant_dispose (GObject *object)
{
  FsParticipant *self = FS_PARTICIPANT (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_participant_finalize (GObject *object)
{
  g_signal_handlers_destroy (object);

  parent_class->finalize (object);
}

static void
fs_participant_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
}

static void
fs_participant_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
}

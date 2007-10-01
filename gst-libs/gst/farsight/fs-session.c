/*
 * Farsight2 - Farsight Session
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-session.c - A Farsight Session gobject (base implementation)
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
 * SECTION:FsSession
 * @short_description: A gobject representing a farsight session
 *
 * This object is the base implementation of a Farsight Session. It needs to be
 * derived and implemented by a farsight conference gstreamer element. A
 * Farsight session is defined in the same way as an RTP session. It can contain
 * one or more participants but represents only one media stream (i.e. One
 * session for video and one session for audio in an AV conference). Sessions
 * contained in the same conference should be synchronised together during
 * playback.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-session.h"
#include "fs-marshal.h"

/* Signals */
enum
{
  ERROR,
  SINK_PAD_READY,
  LAST_SIGNAL
};

/* props */
enum
{
  ARG_0
};

struct _FsPrivate
{
  gboolean disposed;
};

#define FS_SESSION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_SESSION, FsSessionPrivate))

static void fs_session_class_init (FsSessionClass *klass);
static void fs_session_init (FsSession *self);
static void fs_session_dispose (GObject *object);
static void fs_session_finalize (GObject *object);

static void fs_session_get_property (GObject *object, 
                                          guint prop_id, 
                                          GValue *value,
                                          GParamSpec *pspec);
static void fs_session_set_property (GObject *object, 
                                          guint prop_id,
                                          const GValue *value, 
                                          GParamSpec *pspec);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_session_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsSessionClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_session_class_init,
      NULL,
      NULL,
      sizeof (FsSession),
      0,
      (GInstanceInitFunc) fs_session_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsSession", &info, 0);
  }

  return type;
}

static void
fs_session_class_init (FsSessionClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_session_set_property;
  gobject_class->get_property = fs_session_get_property;

  /**
   * FsSession::error:
   * @self: #FsSession that emmitted the signal
   * @errorno: The number of the error 
   * @message: Error message to be displayed to user
   * @message: Debugging error message
   *
   * This signal is emitted in any error condition
   */
  signals[ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      fs_marshal_VOID__INT_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

  /**
   * FsSession::sink-pad-ready:
   * @self: #FsSession that emmitted the signal
   * @pad: A GstPad that represents the sink
   *
   * This signal is emitted when a sink pad has been created on the Farsight
   * conference bin for this session.
   */
  signals[SINK_PAD_READY] = g_signal_new ("sink-pad-ready",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__POINTER,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  gobject_class->dispose = fs_session_dispose;
  gobject_class->finalize = fs_session_finalize;

  g_type_class_add_private (klass, sizeof (FsSessionPrivate));
}

static void
fs_session_init (FsSession *self)
{
  /* member init */
  self->priv = FS_SESSION_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_session_dispose (GObject *object)
{
  FsSession *self = FS_SESSION (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_session_finalize (GObject *object)
{
  g_signal_handlers_destroy (object);

  parent_class->finalize (object);
}

/*
 * Farsight2 - Farsight Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-stream.c - A Farsight Stream gobject (base implementation)
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
 * SECTION:FsStream
 * @short_description: A gobject representing a stream inside a session
 *
 * This object is the base implementation of a Farsight Stream. It
 * needs to be derived and implemented by a farsight conference gstreamer
 * element. A Farsight Stream is a media stream originating from a participant
 * inside a session. In fact, a FarsightStream instance is obtained by adding a
 * participant into a session using #fs_session_add_participant.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-stream.h"
#include "fs-marshal.h"

/* Signals */
enum
{
  ERROR,
  SRC_PAD_ADDED,
  RECV_CODEC_CHANGED,
  CURRENT_CANDIDATE_PAIR,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0
};

struct _FsStreamPrivate
{
  gboolean disposed;
};

#define FS_STREAM_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_SESSION, FsStreamPrivate))

static void fs_stream_class_init (FsStreamClass *klass);
static void fs_stream_init (FsStream *self);
static void fs_stream_dispose (GObject *object);
static void fs_stream_finalize (GObject *object);

static void fs_stream_get_property (GObject *object, 
                                          guint prop_id, 
                                          GValue *value,
                                          GParamSpec *pspec);
static void fs_stream_set_property (GObject *object, 
                                          guint prop_id,
                                          const GValue *value, 
                                          GParamSpec *pspec);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_stream_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsStreamClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_stream_class_init,
      NULL,
      NULL,
      sizeof (FsStream),
      0,
      (GInstanceInitFunc) fs_stream_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsStream", &info, 0);
  }

  return type;
}

static void
fs_stream_class_init (FsStreamClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_stream_set_property;
  gobject_class->get_property = fs_stream_get_property;

  /**
   * FsStream::error:
   * @self: #FsStream that emmitted the signal
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

  gobject_class->dispose = fs_stream_dispose;
  gobject_class->finalize = fs_stream_finalize;

  g_type_class_add_private (klass, sizeof (FsStreamPrivate));
}

static void
fs_stream_init (FsStream *self)
{
  /* member init */
  self->priv = FS_STREAM_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_stream_dispose (GObject *object)
{
  FsStream *self = FS_STREAM (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_stream_finalize (GObject *object)
{
  g_signal_handlers_destroy (object);

  parent_class->finalize (object);
}

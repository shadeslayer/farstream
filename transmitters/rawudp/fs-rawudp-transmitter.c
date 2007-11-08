/*
 * Farsight2 - Farsight RAW UDP with STUN Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rawudp-transmitter.h - A Farsight UDP transmitter with STUN
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
 * SECTION:fs-rawudp-transmitter
 * @short_description: A transmitter for raw udp (with STUN)
 *
 * This transmitter provides RAW udp (with stun)
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rawudp-transmitter.h"

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_GST_SINK,
  PROP_GST_SRC,
};

struct _FsRawUdpTransmitterPrivate
{
  gboolean disposed;
};

#define FS_RAWUDP_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAWUDP_TRANSMITTER, FsRawUdpTransmitterPrivate))

static void fs_rawudp_transmitter_class_init (FsRawUdpTransmitterClass *klass);
static void fs_rawudp_transmitter_init (FsRawUdpTransmitter *self);
static void fs_rawudp_transmitter_dispose (GObject *object);
static void fs_rawudp_transmitter_finalize (GObject *object);

static void fs_rawudp_transmitter_get_property (GObject *object,
                                         guint prop_id,
                                         GValue *value,
                                         GParamSpec *pspec);
static void fs_rawudp_transmitter_set_property (GObject *object,
                                         guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec);

static FsStreamTransmitter *fs_rawudp_transmitter_new_stream_transmitter (
    FsTransmitter *transmitter, FsParticipant *participant);


static GObjectClass *parent_class = NULL;
//static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_rawudp_transmitter_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsRawUdpTransmitterClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_rawudp_transmitter_class_init,
      NULL,
      NULL,
      sizeof (FsRawUdpTransmitter),
      0,
      (GInstanceInitFunc) fs_rawudp_transmitter_init
    };

    type = g_type_register_static (FS_TYPE_TRANSMITTER,
        "FsRawUdpTransmitter", &info, 0);
  }

  return type;
}

static void
fs_rawudp_transmitter_class_init (FsRawUdpTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsTransmitterClass *transmitter_class = FS_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_rawudp_transmitter_set_property;
  gobject_class->get_property = fs_rawudp_transmitter_get_property;

  g_object_class_override_property (gobject_class, PROP_GST_SRC, "gst-src");
  g_object_class_override_property (gobject_class, PROP_GST_SINK, "gst-sink");

  transmitter_class->new_stream_transmitter =
    fs_rawudp_transmitter_new_stream_transmitter;

  gobject_class->dispose = fs_rawudp_transmitter_dispose;
  gobject_class->finalize = fs_rawudp_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsRawUdpTransmitterPrivate));
}

static void
fs_rawudp_transmitter_init (FsRawUdpTransmitter *self)
{
  /* member init */
  self->priv = FS_RAWUDP_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_rawudp_transmitter_dispose (GObject *object)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rawudp_transmitter_finalize (GObject *object)
{
  parent_class->finalize (object);
}

static void
fs_rawudp_transmitter_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
}

static void
fs_rawudp_transmitter_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
}


/**
 * fs_rawudp_transmitter_new_stream_rawudp_transmitter:
 * @transmitter: a #FsTranmitter
 * @participant: the #FsParticipant for which the #FsStream using this
 * new #FsStreamTransmitter is created
 *
 * This function will create a new #FsStreamTransmitter element for a
 * specific participant for this #FsRawUdpTransmitter
 *
 * Returns: a new #FsStreamTransmitter
 */

static FsStreamTransmitter *
fs_rawudp_transmitter_new_stream_transmitter (FsTransmitter *transmitter,
  FsParticipant *participant)
{

  return NULL;
}

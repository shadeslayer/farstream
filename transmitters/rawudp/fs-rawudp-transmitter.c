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

#include <gst/farsight/fs-session.h>


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
  PROP_GST_SRC
};

struct _FsRawUdpTransmitterPrivate
{
  /* We hold references to this element */
  GstElement *gst_sink;
  GstElement *gst_src;

  /* We don't hold a reference to these elements, they are owned
     by the bins */
  GstElement *udpsrc_funnel;
  GstElement *udprtcpsrc_funnel;
  GstElement *udpsink_tee;
  GstElement *udprtcpsink_tee;

  gboolean disposed;
};

#define FS_RAWUDP_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAWUDP_TRANSMITTER, \
    FsRawUdpTransmitterPrivate))

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
  FsTransmitter *trans = FS_TRANSMITTER_CAST (self);
  GstPad *pad = NULL;
  GstPad *ghostpad = NULL;

  /* member init */
  self->priv = FS_RAWUDP_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  /* First we need the src elemnet */

  self->priv->gst_src = gst_element_factory_make ("bin", NULL);

  if (!self->priv->gst_src) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not build the transmitter src bin");
    return;
  }

  /* Lets create the RTP source funnel */

  self->priv->udpsrc_funnel = gst_element_factory_make ("fsfunnel", NULL);

  if (!self->priv->udpsrc_funnel) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not make the fsfunnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->gst_src), self->priv->udpsrc_funnel)) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the fsfunnel element to the transmitter src bin");
  }

  pad = gst_element_get_static_pad (self->priv->udpsrc_funnel, "src");
  ghostpad = gst_ghost_pad_new ("src", pad);
  gst_object_unref (pad);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (self->priv->gst_src, ghostpad);

  /* Lets create the RTCP source funnel*/

  self->priv->udprtcpsrc_funnel = gst_element_factory_make ("fsfunnel", NULL);

  if (!self->priv->udprtcpsrc_funnel) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not make the fsfunnnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->gst_src),
      self->priv->udprtcpsrc_funnel)) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the rtcp fsfunnel element to the transmitter src bin");
  }

  pad = gst_element_get_static_pad (self->priv->udprtcpsrc_funnel, "src");
  ghostpad = gst_ghost_pad_new ("rtcpsrc", pad);
  gst_object_unref (pad);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (self->priv->gst_src, ghostpad);


  /* Second, we do the sink element */

  self->priv->gst_sink = gst_element_factory_make ("bin", NULL);

  if (!self->priv->gst_sink) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not build the transmitter sink bin");
    return;
  }

  /* Lets create the RTP source tee */

  self->priv->udpsink_tee = gst_element_factory_make ("tee", NULL);

  if (!self->priv->udpsink_tee) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not make the tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->gst_sink), self->priv->udpsink_tee)) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the tee element to the transmitter sink bin");
  }

  pad = gst_element_get_static_pad (self->priv->udpsink_tee, "sink");
  ghostpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (self->priv->gst_sink, ghostpad);

  /* Lets create the RTCP source tee*/

  self->priv->udprtcpsink_tee = gst_element_factory_make ("tee", NULL);

  if (!self->priv->udprtcpsink_tee) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not make the fsfunnnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->gst_sink),
      self->priv->udprtcpsink_tee)) {
    trans->construction_error = g_error_new (FS_SESSION_ERROR,
      FS_SESSION_ERROR_CONSTRUCTION,
      "Could not add the rtcp tee element to the transmitter sink bin");
  }

  pad = gst_element_get_static_pad (self->priv->udprtcpsink_tee, "sink");
  ghostpad = gst_ghost_pad_new ("rtcpsink", pad);
  gst_object_unref (pad);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (self->priv->gst_sink, ghostpad);
}

static void
fs_rawudp_transmitter_dispose (GObject *object)
{
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  if (self->priv->gst_src) {
    gst_object_unref (self->priv->gst_src);
    self->priv->gst_src = NULL;
  }

  if (self->priv->gst_sink) {
    gst_object_unref (self->priv->gst_sink);
    self->priv->gst_sink = NULL;
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
  FsRawUdpTransmitter *self = FS_RAWUDP_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_GST_SINK:
      g_value_set_object (value, self->priv->gst_sink);
      break;
    case PROP_GST_SRC:
      g_value_set_object (value, self->priv->gst_src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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

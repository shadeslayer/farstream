/*
 * Farsight2 - Farsight libnice Stream Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-nice-stream-transmitter.c - A Farsight libnice stream transmitter
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
 * SECTION:fs-nice-stream-transmitter
 * @short_description: A stream transmitter object for ICE using libnice
 * @see_also: fs-rawudp-stream-transmitter
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-nice-stream-transmitter.h"
#include "fs-nice-transmitter.h"

#include <gst/farsight/fs-candidate.h>
#include <gst/farsight/fs-conference-iface.h>

#include <gst/gst.h>

#include <string.h>
#include <sys/types.h>

GST_DEBUG_CATEGORY_EXTERN (fs_nice_transmitter_debug);
#define GST_CAT_DEFAULT fs_nice_transmitter_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_SENDING,
  PROP_PREFERRED_LOCAL_CANDIDATES,
  PROP_STUN_IP,
  PROP_STUN_PORT,
  PROP_TURN_IP,
  PROP_TURN_PORT,
  PROP_CONTROLLING_MODE,
  PROP_COMPATIBILITY
};

struct _FsNiceStreamTransmitterPrivate
{
  guint stream_id;

  FsNiceTransmitter *transmitter;

  gboolean sending;

  gchar *stun_ip;
  guint stun_port;
  gchar *turn_ip;
  guint turn_port;

  gboolean compatibility;
  gboolean controlling_mode;
};

#define FS_NICE_STREAM_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_NICE_STREAM_TRANSMITTER, \
                                FsNiceStreamTransmitterPrivate))

static void fs_nice_stream_transmitter_class_init (FsNiceStreamTransmitterClass *klass);
static void fs_nice_stream_transmitter_init (FsNiceStreamTransmitter *self);
static void fs_nice_stream_transmitter_dispose (GObject *object);
static void fs_nice_stream_transmitter_finalize (GObject *object);

static void fs_nice_stream_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_nice_stream_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static gboolean fs_nice_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error);


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

static GType type = 0;

GType
fs_nice_stream_transmitter_get_type (void)
{
  return type;
}

GType
fs_nice_stream_transmitter_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsNiceStreamTransmitterClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_nice_stream_transmitter_class_init,
    NULL,
    NULL,
    sizeof (FsNiceStreamTransmitter),
    0,
    (GInstanceInitFunc) fs_nice_stream_transmitter_init
  };

  type = g_type_module_register_type (G_TYPE_MODULE (module),
    FS_TYPE_STREAM_TRANSMITTER, "FsNiceStreamTransmitter", &info, 0);

  return type;
}

static void
fs_nice_stream_transmitter_class_init (FsNiceStreamTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsStreamTransmitterClass *streamtransmitterclass =
    FS_STREAM_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_nice_stream_transmitter_set_property;
  gobject_class->get_property = fs_nice_stream_transmitter_get_property;
  gobject_class->dispose = fs_nice_stream_transmitter_dispose;
  gobject_class->finalize = fs_nice_stream_transmitter_finalize;

  streamtransmitterclass->add_remote_candidate =
    fs_nice_stream_transmitter_add_remote_candidate;

  g_type_class_add_private (klass, sizeof (FsNiceStreamTransmitterPrivate));

  g_object_class_override_property (gobject_class, PROP_SENDING, "sending");
  g_object_class_override_property (gobject_class,
      PROP_PREFERRED_LOCAL_CANDIDATES, "preferred-local-candidates");

  g_object_class_install_property (gobject_class, PROP_STUN_IP,
      g_param_spec_string (
          "stun-ip",
          "STUN server",
          "The STUN server used to obtain server-reflexive candidates",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (gobject_class, PROP_STUN_PORT,
      g_param_spec_uint (
          "stun-port",
          "STUN server port",
          "The STUN server used to obtain server-reflexive candidates",
          1, 65536,
          3478,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TURN_IP,
      g_param_spec_string (
          "turn-ip",
          "TURN server",
          "The TURN server used to obtain relay candidates",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TURN_PORT,
      g_param_spec_uint (
          "turn-port",
          "TURN server port",
          "The TURN server used to obtain relay candidates",
          1, 65536,
          3478,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONTROLLING_MODE,
      g_param_spec_boolean (
          "controlling-mode",
          "ICE controlling mode",
          "Whether the agent is in controlling mode",
          TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
fs_nice_stream_transmitter_init (FsNiceStreamTransmitter *self)
{
  /* member init */
  self->priv = FS_NICE_STREAM_TRANSMITTER_GET_PRIVATE (self);

  self->priv->sending = TRUE;
}

static void
fs_nice_stream_transmitter_dispose (GObject *object)
{
  //FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);


  parent_class->dispose (object);
}

static void
fs_nice_stream_transmitter_finalize (GObject *object)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);

  g_free (self->priv->stun_ip);
  g_free (self->priv->turn_ip);

  parent_class->finalize (object);
}

static void
fs_nice_stream_transmitter_get_property (GObject *object,
                                           guint prop_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_SENDING:
      g_value_set_boolean (value, self->priv->sending);
      break;
    case PROP_STUN_IP:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_string (value, self->priv->stun_ip);
      break;
    case PROP_STUN_PORT:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_uint (value, self->priv->stun_port);
      break;
    case PROP_TURN_IP:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_string (value, self->priv->turn_ip);
      break;
    case PROP_TURN_PORT:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_uint (value, self->priv->turn_port);

      break;
    case PROP_CONTROLLING_MODE:
      if (self->priv->transmitter->agent)
        g_object_get_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      else
        g_value_set_boolean (value, self->priv->controlling_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_nice_stream_transmitter_set_property (GObject *object,
                                           guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec)
{
  FsNiceStreamTransmitter *self = FS_NICE_STREAM_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_SENDING:
      self->priv->sending = g_value_get_boolean (value);
      break;
    case PROP_STUN_IP:
      self->priv->stun_ip = g_value_dup_string (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    case PROP_STUN_PORT:
      self->priv->stun_port = g_value_get_uint (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    case PROP_TURN_IP:
      self->priv->turn_ip = g_value_dup_string (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    case PROP_TURN_PORT:
      self->priv->turn_port = g_value_get_uint (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    case PROP_CONTROLLING_MODE:
      self->priv->controlling_mode = g_value_get_boolean (value);
      if (self->priv->transmitter->agent)
        g_object_set_property (G_OBJECT (self->priv->transmitter->agent),
            g_param_spec_get_name (pspec), value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * fs_nice_stream_transmitter_add_remote_candidate
 * @streamtransmitter: a #FsStreamTransmitter
 * @candidate: a remote #FsCandidate to add
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function is used to add remote candidates to the transmitter
 *
 * Returns: TRUE of the candidate could be added, FALSE if it couldnt
 *   (and the #GError will be set)
 */

static gboolean
fs_nice_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error)
{
  return FALSE;
}


FsNiceStreamTransmitter *
fs_nice_stream_transmitter_newv (FsNiceTransmitter *transmitter,
    guint stream_id,
    guint n_parameters,
    GParameter *parameters,
    GError **error)
{
  FsNiceStreamTransmitter *streamtransmitter = NULL;

  streamtransmitter = g_object_newv (FS_TYPE_NICE_STREAM_TRANSMITTER,
    n_parameters, parameters);

  if (!streamtransmitter)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not build the stream transmitter");
    return NULL;
  }

  streamtransmitter->priv->transmitter = transmitter;
  streamtransmitter->priv->stream_id = stream_id;

  return streamtransmitter;
}

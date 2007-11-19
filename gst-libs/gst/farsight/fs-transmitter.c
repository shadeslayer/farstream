/*
 * Farsight2 - Farsight Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-transmitter.c - A Farsight Transmitter gobject (base implementation)
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
 * SECTION:fs-transmitter
 * @short_description: A transmitter object linked to a session
 *
 * This object is the base implementation of a Farsight Transmitter.
 * It needs to be derived and implement by a Farsight transmitter. A
 * Farsight Transmitter provides a GStreamer network sink and source to be used
 * for the Farsight Session. It creates #FsStreamTransmitter objects which are
 * used to set the different per-stream properties
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-marshal.h"
#include "fs-transmitter.h"

#include "fs-plugin.h"

#include "fs-conference-iface.h"

#include <gst/gst.h>

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
  PROP_GST_SINK,
  PROP_GST_SRC,
  PROP_COMPONENTS
};

struct _FsTransmitterPrivate
{
  gboolean disposed;
};

#define FS_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_TRANSMITTER, FsTransmitterPrivate))

static void fs_transmitter_class_init (FsTransmitterClass *klass);
static void fs_transmitter_init (FsTransmitter *self);
static void fs_transmitter_dispose (GObject *object);
static void fs_transmitter_finalize (GObject *object);

static void fs_transmitter_get_property (GObject *object,
                                         guint prop_id,
                                         GValue *value,
                                         GParamSpec *pspec);
static void fs_transmitter_set_property (GObject *object,
                                         guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_transmitter_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsTransmitterClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_transmitter_class_init,
      NULL,
      NULL,
      sizeof (FsTransmitter),
      0,
      (GInstanceInitFunc) fs_transmitter_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsTransmitter", &info, G_TYPE_FLAG_ABSTRACT);
  }

  return type;
}

static void
fs_transmitter_class_init (FsTransmitterClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_transmitter_set_property;
  gobject_class->get_property = fs_transmitter_get_property;



  /**
   * FsTransmitter:gst-src:
   *
   * A network source #GstElement to be used by the #FsSession
   * This element MUST provide a source pad named "src%d" per component.
   * These pads number must start at 1 (the %d corresponds to the component
   * number).
   * These pads MUST be static pads.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_GST_SRC,
      g_param_spec_object ("gst-src",
        "The network source",
        "A source GstElement to be used by a FsSession",
        GST_TYPE_ELEMENT,
        G_PARAM_READABLE));

  /**
   * FsTransmitter:gst-sink:
   *
   * A network source #GstElement to be used by the #FsSession
   * These element's sink must have async=FALSE
   * This element MUST provide a pad named "sink\%d" per component.
   * These pads number must start at 1 (the \%d corresponds to the component
   * number).
   * These pads MUST be static pads.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_GST_SINK,
      g_param_spec_object ("gst-sink",
        "The network source",
        "A source GstElement to be used by a FsSession",
        GST_TYPE_ELEMENT,
        G_PARAM_READABLE));

  /**
   * FsTransmitter:components:
   *
   * The number of components to create
   */
  g_object_class_install_property (gobject_class,
      PROP_COMPONENTS,
      g_param_spec_uint ("components",
        "Number of componnets",
        "The number of components to create",
        1, 255, 1,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  /**
   * FsTransmitter::error:
   * @self: #FsTransmitter that emitted the signal
   * @errorno: The number of the error
   * @error_msg: Error message to be displayed to user
   * @debug_msg: Debugging error message
   *
   * This signal is emitted in any error condition
   *
   */
  signals[ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      fs_marshal_VOID__OBJECT_INT_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);


  gobject_class->dispose = fs_transmitter_dispose;
  gobject_class->finalize = fs_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsTransmitterPrivate));
}

static void
fs_transmitter_init (FsTransmitter *self)
{
  /* member init */
  self->priv = FS_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_transmitter_dispose (GObject *object)
{
  FsTransmitter *self = FS_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_transmitter_finalize (GObject *object)
{
  parent_class->finalize (object);
}

static void
fs_transmitter_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
}

static void
fs_transmitter_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
}


/**
 * fs_transmitter_new_stream_transmitter:
 * @transmitter: a #FsTranmitter
 * @participant: the #FsParticipant for which the #FsStream using this
 * new #FsStreamTransmitter is created
 * @n_parameters: The number of parameters to pass to the newly created
 * #FsStreamTransmitter
 * @parameters: an array of #GParameter
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will create a new #FsStreamTransmitter element for a
 * specific participant for this #FsTransmitter
 *
 * Returns: a new #FsStreamTransmitter, or NULL if there is an error
 */

FsStreamTransmitter *
fs_transmitter_new_stream_transmitter (FsTransmitter *transmitter,
                                       FsParticipant *participant,
                                       guint n_parameters,
                                       GParameter *parameters,
                                       GError **error)
{
  FsTransmitterClass *klass = FS_TRANSMITTER_GET_CLASS (transmitter);

  if (klass->new_stream_transmitter) {
    return klass->new_stream_transmitter (transmitter, participant,
      n_parameters, parameters, error);
  } else {
    g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
      "new_stream_transmitter not defined in class");
  }

  return NULL;
}

/**
 * fs_transmitter_new:
 * @type: The type of transmitter to create
 * @components: The number of components to create
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function creates a new transmitter of the requested type.
 * It will load the appropriate plugin as required.
 *
 * Returns: a newly-created #FsTransmitter of the requested type
 *    (or NULL if there is an error)
 */

FsTransmitter *
fs_transmitter_new (gchar *type, guint components, GError **error)
{
  FsTransmitter *self = NULL;

  g_return_val_if_fail (type != NULL, NULL);

  self = FS_TRANSMITTER(fs_plugin_create(type, "transmitter", error,
      "components", components, NULL));

  if (!self)
    return NULL;

  if (self->construction_error) {
    *error = self->construction_error;
    g_object_unref (self);
    self = NULL;
  }

  return self;
}

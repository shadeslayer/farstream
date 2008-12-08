/*
 * Farsight2 - Farsight Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-transmitter.c - A Farsight Transmitter gobject (base implementation)
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
#include "fs-private.h"

#include <gst/gst.h>

/* Signals */
enum
{
  ERROR_SIGNAL,
  GET_RECVONLY_FILTER_SIGNAL,
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

G_DEFINE_ABSTRACT_TYPE(FsTransmitter, fs_transmitter, G_TYPE_OBJECT);

#define FS_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_TRANSMITTER, FsTransmitterPrivate))

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


static void
fs_transmitter_class_init (FsTransmitterClass *klass)
{
  GObjectClass *gobject_class;

  fs_base_conference_init_debug ();

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
  signals[ERROR_SIGNAL] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_marshal_VOID__ENUM_STRING_STRING,
      G_TYPE_NONE, 3, FS_TYPE_ERROR, G_TYPE_STRING, G_TYPE_STRING);

  /**
   * FsTransmitter::get-recvonly-filter
   * @self: #FsTransmitter that emitted the signal
   * @component: The component that the filter will be used for
   *
   * This signal is emitted when the transmitter wants to get a filter for
   * to use if sending is disabled. If you want to drop all buffers, just
   * don't listen to the signal.
   *
   * This element should have a "sending" property that can be changed with the
   * sending state of the stream. It should default to %TRUE.
   *
   * Returns: the #GstElement to use as the filter, or %NULL to drop everything
   */

  signals[GET_RECVONLY_FILTER_SIGNAL] = g_signal_new ("get-recvonly-filter",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_marshal_OBJECT__UINT,
      GST_TYPE_ELEMENT, 1, G_TYPE_UINT);


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
  GST_WARNING ("Subclass %s of FsTransmitter does not override the %s property"
      " getter",
      G_OBJECT_TYPE_NAME(object),
      g_param_spec_get_name (pspec));
}

static void
fs_transmitter_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  GST_WARNING ("Subclass %s of FsTransmitter does not override the %s property"
      " setter",
      G_OBJECT_TYPE_NAME(object),
      g_param_spec_get_name (pspec));
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

  g_return_val_if_fail (transmitter, NULL);
  g_return_val_if_fail (klass, NULL);
  g_return_val_if_fail (klass->new_stream_transmitter, NULL);

  return klass->new_stream_transmitter (transmitter, participant,
      n_parameters, parameters, error);

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
fs_transmitter_new (const gchar *type, guint components, GError **error)
{
  FsTransmitter *self = NULL;

  g_return_val_if_fail (type != NULL, NULL);

  self = FS_TRANSMITTER (fs_plugin_create (type, "transmitter", error,
      "components", components, NULL));

  if (!self)
    return NULL;

  if (self->construction_error) {
    g_propagate_error(error, self->construction_error);
    g_object_unref (self);
    self = NULL;
  }

  return self;
}

/**
 * fs_transmitter_get_stream_transmitter_type:
 * @transmitter: A #FsTransmitter object
 *
 * This function returns the GObject type for the stream transmitter.
 * This is meant for bindings that need to introspect the type of arguments
 * that can be passed to the _new_stream_transmitter.
 *
 * Returns: the #GType
 */

GType
fs_transmitter_get_stream_transmitter_type (FsTransmitter *transmitter)
{
  FsTransmitterClass *klass = FS_TRANSMITTER_GET_CLASS (transmitter);

  g_return_val_if_fail (klass, 0);
  g_return_val_if_fail (klass->get_stream_transmitter_type, 0);

  return klass->get_stream_transmitter_type (transmitter);
}


/**
 * fs_transmitter_emit_error:
 * @transmitter: #FsTransmitter on which to emit the error signal
 * @error_no: The number of the error
 * @error_msg: Error message to be displayed to user
 * @debug_msg: Debugging error message
 *
 * This function emit the "error" signal on a #FsTransmitter, it should
 * only be called by subclasses.
 */
void
fs_transmitter_emit_error (FsTransmitter *transmitter,
    gint error_no,
    const gchar *error_msg,
    const gchar *debug_msg)
{
  g_signal_emit (transmitter, signals[ERROR_SIGNAL], 0, error_no,
      error_msg, debug_msg);
}

/**
 * fs_transmitter_list_available:
 *
 * Get the list of all available transmitters
 *
 * Returns: a newly allocated array of strings containing the list of all
 *  available transmitters or %NULL if there are none. It should
 *  be freed with g_strfreev().
 */

char **
fs_transmitter_list_available (void)
{
  return fs_plugin_list_available ("transmitter");
}

/**
 * fs_transmitter_get_recvonly_filter:
 * @transmitter: A #FsTransmitter object
 * @component: The component to get the filter for
 *
 * Get the filter to add on the send pipeline if sending is disabled.
 *
 * Only for use by subclasses.
 *
 * Returns: a #GstElement to use as the filter or %NULL
 */

GstElement *
fs_transmitter_get_recvonly_filter (FsTransmitter *transmitter,
    guint component)
{
  GstElement *element = NULL;

  g_signal_emit (transmitter, signals[GET_RECVONLY_FILTER_SIGNAL], 0, component,
      &element);

  return element;
}

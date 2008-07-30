/*
 * Farsight2 - Farsight libnice Transmitter
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-nice-transmitter.c - A Farsight libnice transmitter
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
 * SECTION:fs-nice-transmitter
 * @short_description: A transmitter for ICE using libnice
 *
 * The transmitter provides ICE (Interactive Connection Establishment) using
 * libnice.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-nice-transmitter.h"
#include "fs-nice-stream-transmitter.h"
#include "fs-nice-agent.h"

#include <gst/farsight/fs-conference-iface.h>
#include <gst/farsight/fs-plugin.h>

#include <agent.h>

#include <string.h>
#include <sys/types.h>

GST_DEBUG_CATEGORY (fs_nice_transmitter_debug);
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
  PROP_GST_SINK,
  PROP_GST_SRC,
  PROP_COMPONENTS
};

struct _FsNiceTransmitterPrivate
{
  /* We hold references to this element */
  GstElement *gst_sink;
  GstElement *gst_src;

  /* We don't hold a reference to these elements, they are owned
     by the bins */
  /* They are tables of pointers, one per component */
  GstElement **src_funnels;
  GstElement **sink_tees;
};

#define FS_NICE_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_NICE_TRANSMITTER, \
    FsNiceTransmitterPrivate))


static void fs_nice_transmitter_class_init (
    FsNiceTransmitterClass *klass);
static void fs_nice_transmitter_init (FsNiceTransmitter *self);
static void fs_nice_transmitter_constructed (GObject *object);
static void fs_nice_transmitter_dispose (GObject *object);
static void fs_nice_transmitter_finalize (GObject *object);

static void fs_nice_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_nice_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static FsStreamTransmitter *fs_nice_transmitter_new_stream_transmitter (
    FsTransmitter *transmitter, FsParticipant *participant,
    guint n_parameters, GParameter *parameters, GError **error);
static GType fs_nice_transmitter_get_stream_transmitter_type (
    FsTransmitter *transmitter,
    GError **error);

static GObjectClass *parent_class = NULL;
//static guint signals[LAST_SIGNAL] = { 0 };


/*
 * Lets register the plugin
 */

static GType type = 0;

GType
fs_nice_transmitter_get_type (void)
{
  g_assert (type);
  return type;
}

static GType
fs_nice_transmitter_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsNiceTransmitterClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_nice_transmitter_class_init,
    NULL,
    NULL,
    sizeof (FsNiceTransmitter),
    0,
    (GInstanceInitFunc) fs_nice_transmitter_init
  };

  GST_DEBUG_CATEGORY_INIT (fs_nice_transmitter_debug,
      "fsnicetransmitter", 0,
      "Farsight libnice transmitter");

  fs_nice_stream_transmitter_register_type (module);
  fs_nice_agent_register_type (module);

  type = g_type_module_register_type (G_TYPE_MODULE (module),
    FS_TYPE_TRANSMITTER, "FsNiceTransmitter", &info, 0);

  return type;
}

FS_INIT_PLUGIN (fs_nice_transmitter_register_type)

static void
fs_nice_transmitter_class_init (FsNiceTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsTransmitterClass *transmitter_class = FS_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_nice_transmitter_set_property;
  gobject_class->get_property = fs_nice_transmitter_get_property;

  gobject_class->constructed = fs_nice_transmitter_constructed;

  g_object_class_override_property (gobject_class, PROP_GST_SRC, "gst-src");
  g_object_class_override_property (gobject_class, PROP_GST_SINK, "gst-sink");
  g_object_class_override_property (gobject_class, PROP_COMPONENTS,
    "components");

  transmitter_class->new_stream_transmitter =
    fs_nice_transmitter_new_stream_transmitter;
  transmitter_class->get_stream_transmitter_type =
    fs_nice_transmitter_get_stream_transmitter_type;

  gobject_class->dispose = fs_nice_transmitter_dispose;
  gobject_class->finalize = fs_nice_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsNiceTransmitterPrivate));
}

static void
fs_nice_transmitter_init (FsNiceTransmitter *self)
{

  /* member init */
  self->priv = FS_NICE_TRANSMITTER_GET_PRIVATE (self);

  self->components = 2;
}

static void
fs_nice_transmitter_constructed (GObject *object)
{
  FsNiceTransmitter *self = FS_NICE_TRANSMITTER_CAST (object);
  FsTransmitter *trans = FS_TRANSMITTER_CAST (self);
  GstPad *pad = NULL, *pad2 = NULL;
  GstPad *ghostpad = NULL;
  gchar *padname;
  GstPadLinkReturn ret;
  int c; /* component_id */


  /* We waste one space in order to have the index be the component_id */
  self->priv->src_funnels = g_new0 (GstElement *, self->components+1);
  self->priv->sink_tees = g_new0 (GstElement *, self->components+1);

  /* First we need the src elemnet */

  self->priv->gst_src = gst_bin_new (NULL);

  if (!self->priv->gst_src)
  {
    trans->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not build the transmitter src bin");
    return;
  }

  gst_object_ref (self->priv->gst_src);


  /* Second, we do the sink element */

  self->priv->gst_sink = gst_bin_new (NULL);

  if (!self->priv->gst_sink)
  {
    trans->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not build the transmitter sink bin");
    return;
  }

  g_object_set (G_OBJECT (self->priv->gst_sink),
      "async-handling", TRUE,
      NULL);

  gst_object_ref (self->priv->gst_sink);

  for (c = 1; c <= self->components; c++)
  {
    GstElement *fakesink = NULL;

    /* Lets create the RTP source funnel */

    self->priv->src_funnels[c] = gst_element_factory_make ("fsfunnel", NULL);

    if (!self->priv->src_funnels[c])
    {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make the fsfunnel element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_src),
        self->priv->src_funnels[c]))
    {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not add the fsfunnel element to the transmitter src bin");
    }

    pad = gst_element_get_static_pad (self->priv->src_funnels[c], "src");
    padname = g_strdup_printf ("src%d", c);
    ghostpad = gst_ghost_pad_new (padname, pad);
    g_free (padname);
    gst_object_unref (pad);

    gst_pad_set_active (ghostpad, TRUE);
    gst_element_add_pad (self->priv->gst_src, ghostpad);


    /* Lets create the RTP sink tee */

    self->priv->sink_tees[c] = gst_element_factory_make ("tee", NULL);

    if (!self->priv->sink_tees[c])
    {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make the tee element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_sink),
        self->priv->sink_tees[c]))
    {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not add the tee element to the transmitter sink bin");
    }

    pad = gst_element_get_static_pad (self->priv->sink_tees[c], "sink");
    padname = g_strdup_printf ("sink%d", c);
    ghostpad = gst_ghost_pad_new (padname, pad);
    g_free (padname);
    gst_object_unref (pad);

    gst_pad_set_active (ghostpad, TRUE);
    gst_element_add_pad (self->priv->gst_sink, ghostpad);

    fakesink = gst_element_factory_make ("fakesink", NULL);

    if (!fakesink)
    {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make the fakesink element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_sink), fakesink))
    {
      gst_object_unref (fakesink);
      trans->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the fakesink element to the transmitter sink bin");
      return;
    }

    g_object_set (fakesink,
        "async", FALSE,
        "sync" , FALSE,
        NULL);

    pad = gst_element_get_request_pad (self->priv->sink_tees[c], "src%d");
    pad2 = gst_element_get_static_pad (fakesink, "sink");

    ret = gst_pad_link (pad, pad2);

    gst_object_unref (pad2);
    gst_object_unref (pad);

    if (GST_PAD_LINK_FAILED(ret))
    {
      trans->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not link the tee to the fakesink");
      return;
    }
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
}

static void
fs_nice_transmitter_dispose (GObject *object)
{
  FsNiceTransmitter *self = FS_NICE_TRANSMITTER (object);

  if (self->priv->gst_src)
  {
    gst_object_unref (self->priv->gst_src);
    self->priv->gst_src = NULL;
  }

  if (self->priv->gst_sink)
  {
    gst_object_unref (self->priv->gst_sink);
    self->priv->gst_sink = NULL;
  }

  parent_class->dispose (object);
}

static void
fs_nice_transmitter_finalize (GObject *object)
{
  FsNiceTransmitter *self = FS_NICE_TRANSMITTER (object);

  if (self->priv->src_funnels)
  {
    g_free (self->priv->src_funnels);
    self->priv->src_funnels = NULL;
  }

  if (self->priv->sink_tees)
  {
    g_free (self->priv->sink_tees);
    self->priv->sink_tees = NULL;
  }

  parent_class->finalize (object);
}

static void
fs_nice_transmitter_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsNiceTransmitter *self = FS_NICE_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_GST_SINK:
      g_value_set_object (value, self->priv->gst_sink);
      break;
    case PROP_GST_SRC:
      g_value_set_object (value, self->priv->gst_src);
      break;
    case PROP_COMPONENTS:
      g_value_set_uint (value, self->components);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_nice_transmitter_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  FsNiceTransmitter *self = FS_NICE_TRANSMITTER (object);

  switch (prop_id)
  {
    case PROP_COMPONENTS:
      self->components = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * fs_nice_transmitter_new_stream_nice_transmitter:
 * @transmitter: a #FsTranmitter
 * @participant: the #FsParticipant for which the #FsStream using this
 * new #FsStreamTransmitter is created
 *
 * This function will create a new #FsStreamTransmitter element for a
 * specific participant for this #FsNiceTransmitter
 *
 * Returns: a new #FsStreamTransmitter
 */

static FsStreamTransmitter *
fs_nice_transmitter_new_stream_transmitter (FsTransmitter *transmitter,
  FsParticipant *participant, guint n_parameters, GParameter *parameters,
  GError **error)
{
  FsNiceTransmitter *self = FS_NICE_TRANSMITTER (transmitter);

  return FS_STREAM_TRANSMITTER (fs_nice_stream_transmitter_newv (
          self, participant, n_parameters, parameters, error));
}

static GType
fs_nice_transmitter_get_stream_transmitter_type (
    FsTransmitter *transmitter,
    GError **error)
{
  return FS_TYPE_NICE_STREAM_TRANSMITTER;
}



static GstElement *
_create_sinksource (
    gchar *elementname,
    GstBin *bin,
    GstElement *teefunnel,
    NiceAgent *agent,
    guint stream_id,
    guint component_id,
    GstPadDirection direction,
    GCallback have_buffer_callback,
    gpointer have_buffer_user_data,
    gulong *buffer_probe_id,
    GstPad **requested_pad,
    GError **error)
{
  GstElement *elem;
  GstPadLinkReturn ret;
  GstPad *elempad = NULL;
  GstStateChangeReturn state_ret;

  g_assert (direction == GST_PAD_SINK || direction == GST_PAD_SRC);

  elem = gst_element_factory_make (elementname, NULL);
  if (!elem)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create the %s element", elementname);
    return NULL;
  }

  g_object_set (elem,
      "agent", agent,
      "stream", stream_id,
      "component", component_id,
      NULL);


  if (direction == GST_PAD_SINK)
    g_object_set (elem,
        "async", FALSE,
        "sync", FALSE,
        NULL);

  if (!gst_bin_add (bin, elem))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the %s element to the gst %s bin", elementname,
        (direction == GST_PAD_SINK) ? "sink" : "src");
    gst_object_unref (elem);
    return NULL;
  }

  if (direction == GST_PAD_SINK)
    *requested_pad = gst_element_get_request_pad (teefunnel, "src%d");
  else
    *requested_pad = gst_element_get_request_pad (teefunnel, "sink%d");

  if (!*requested_pad)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not get the %s request pad from the %s",
        (direction == GST_PAD_SINK) ? "src" : "sink",
        (direction == GST_PAD_SINK) ? "tee" : "funnel");
    goto error;
  }

  if (direction == GST_PAD_SINK)
    elempad = gst_element_get_static_pad (elem, "sink");
  else
    elempad = gst_element_get_static_pad (elem, "src");

  if (direction == GST_PAD_SINK)
    ret = gst_pad_link (*requested_pad, elempad);
  else
    ret = gst_pad_link (elempad, *requested_pad);

  if (have_buffer_callback && buffer_probe_id)
  {
    if (direction == GST_PAD_SINK)
      *buffer_probe_id = gst_pad_add_buffer_probe (*requested_pad,
          have_buffer_callback,
          have_buffer_user_data);
    else
      *buffer_probe_id = gst_pad_add_buffer_probe (elempad,
          have_buffer_callback,
          have_buffer_user_data);

    if (*buffer_probe_id == 0)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not create buffer probe as requested");
    }
  }

  gst_object_unref (elempad);

  if (GST_PAD_LINK_FAILED(ret))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the new element %s (%d)", elementname, ret);
    goto error;
  }

  if (!gst_element_sync_state_with_parent (elem))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not sync the state of the new %s with its parent",
        elementname);
    goto error;
  }

  return elem;

 error:

  gst_element_set_locked_state (elem, TRUE);
  state_ret = gst_element_set_state (elem, GST_STATE_NULL);
  if (state_ret != GST_STATE_CHANGE_SUCCESS)
    GST_ERROR ("On error, could not reset %s to state NULL (%s)", elementname,
        gst_element_state_change_return_get_name (state_ret));
  if (!gst_bin_remove (bin, elem))
    GST_ERROR ("Could not remove element %s from bin on error", elementname);

  if (elempad)
    gst_object_unref (elempad);

  return NULL;
}

struct _NiceGstStream {
  GstElement **nicesrcs;
  GstElement **nicesinks;

  GstPad **requested_funnel_pads;
  GstPad **requested_tee_pads;

  gulong *probe_ids;
};

NiceGstStream *
fs_nice_transmitter_add_gst_stream (FsNiceTransmitter *self,
    NiceAgent *agent,
    guint stream_id,
    GCallback have_buffer_callback,
    gpointer have_buffer_user_data,
    GError **error)
{
  guint c;
  NiceGstStream *ns = NULL;

  ns = g_new0 (NiceGstStream, 1);
  ns->nicesrcs = g_new0 (GstElement *, self->components + 1);
  ns->nicesinks = g_new0 (GstElement *, self->components + 1);
  ns->requested_tee_pads = g_new0 (GstPad *, self->components + 1);
  ns->requested_funnel_pads = g_new0 (GstPad *, self->components + 1);
  ns->probe_ids = g_new0 (gulong, self->components + 1);

  for (c = 1; c <= self->components; c++)
  {
    ns->nicesrcs[c] = _create_sinksource ("nicesrc",
        GST_BIN (self->priv->gst_src),
        self->priv->src_funnels[c],
        agent,
        stream_id,
        c,
        GST_PAD_SRC,
        have_buffer_callback,
        have_buffer_user_data,
        &ns->probe_ids[c],
        &ns->requested_funnel_pads[c],
        error);

    if (ns->nicesrcs[c] == NULL)
      goto error;


    ns->nicesinks[c] = _create_sinksource ("nicesink",
        GST_BIN (self->priv->gst_sink),
        self->priv->sink_tees[c],
        agent,
        stream_id,
        c,
        GST_PAD_SINK,
        NULL, NULL, NULL,
        &ns->requested_tee_pads[c],
        error);

    if (ns->nicesinks[c] == NULL)
      goto error;
  }

  return ns;

 error:
  fs_nice_transmitter_free_gst_stream (self, ns);
  return NULL;
}

void
fs_nice_transmitter_free_gst_stream (FsNiceTransmitter *self,
    NiceGstStream *ns)
{
  guint c;

  for (c = 1; c <= self->components; c++)
  {
    if (ns->nicesrcs[c])
    {
      GstStateChangeReturn ret;
      gst_element_set_locked_state (ns->nicesrcs[c], TRUE);
      ret = gst_element_set_state (ns->nicesrcs[c], GST_STATE_NULL);
      if (ret != GST_STATE_CHANGE_SUCCESS)
        GST_ERROR ("Error changing state of nicesrc: %s",
            gst_element_state_change_return_get_name (ret));
      if (!gst_bin_remove (GST_BIN (self->priv->gst_src), ns->nicesrcs[c]))
        GST_ERROR ("Could not remove nicesrc element from transmitter source");
    }

    if (ns->requested_funnel_pads[c])
    {
      gst_element_release_request_pad (self->priv->src_funnels[c],
          ns->requested_funnel_pads[c]);
      gst_object_unref (ns->requested_funnel_pads[c]);
    }

    if (ns->nicesinks[c])
    {
      GstStateChangeReturn ret;
      gst_element_set_locked_state (ns->nicesinks[c], TRUE);
      ret = gst_element_set_state (ns->nicesinks[c], GST_STATE_NULL);
      if (ret != GST_STATE_CHANGE_SUCCESS)
        GST_ERROR ("Error changing state of nicesink: %s",
            gst_element_state_change_return_get_name (ret));
      if (!gst_bin_remove (GST_BIN (self->priv->gst_sink), ns->nicesinks[c]))
        GST_ERROR ("Could not remove nicesink element from transmitter source");
    }

    if (ns->requested_tee_pads[c])
    {
      gst_element_release_request_pad (self->priv->sink_tees[c],
          ns->requested_tee_pads[c]);
      gst_object_unref (ns->requested_tee_pads[c]);
    }
  }

  g_free (ns->nicesrcs);
  g_free (ns->nicesinks);
  g_free (ns->requested_tee_pads);
  g_free (ns->requested_funnel_pads);
  g_free (ns);
}

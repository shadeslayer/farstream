/*
 * Farsight2 - Farsight RTP Special Codec
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-special-source.c - A Farsight RTP Special Source gobject
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/farsight/fs-base-conference.h>

#include "fs-rtp-conference.h"

#include "fs-rtp-special-source.h"

#include "fs-rtp-dtmf-event-source.h"
#include "fs-rtp-dtmf-sound-source.h"

#define GST_CAT_DEFAULT fsrtpconference_debug

/**
 * SECTION:fs-rtp-special-source
 * @short_description: Base class to abstract how special sources are handled
 *
 * This class defines how special sources can be handled, it is the base
 * for DMTF and CN sources.
 *
 */


/* props */
enum
{
  PROP_0,
  PROP_BIN,
  PROP_RTPMUXER
};

struct _FsRtpSpecialSourcePrivate {
  gboolean disposed;

  GstElement *outer_bin;
  GstElement *rtpmuxer;

  GstElement *src;

  GThread *stop_thread;

  /* Protects the content of this struct after object has been disposed of */
  GMutex *mutex;
};

static GObjectClass *parent_class = NULL;

static GList *classes = NULL;

G_DEFINE_ABSTRACT_TYPE(FsRtpSpecialSource, fs_rtp_special_source,
    G_TYPE_OBJECT);

#define FS_RTP_SPECIAL_SOURCE_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_SPECIAL_SOURCE,             \
   FsRtpSpecialSourcePrivate))


#define FS_RTP_SPECIAL_SOURCE_LOCK(src)   g_mutex_lock (src->priv->mutex)
#define FS_RTP_SPECIAL_SOURCE_UNLOCK(src) g_mutex_unlock (src->priv->mutex)

static void fs_rtp_special_source_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);
static void fs_rtp_special_source_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);

static void fs_rtp_special_source_dispose (GObject *object);
static void fs_rtp_special_source_finalize (GObject *object);

static FsRtpSpecialSource *
fs_rtp_special_source_new (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    GError **error);
static gboolean
fs_rtp_special_source_update (FsRtpSpecialSource *source,
    GList *negotiated_codecs,
    FsCodec *selected_codec);

static gpointer
register_classes (gpointer data)
{
  GList *my_classes = NULL;

  my_classes = g_list_prepend (my_classes,
      g_type_class_ref (FS_TYPE_RTP_DTMF_EVENT_SOURCE));
  my_classes = g_list_prepend (my_classes,
      g_type_class_ref (FS_TYPE_RTP_DTMF_SOUND_SOURCE));

  return my_classes;
}

static void
fs_rtp_special_sources_init (void)
{
  static GOnce my_once = G_ONCE_INIT;

  classes = g_once (&my_once, register_classes, NULL);
}

static void
fs_rtp_special_source_class_init (FsRtpSpecialSourceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = fs_rtp_special_source_parent_class;

  gobject_class->set_property = fs_rtp_special_source_set_property;
  gobject_class->get_property = fs_rtp_special_source_get_property;
  gobject_class->dispose = fs_rtp_special_source_dispose;
  gobject_class->finalize = fs_rtp_special_source_finalize;

  g_object_class_install_property (gobject_class,
      PROP_BIN,
      g_param_spec_object ("bin",
          "The GstBin to add the elements to",
          "This is the GstBin where this class adds elements",
          GST_TYPE_BIN,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_RTPMUXER,
      g_param_spec_object ("rtpmuxer",
          "The RTP muxer that the source is linked to",
          "The RTP muxer that the source is linked to",
          GST_TYPE_ELEMENT,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));



  g_type_class_add_private (klass, sizeof (FsRtpSpecialSourcePrivate));
}

static void
fs_rtp_special_source_init (FsRtpSpecialSource *self)
{
  self->priv = FS_RTP_SPECIAL_SOURCE_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->priv->mutex = g_mutex_new ();
}

/**
 * stop_source_thread:
 * @data: a pointer to the current #FsRtpSpecialSource
 *
 * This functioin will lock on the source's state change until its release
 * and only then let the source be disposed of
 */

static gpointer
stop_source_thread (gpointer data)
{
  FsRtpSpecialSource *self = FS_RTP_SPECIAL_SOURCE (data);

  gst_element_set_locked_state (self->priv->src, TRUE);
  gst_element_set_state (self->priv->src, GST_STATE_NULL);

  FS_RTP_SPECIAL_SOURCE_LOCK (self);
  gst_bin_remove (GST_BIN (self->priv->outer_bin), self->priv->src);
  self->priv->src = NULL;
  FS_RTP_SPECIAL_SOURCE_UNLOCK (self);

  g_object_unref (self);

  return NULL;
}

static void
fs_rtp_special_source_dispose (GObject *object)
{
  FsRtpSpecialSource *self = FS_RTP_SPECIAL_SOURCE (object);

  if (self->priv->disposed)
    return;


  FS_RTP_SPECIAL_SOURCE_LOCK (self);

  if (self->priv->disposed)
  {
    FS_RTP_SPECIAL_SOURCE_UNLOCK (self);
    return;
  }

  if (self->priv->src)
  {
    GError *error = NULL;

    if (self->priv->stop_thread)
    {
      GST_DEBUG ("stopping thread for special source already running");
      return;
    }

    g_object_ref (self);
    self->priv->stop_thread = g_thread_create (stop_source_thread, self, FALSE,
        &error);

    if (!self->priv->stop_thread)
    {
      GST_WARNING ("Could not start stopping thread for FsRtpSpecialSource:"
          " %s", error->message);
    }
    g_clear_error (&error);

    FS_RTP_SPECIAL_SOURCE_UNLOCK (self);
    return;
  }

  if (self->priv->rtpmuxer)
  {
    gst_object_unref (self->priv->rtpmuxer);
    self->priv->rtpmuxer = NULL;
  }

  if (self->priv->outer_bin)
  {
    gst_object_unref (self->priv->outer_bin);
    self->priv->outer_bin = NULL;
  }

  self->priv->disposed = TRUE;

  FS_RTP_SPECIAL_SOURCE_UNLOCK (self);

  G_OBJECT_CLASS (fs_rtp_special_source_parent_class)->dispose (object);
}


static void
fs_rtp_special_source_finalize (GObject *object)
{
  FsRtpSpecialSource *self = FS_RTP_SPECIAL_SOURCE (object);

  if (self->priv->mutex)
    g_mutex_free (self->priv->mutex);
  self->priv->mutex = NULL;

  G_OBJECT_CLASS (fs_rtp_special_source_parent_class)->finalize (object);
}


static void
fs_rtp_special_source_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  FsRtpSpecialSource *self = FS_RTP_SPECIAL_SOURCE (object);

  switch (prop_id)
  {
    case PROP_BIN:
      self->priv->outer_bin = g_value_dup_object (value);
      break;
    case PROP_RTPMUXER:
      self->priv->rtpmuxer = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rtp_special_source_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
 FsRtpSpecialSource *self = FS_RTP_SPECIAL_SOURCE (object);

  switch (prop_id)
  {
    case PROP_BIN:
      g_value_set_object (value, self->priv->outer_bin);
      break;
    case PROP_RTPMUXER:
      g_value_set_object (value, self->priv->rtpmuxer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GList*
fs_rtp_special_source_class_add_blueprint (FsRtpSpecialSourceClass *klass,
    GList *blueprints)
{
  if (klass->add_blueprint)
    return klass->add_blueprint (klass, blueprints);
  else
    GST_CAT_DEBUG (fsrtpconference_disco,
        "Class %s has no add_blueprint function", G_OBJECT_CLASS_NAME(klass));

  return blueprints;
}

static gboolean
fs_rtp_special_source_class_want_source (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec)
{
  if (klass->want_source)
    return klass->want_source (klass, negotiated_codecs, selected_codec);

  return FALSE;
}

/**
 * fs_rtp_special_sources_add_blueprints:
 * @blueprints: a #GList of #CodecBlueprint
 *
 * This function will add blueprints to the current list of blueprints based
 * on which elements are installed and on which codecs are already in the list
 * of blueprints.
 *
 * Returns: The updated #GList of #CodecBlueprint
 */

GList *
fs_rtp_special_sources_add_blueprints (GList *blueprints)
{
  GList *item = NULL;

  fs_rtp_special_sources_init ();

  for (item = g_list_first (classes);
       item;
       item = g_list_next (item))
  {
    FsRtpSpecialSourceClass *klass = item->data;

    blueprints = fs_rtp_special_source_class_add_blueprint (klass, blueprints);
  }

  return blueprints;
}

static gboolean
_source_order_compare_func (gconstpointer item1,gconstpointer item2)
{
  FsRtpSpecialSource *src1 = FS_RTP_SPECIAL_SOURCE_CAST (item1);
  FsRtpSpecialSource *src2 = FS_RTP_SPECIAL_SOURCE_CAST (item2);

  return src1->order - src2->order;
}

/**
 * fs_rtp_special_sources_remove:
 * @current_extra_sources: The #GList returned by previous calls to this function
 * @negotiated_codecs: A #GList of current negotiated #CodecAssociation
 * @send_codec: The currently selected send codec
 * @bin: The #GstBin to add the stuff to
 * @rtpmuxer: The rtpmux element
 * @error: NULL or the local of a #GError
 *
 * This function removes any special source that are not compatible with the
 * currently selected send codec.
 *
 * Returns: A #GList to be passed to other functions in this class
 */
GList *
fs_rtp_special_sources_remove (
    GList *current_extra_sources,
    GList *negotiated_codecs,
    FsCodec *send_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    GError **error)
{
  GList *klass_item = NULL;

  fs_rtp_special_sources_init ();

  for (klass_item = g_list_first (classes);
       klass_item;
       klass_item = g_list_next (klass_item))
  {
    FsRtpSpecialSourceClass *klass = klass_item->data;
    GList *obj_item;
    FsRtpSpecialSource *obj = NULL;

    /* Check if we already have an object for this type */
    for (obj_item = g_list_first (current_extra_sources);
         obj_item;
         obj_item = g_list_next (obj_item))
    {
      obj = obj_item->data;
      if (G_OBJECT_TYPE(obj) == G_OBJECT_CLASS_TYPE(klass))
        break;
    }

    if (obj_item)
    {
      if (!fs_rtp_special_source_class_want_source (klass, negotiated_codecs,
              send_codec) ||
          fs_rtp_special_source_update (obj, negotiated_codecs, send_codec))
      {
        current_extra_sources = g_list_remove (current_extra_sources, obj);
        g_object_unref (obj);
      }
    }
  }

  return current_extra_sources;
}


/**
 * fs_rtp_special_sources_remove:
 * @current_extra_sources: The #GList returned by previous calls to this function
 * @negotiated_codecs: A #GList of current negotiated #CodecAssociation
 * @send_codec: The currently selected send codec
 * @bin: The #GstBin to add the stuff to
 * @rtpmuxer: The rtpmux element
 * @error: NULL or the local of a #GError
 *
 * This function add special sources that don't already exist but are needed
 *
 * Returns: A #GList to be passed to other functions in this class
 */
GList *
fs_rtp_special_sources_create (
    GList *current_extra_sources,
    GList *negotiated_codecs,
    FsCodec *send_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    GError **error)
{
  GList *klass_item = NULL;

  fs_rtp_special_sources_init ();

  for (klass_item = g_list_first (classes);
       klass_item;
       klass_item = g_list_next (klass_item))
  {
    FsRtpSpecialSourceClass *klass = klass_item->data;
    GList *obj_item;
    FsRtpSpecialSource *obj = NULL;

    /* Check if we already have an object for this type */
    for (obj_item = g_list_first (current_extra_sources);
         obj_item;
         obj_item = g_list_next (obj_item))
    {
      obj = obj_item->data;
      if (G_OBJECT_TYPE(obj) == G_OBJECT_CLASS_TYPE(klass))
        break;
    }

    if (!obj_item &&
        fs_rtp_special_source_class_want_source (klass, negotiated_codecs,
            send_codec))
    {
      obj = fs_rtp_special_source_new (klass, negotiated_codecs, send_codec,
          bin, rtpmuxer, error);
      if (!obj)
        goto error;
      current_extra_sources = g_list_insert_sorted (current_extra_sources,
          obj, _source_order_compare_func);
    }
  }

 error:

  return current_extra_sources;
}

static FsRtpSpecialSource *
fs_rtp_special_source_new (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    GError **error)
{
  FsRtpSpecialSource *source = NULL;

  if (!klass->build)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_NOT_IMPLEMENTED,
        "Could not build new %s source", G_OBJECT_CLASS_NAME (klass));
    return NULL;
  }

  source = g_object_new (G_OBJECT_CLASS_TYPE (klass),
      "bin", bin,
      "rtpmuxer", rtpmuxer,
      NULL);
  g_assert (source);


  if (!source->priv->outer_bin)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid bin set");
    goto error;
  }

  if (!source->priv->rtpmuxer)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid rtpmuxer set");
    goto error;
  }

  source->priv->src = klass->build (source, negotiated_codecs, selected_codec,
      error);

  if (!source->priv->src)
    goto error;

  if (!gst_bin_add (GST_BIN (source->priv->outer_bin), source->priv->src))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add bin to outer bin");
    gst_object_unref (source->priv->src);
    source->priv->src = NULL;
    goto error;
  }

  if (!gst_element_link_pads (source->priv->src, "src",
          rtpmuxer, NULL))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link rtpdtmfsrc src to muxer sink");
    goto error_added;

  }

  if (!gst_element_sync_state_with_parent (source->priv->src))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not sync capsfilter state with its parent");
    goto error_added;
  }

  return source;

 error_added:
  gst_element_set_state (source->priv->src, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (source->priv->outer_bin), source->priv->src);
  source->priv->src = NULL;

 error:
  g_object_unref (source);

  return NULL;
}

static gboolean
fs_rtp_special_source_update (FsRtpSpecialSource *source,
    GList *negotiated_sources, FsCodec *selected_codec)
{
  FsRtpSpecialSourceClass *klass = FS_RTP_SPECIAL_SOURCE_GET_CLASS (source);

  if (klass->update)
    return klass->update (source, negotiated_sources, selected_codec);

  return FALSE;
}

/**
 * fs_rtp_special_source_send_event:
 * @self: a #FsRtpSpecialSource
 * @event: a upstream #GstEvent to send
 *
 * Sends an upstream event to the source.
 *
 * Returns: %TRUE if the event was delivered succesfully, %FALSE otherwise
 */

static gboolean
fs_rtp_special_source_send_event (FsRtpSpecialSource *self,
    GstEvent *event)
{
  gboolean ret = FALSE;
  GstPad *pad;

  pad = gst_element_get_pad (self->priv->src, "src");

  if (!pad)
  {
    GST_ERROR ("Could not find the source pad on the special source");
    gst_event_unref (event);
    return FALSE;
  }

  ret = gst_pad_send_event (pad, event);

  gst_object_unref (pad);

  return ret;
}

/**
 * fs_rtp_special_sources_send_event:
 * @current_extra_sources: The #GList of current #FsRtpSpecialSource
 * @event: an upstream #GstEvent
 *
 * This function will try to deliver the events in the specified order to the
 * special sources, it will stop once one source has accepted the event.
 *
 * Returns: %TRUE if a sources accepted the event, %FALSE otherwise
 */

static gboolean
fs_rtp_special_sources_send_event (GList *current_extra_sources,
    GstEvent *event)
{
  GList *item = NULL;

  if (!event)
  {
    GST_ERROR ("Could not make dtmf-event");
    return FALSE;
  }

  for (item = g_list_first (current_extra_sources);
       item;
       item = g_list_next (item))
  {
    FsRtpSpecialSource *source = item->data;
    gst_event_ref (event);
    if (fs_rtp_special_source_send_event (source, event))
    {
      gst_event_unref (event);
      return TRUE;
    }
  }
  gst_event_unref (event);
  return FALSE;
}


gboolean
fs_rtp_special_sources_start_telephony_event (GList *current_extra_sources,
      guint8 event,
      guint8 volume,
      FsDTMFMethod method)
{
  GstStructure *structure = NULL;
  gchar *method_str;

  structure = gst_structure_new ("dtmf-event",
      "number", G_TYPE_INT, event,
      "volume", G_TYPE_INT, volume,
      "start", G_TYPE_BOOLEAN, TRUE,
      "type", G_TYPE_INT, 1,
      NULL);

  if (!structure)
  {
    GST_ERROR ("Could not make dtmf-event structure");
    return FALSE;
  }

  switch (method)
  {
    case FS_DTMF_METHOD_AUTO:
      method_str = "default";
      break;
    case FS_DTMF_METHOD_RTP_RFC4733:
      method_str="RFC4733";
      gst_structure_set (structure, "method", G_TYPE_INT, 1, NULL);
      break;
    case FS_DTMF_METHOD_IN_BAND:
      method_str="sound";
      gst_structure_set (structure, "method", G_TYPE_INT, 2, NULL);
      break;
    default:
      method_str="other";
  }

  GST_DEBUG ("sending telephony event %d using method=%s",
      event, method_str);

  return fs_rtp_special_sources_send_event (current_extra_sources,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, structure));
}

gboolean
fs_rtp_special_sources_stop_telephony_event (GList *current_extra_sources,
    FsDTMFMethod method)
{
  GstStructure *structure = NULL;
  gchar *method_str;

  structure = gst_structure_new ("dtmf-event",
      "start", G_TYPE_BOOLEAN, FALSE,
      "type", G_TYPE_INT, 1,
      NULL);

  switch (method)
  {
    case FS_DTMF_METHOD_AUTO:
      method_str = "default";
      break;
    case FS_DTMF_METHOD_RTP_RFC4733:
      method_str="RFC4733";
      gst_structure_set (structure, "method", G_TYPE_INT, 1, NULL);
      break;
    case FS_DTMF_METHOD_IN_BAND:
      method_str="sound";
      gst_structure_set (structure, "method", G_TYPE_INT, 2, NULL);
      break;
    default:
      method_str="unknown (defaulting to auto)";
  }

  GST_DEBUG ("stopping telephony event using method=%s", method_str);

  return fs_rtp_special_sources_send_event (current_extra_sources,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, structure));
}

GList *
fs_rtp_special_sources_destroy (GList *current_extra_sources)
{
  g_list_foreach (current_extra_sources, (GFunc) g_object_unref, NULL);
  g_list_free (current_extra_sources);

  return NULL;
}

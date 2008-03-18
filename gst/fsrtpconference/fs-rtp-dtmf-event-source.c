/*
 * Farsight2 - Farsight RTP DTMF Event Source
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-dtmf-event-source.c - A Farsight RTP Event Source gobject
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
#include "fs-rtp-discover-codecs.h"

#include "fs-rtp-dtmf-event-source.h"

#define GST_CAT_DEFAULT fsrtpconference_debug

/**
 * SECTION:fs-rtp-dtmf-event-source
 * @short_description: Class to create the source of DTMF events
 *
 * This class is manages the DTMF Event source and related matters
 *
 */


/* props */
enum
{
  PROP_0,
  PROP_BIN,
  PROP_RTPMUXER
};

/* all privates variables are protected by the mutex */
struct _FsRtpDtmfEventSourcePrivate {
  gboolean disposed;

  GstElement *outer_bin;
  GstElement *rtpmuxer;

  GstElement *bin;

  GThread *stop_thread;

  GMutex *mutex;
};

static FsRtpSpecialSourceClass *parent_class = NULL;

G_DEFINE_TYPE(FsRtpDtmfEventSource, fs_rtp_dtmf_event_source,
    FS_TYPE_RTP_SPECIAL_SOURCE);

#define FS_RTP_DTMF_EVENT_SOURCE_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_DTMF_EVENT_SOURCE,             \
   FsRtpDtmfEventSourcePrivate))

static void fs_rtp_dtmf_event_source_set_property (GObject *object, guint prop_id,
  const GValue *value, GParamSpec *pspec);

static void fs_rtp_dtmf_event_source_dispose (GObject *object);
static void fs_rtp_dtmf_event_source_finalize (GObject *object);

static FsRtpSpecialSource *fs_rtp_dtmf_event_source_new (
    FsRtpSpecialSourceClass *klass,
    GList *negotiated_sources,
    FsCodec *selected_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    gboolean *last,
    GError **error);
static gboolean fs_rtp_dtmf_event_source_start_telephony_event (
    FsRtpSpecialSource *source,
    guint8 event,
    guint8 volume,
    FsDTMFMethod method);
static gboolean fs_rtp_dtmf_event_source_stop_telephony_event (
    FsRtpSpecialSource *source,
    FsDTMFMethod method);


static gboolean fs_rtp_dtmf_event_source_class_want_source (
    FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec);
static GList *fs_rtp_dtmf_event_source_class_add_blueprint (
    FsRtpSpecialSourceClass *klass,
    GList *blueprints);


#define FS_RTP_DTMF_EVENT_SOURCE_LOCK(src)   g_mutex_lock (src->priv->mutex)
#define FS_RTP_DTMF_EVENT_SOURCE_UNLOCK(src) g_mutex_unlock (src->priv->mutex)

static void
fs_rtp_dtmf_event_source_class_init (FsRtpDtmfEventSourceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  FsRtpSpecialSourceClass *spsource_class = FS_RTP_SPECIAL_SOURCE_CLASS (klass);
  parent_class = fs_rtp_dtmf_event_source_parent_class;

  gobject_class->dispose = fs_rtp_dtmf_event_source_dispose;
  gobject_class->finalize = fs_rtp_dtmf_event_source_finalize;
  gobject_class->set_property = fs_rtp_dtmf_event_source_set_property;

  spsource_class->new = fs_rtp_dtmf_event_source_new;
  spsource_class->want_source = fs_rtp_dtmf_event_source_class_want_source;
  spsource_class->add_blueprint = fs_rtp_dtmf_event_source_class_add_blueprint;
  spsource_class->start_telephony_event = fs_rtp_dtmf_event_source_start_telephony_event;
  spsource_class->stop_telephony_event = fs_rtp_dtmf_event_source_stop_telephony_event;

  g_object_class_install_property (gobject_class,
      PROP_BIN,
      g_param_spec_object ("bin",
          "The GstBin to add the elements to",
          "This is the GstBin where this class adds elements",
          GST_TYPE_BIN,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
      PROP_RTPMUXER,
      g_param_spec_object ("rtpmuxer",
          "The RTP muxer that the source is linked to",
          "The RTP muxer that the source is linked to",
          GST_TYPE_ELEMENT,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
}

static void
fs_rtp_dtmf_event_source_init (FsRtpDtmfEventSource *self)
{
  self->priv = FS_RTP_DTMF_EVENT_SOURCE_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->priv->mutex = g_mutex_new ();
}

static gpointer
stop_source_thread (gpointer data)
{
  FsRtpDtmfEventSource *self = FS_RTP_DTMF_EVENT_SOURCE (data);

  gst_element_set_locked_state (self->priv->bin, TRUE);
  gst_element_set_state (self->priv->bin, GST_STATE_NULL);

  FS_RTP_DTMF_EVENT_SOURCE_LOCK (self);
  gst_bin_remove (GST_BIN (self->priv->outer_bin), self->priv->bin);
  self->priv->bin = NULL;
  FS_RTP_DTMF_EVENT_SOURCE_UNLOCK (self);

  g_object_unref (self);

  return NULL;
}

static void
fs_rtp_dtmf_event_source_dispose (GObject *object)
{
  FsRtpDtmfEventSource *self = FS_RTP_DTMF_EVENT_SOURCE (object);

  FS_RTP_DTMF_EVENT_SOURCE_LOCK (self);

  if (self->priv->disposed)
  {
    FS_RTP_DTMF_EVENT_SOURCE_UNLOCK (self);
    return;
  }

  if (self->priv->bin)
  {
    GError *error = NULL;

    if (self->priv->stop_thread)
    {
      GST_DEBUG ("stopping thread for rtpdtmfsrc already running");
      return;
    }

    g_object_ref (self);
    self->priv->stop_thread = g_thread_create (stop_source_thread, self, FALSE,
        &error);

    if (!self->priv->stop_thread)
    {
      GST_WARNING ("Could not start stopping thread for FsRtpDtmfEventSource:"
          " %s", error->message);
    }
    g_clear_error (&error);

    FS_RTP_DTMF_EVENT_SOURCE_UNLOCK (self);
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

  FS_RTP_DTMF_EVENT_SOURCE_UNLOCK (self);

  G_OBJECT_CLASS (fs_rtp_dtmf_event_source_parent_class)->dispose (object);
}

static void
fs_rtp_dtmf_event_source_finalize (GObject *object)
{
  FsRtpDtmfEventSource *self = FS_RTP_DTMF_EVENT_SOURCE (object);

  if (self->priv->mutex)
    g_mutex_free (self->priv->mutex);
  self->priv->mutex = NULL;

  G_OBJECT_CLASS (fs_rtp_dtmf_event_source_parent_class)->finalize (object);
}

static void
fs_rtp_dtmf_event_source_set_property (GObject *object, guint prop_id,
  const GValue *value, GParamSpec *pspec)
{
  FsRtpDtmfEventSource *self = FS_RTP_DTMF_EVENT_SOURCE (object);

  switch (prop_id)
  {
    case PROP_BIN:
      self->priv->outer_bin = g_value_get_object (value);
      break;
    case PROP_RTPMUXER:
      self->priv->rtpmuxer = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * fs_rtp_dtmf_event_source_class_add_blueprint:
 *
 * Add one blueprint for telephone-event for each different clock-rate that
 * exists in the request
 */

static GList*
fs_rtp_dtmf_event_source_class_add_blueprint (FsRtpSpecialSourceClass *klass,
    GList *blueprints)
{
  GList *item;
  GList *already_done = NULL;
  GstElementFactory *fact = NULL;

  fact = gst_element_factory_find ("rtpdtmfsrc");
  if (fact)
  {
    gst_object_unref (fact);
  }
  else
  {
    GST_CAT_WARNING (fsrtpconference_disco,
        "Could not find rtpdtmfsrc, will not offer DTMF events");
    return blueprints;
  }

  for (item = g_list_first (blueprints);
       item;
       item = g_list_next (item))
  {
    CodecBlueprint *bp = item->data;
    GList *done_item = NULL;
    gboolean skip = FALSE;
    CodecBlueprint *new_bp = NULL;
    FsCodecParameter *param = NULL;

    if (bp->codec->media_type != FS_MEDIA_TYPE_AUDIO)
      continue;

    if (!g_ascii_strcasecmp (bp->codec->encoding_name, "telephone-event"))
      continue;

    for (done_item = g_list_first (already_done);
         done_item;
         done_item = g_list_next (done_item))
    {
      if (GPOINTER_TO_UINT (done_item->data) == bp->codec->clock_rate)
      {
        skip = TRUE;
        break;
      }
    }
    if (skip)
      continue;

    new_bp = g_new0 (CodecBlueprint, 1);

    new_bp->codec = fs_codec_new (FS_CODEC_ID_ANY, "telephone-event",
        FS_MEDIA_TYPE_AUDIO, bp->codec->clock_rate);
    param = g_new0 (FsCodecParameter, 1);
    param->name = g_strdup ("");
    param->value = g_strdup ("0-15");
    new_bp->codec->optional_params = g_list_prepend (NULL, param);

    blueprints = g_list_append (blueprints, new_bp);

    already_done = g_list_prepend (already_done,
        GUINT_TO_POINTER (bp->codec->clock_rate));
  }

  g_list_free (already_done);

  return blueprints;
}

/**
 * get_telephone_event_codec:
 * @codecs: a #GList of #FsCodec
 * @clock_rate: The clock rate to look for
 *
 * Find the telephone-event codec with the proper clock rate in the list
 *
 * Returns: The #FsCodec of type "telephone-event" with the requested clock-rate
 *   from the list, or %NULL
 */
static FsCodec *
get_telephone_event_codec (GList *codecs, guint clock_rate)
{
  GList *item = NULL;
  for (item = g_list_first (codecs);
       item;
       item = g_list_next (item))
  {
    FsCodec *codec = item->data;

    if (codec->media_type == FS_MEDIA_TYPE_AUDIO &&
        !g_ascii_strcasecmp (codec->encoding_name, "telephone-event") &&
        codec->clock_rate == clock_rate)
      return codec;
  }

   return NULL;
}

static gboolean
fs_rtp_dtmf_event_source_class_want_source (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec)
{
  if (selected_codec->media_type != FS_MEDIA_TYPE_AUDIO)
    return FALSE;

  if (get_telephone_event_codec (negotiated_codecs, selected_codec->clock_rate))
    return TRUE;
  else
    return FALSE;
}

static gboolean
fs_rtp_dtmf_event_source_build (FsRtpDtmfEventSource *self,
    GList *negotiated_codecs,
    FsCodec *selected_codec,
    GError **error)
{
  FsCodec *telephony_codec = NULL;
  GstCaps *caps = NULL;
  GstPad *pad = NULL;
  GstElement *dtmfsrc = NULL;
  GstElement *capsfilter = NULL;

  telephony_codec = get_telephone_event_codec (negotiated_codecs,
      selected_codec->clock_rate);

  if (!telephony_codec)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Could not find a telephone-event for the current codec's clock-rate");
    return FALSE;
  }

  if (!self->priv->outer_bin)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid bin set");
    return FALSE;
  }

  if (!self->priv->rtpmuxer)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid rtpmuxer set");
    return FALSE;
  }

  self->priv->bin = gst_bin_new (NULL);
  if (!gst_bin_add (GST_BIN (self->priv->outer_bin), self->priv->bin))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add bin to outer bin");
    gst_object_unref (self->priv->bin);
    self->priv->bin = NULL;
    return FALSE;
  }

  dtmfsrc = gst_element_factory_make ("rtpdtmfsrc", NULL);
  if (!dtmfsrc)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not make rtpdtmfsrc");
    goto error;
  }
  if (!gst_bin_add (GST_BIN (self->priv->bin), dtmfsrc))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add rtpdtmfsrc to bin");
    gst_object_unref (dtmfsrc);
    goto error;
  }

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  if (!capsfilter)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not make rtpcapsfilter");
    goto error;
  }
  if (!gst_bin_add (GST_BIN (self->priv->bin), capsfilter))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add capsfilter to bin");
    gst_object_unref (capsfilter);
    goto error;
  }

  caps = fs_codec_to_gst_caps (telephony_codec);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_object_unref (caps);

  pad = gst_element_get_static_pad (capsfilter, "src");
  if (!pad)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not get \"src\" pad from capsfilter");
    goto error;
  }
  if (!gst_element_add_pad (self->priv->bin, gst_ghost_pad_new ("src", pad)))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not get \"src\" ghostpad to dtmf source bin");
    gst_object_unref (pad);
    goto error;
  }
  gst_object_unref (pad);

  if (!gst_element_link_pads (self->priv->bin, "src", self->priv->rtpmuxer, NULL))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link rtpdtmfsrc src to muxer sink");
    goto error;
  }

  if (!gst_element_sync_state_with_parent (self->priv->bin))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not sync capsfilter state with its parent");
    goto error;
  }

  return TRUE;

 error:
  gst_bin_remove (GST_BIN (self->priv->outer_bin), self->priv->bin);
  self->priv->bin = NULL;

  return FALSE;
}

static FsRtpSpecialSource *
fs_rtp_dtmf_event_source_new (FsRtpSpecialSourceClass *klass,
    GList *negotiated_codecs,
    FsCodec *selected_codec,
    GstElement *bin,
    GstElement *rtpmuxer,
    gboolean *last,
    GError **error)
{
  FsRtpDtmfEventSource *self = NULL;

  self = g_object_new (FS_TYPE_RTP_DTMF_EVENT_SOURCE,
      "bin", bin,
      "rtpmuxer", rtpmuxer,
      NULL);
  g_assert (self);

  if (!fs_rtp_dtmf_event_source_build (self, negotiated_codecs,
          selected_codec, error))
  {
    g_object_unref (self);
    return NULL;
  }

  if (last)
    last = FALSE;

  return FS_RTP_SPECIAL_SOURCE_CAST (self);
}

static gboolean
fs_rtp_dtmf_event_source_start_telephony_event (FsRtpSpecialSource *source,
    guint8 event,
    guint8 volume,
    FsDTMFMethod method)
{
  GstStructure *structure = NULL;
  FsRtpDtmfEventSource *self = FS_RTP_DTMF_EVENT_SOURCE (source);
  gchar *method_str;

  if (method != FS_DTMF_METHOD_RTP_RFC4733 &&
      method != FS_DTMF_METHOD_AUTO)
    return FALSE;

  structure = gst_structure_new ("dtmf-event",
      "number", G_TYPE_INT, event,
      "volume", G_TYPE_INT, volume,
      "start", G_TYPE_BOOLEAN, TRUE,
      NULL);

  switch (method)
  {
    case FS_DTMF_METHOD_AUTO:
      method_str = "default";
      break;
    case FS_DTMF_METHOD_RTP_RFC4733:
      method_str="RFC4733";
      gst_structure_set (structure, "type", G_TYPE_INT, 1, NULL);
      gst_structure_set (structure, "method", G_TYPE_INT, method, NULL);
      break;
    default:
      method_str="other";
  }

  GST_DEBUG ("sending telephony event %d using method=%s",
      event, method_str);

  return gst_element_send_event (self->priv->bin,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, structure));
}


static gboolean
fs_rtp_dtmf_event_source_stop_telephony_event (FsRtpSpecialSource *source,
    FsDTMFMethod method)
{
  GstStructure *structure = NULL;
  FsRtpDtmfEventSource *self = FS_RTP_DTMF_EVENT_SOURCE (source);
  gchar *method_str;

  if (method != FS_DTMF_METHOD_RTP_RFC4733 &&
      method != FS_DTMF_METHOD_AUTO)
    return FALSE;

  structure = gst_structure_new ("dtmf-event",
      "start", G_TYPE_BOOLEAN, FALSE,
      NULL);

  switch (method)
  {
    case FS_DTMF_METHOD_AUTO:
      method_str = "default";
      break;
    case FS_DTMF_METHOD_RTP_RFC4733:
      method_str="RFC4733";
      gst_structure_set (structure, "type", G_TYPE_INT, 1, NULL);
      gst_structure_set (structure, "method", G_TYPE_INT, method, NULL);
      break;
    default:
      method_str="other";
  }

  GST_DEBUG ("stopping telephony event using method=%s", method_str);

  return gst_element_send_event (self->priv->bin,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, structure));
}

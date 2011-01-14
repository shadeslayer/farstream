/*
 * Farsight2 - Farsight Raw Conference Implementation
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007-2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Mike Ruprecht <mike.ruprecht@collabora.co.uk>
 *
 * fs-raw-conference.c - Raw implementation for Farsight Conference Gstreamer
 *                       Elements
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
 * SECTION:fs-raw-conference
 * @short_description: Farsight Raw Conference Gstreamer Elements Base class
 *
 * This element implements a raw content stream over which any Gstreamer
 * content may travel.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-raw-conference.h"

#include "fs-raw-session.h"
#include "fs-raw-participant.h"

GST_DEBUG_CATEGORY (fsrawconference_debug);
#define GST_CAT_DEFAULT fsrawconference_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0
};


static GstStaticPadTemplate fs_raw_conference_sink_template =
  GST_STATIC_PAD_TEMPLATE ("sink_%d",
      GST_PAD_SINK,
      GST_PAD_SOMETIMES,
      GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate fs_raw_conference_src_template =
  GST_STATIC_PAD_TEMPLATE ("src_%d",
      GST_PAD_SRC,
      GST_PAD_SOMETIMES,
      GST_STATIC_CAPS_ANY);

#define FS_RAW_CONFERENCE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), FS_TYPE_RAW_CONFERENCE,  \
      FsRawConferencePrivate))

struct _FsRawConferencePrivate
{
  gboolean disposed;

  /* Protected by GST_OBJECT_LOCK */
  GList *sessions;
  guint max_session_id;

  GList *participants;

  /* Array of all internal threads, as GThreads */
  GPtrArray *threads;
};

static void fs_raw_conference_do_init (GType type);


GST_BOILERPLATE_FULL (FsRawConference, fs_raw_conference, FsBaseConference,
    FS_TYPE_BASE_CONFERENCE, fs_raw_conference_do_init);

static FsSession *fs_raw_conference_new_session (FsBaseConference *conf,
    FsMediaType media_type,
    GError **error);

static FsParticipant *fs_raw_conference_new_participant (FsBaseConference *conf,
    const gchar *cname,
    GError **error);

static void _remove_session (gpointer user_data,
    GObject *where_the_object_was);

static void _remove_participant (gpointer user_data,
    GObject *where_the_object_was);

static void fs_raw_conference_handle_message (
    GstBin * bin,
    GstMessage * message);


static void
fs_raw_conference_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (fsrawconference_debug, "fsrawconference", 0,
                           "Farsight Raw Conference Element");
}

static void
fs_raw_conference_dispose (GObject * object)
{
  FsRawConference *self = FS_RAW_CONFERENCE (object);
  GList *item;

  if (self->priv->disposed)
    return;

  for (item = g_list_first (self->priv->participants);
       item;
       item = g_list_next (item))
    g_object_weak_unref (G_OBJECT (item->data), _remove_participant, self);
  g_list_free (self->priv->participants);
  self->priv->participants = NULL;

  self->priv->disposed = TRUE;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
fs_raw_conference_finalize (GObject * object)
{
  FsRawConference *self = FS_RAW_CONFERENCE (object);

  g_ptr_array_free (self->priv->threads, TRUE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
fs_raw_conference_class_init (FsRawConferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  FsBaseConferenceClass *baseconf_class = FS_BASE_CONFERENCE_CLASS (klass);
  GstBinClass *gstbin_class = GST_BIN_CLASS (klass);

  g_type_class_add_private (klass, sizeof (FsRawConferencePrivate));

  baseconf_class->new_session =
    GST_DEBUG_FUNCPTR (fs_raw_conference_new_session);
  baseconf_class->new_participant =
    GST_DEBUG_FUNCPTR (fs_raw_conference_new_participant);

  gstbin_class->handle_message =
    GST_DEBUG_FUNCPTR (fs_raw_conference_handle_message);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (fs_raw_conference_finalize);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (fs_raw_conference_dispose);
}

static void
fs_raw_conference_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_raw_conference_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fs_raw_conference_src_template));
}

static void
fs_raw_conference_init (FsRawConference *conf,
                        FsRawConferenceClass *bclass)
{
  GST_DEBUG_OBJECT (conf, "fs_raw_conference_init");

  conf->priv = FS_RAW_CONFERENCE_GET_PRIVATE (conf);

  conf->priv->max_session_id = 1;

  conf->priv->threads = g_ptr_array_new ();
}

/**
 * fs_rtp_conference_get_session_by_id_locked
 * @self: The #FsRawConference
 * @session_id: The session id
 *
 * Gets the #FsRawSession from a list of sessions or NULL if it doesnt exist
 * You have to hold the GST_OBJECT_LOCK to call this function.
 *
 * Return value: A #FsRawSession (unref after use) or NULL if it doesn't exist
 */
static FsRawSession *
fs_raw_conference_get_session_by_id_locked (FsRawConference *self,
                                            guint session_id)
{
  GList *item = NULL;

  for (item = g_list_first (self->priv->sessions);
       item;
       item = g_list_next (item)) {
    FsRawSession *session = item->data;

    if (session->id == session_id) {
      g_object_ref (session);
      break;
    }
  }

  if (item)
    return FS_RAW_SESSION (item->data);
  else
    return NULL;
}

static void
_remove_session (gpointer user_data,
                 GObject *where_the_object_was)
{
  FsRawConference *self = FS_RAW_CONFERENCE (user_data);

  GST_OBJECT_LOCK (self);
  self->priv->sessions =
    g_list_remove_all (self->priv->sessions, where_the_object_was);
  GST_OBJECT_UNLOCK (self);
}

static void
_remove_participant (gpointer user_data,
                     GObject *where_the_object_was)
{
  FsRawConference *self = FS_RAW_CONFERENCE (user_data);

  GST_OBJECT_LOCK (self);
  self->priv->participants =
    g_list_remove_all (self->priv->participants, where_the_object_was);
  GST_OBJECT_UNLOCK (self);
}

static FsSession *
fs_raw_conference_new_session (FsBaseConference *conf,
                               FsMediaType media_type,
                               GError **error)
{
  FsRawConference *self = FS_RAW_CONFERENCE (conf);
  FsRawSession *new_session = NULL;
  guint id;

  GST_OBJECT_LOCK (self);
  do {
    id = self->priv->max_session_id++;
  } while (fs_raw_conference_get_session_by_id_locked (self, id));
  GST_OBJECT_UNLOCK (self);

  new_session = fs_raw_session_new (media_type, self, id, error);

  GST_OBJECT_LOCK (self);
  self->priv->sessions = g_list_append (self->priv->sessions, new_session);
  GST_OBJECT_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_session), _remove_session, self);

  return FS_SESSION (new_session);
}


static FsParticipant *
fs_raw_conference_new_participant (FsBaseConference *conf,
                                   const gchar *cname,
                                   GError **error)
{
  FsRawConference *self = FS_RAW_CONFERENCE (conf);
  FsParticipant *new_participant = NULL;

  new_participant = FS_PARTICIPANT_CAST (fs_raw_participant_new ());

  GST_OBJECT_LOCK (self);
  self->priv->participants = g_list_append (self->priv->participants,
      new_participant);
  GST_OBJECT_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_participant), _remove_participant, self);

  return new_participant;
}

static void
fs_raw_conference_handle_message (
    GstBin * bin,
    GstMessage * message)
{
  FsRawConference *self = FS_RAW_CONFERENCE (bin);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STREAM_STATUS:
    {
      GstStreamStatusType type;
      guint i;

      gst_message_parse_stream_status (message, &type, NULL);

      switch (type)
      {
        case GST_STREAM_STATUS_TYPE_ENTER:
          GST_OBJECT_LOCK (self);
          for (i = 0; i < self->priv->threads->len; i++)
          {
            if (g_ptr_array_index (self->priv->threads, i) ==
                g_thread_self ())
              goto done;
          }
          g_ptr_array_add (self->priv->threads, g_thread_self ());
        done:
          GST_OBJECT_UNLOCK (self);
          break;

        case GST_STREAM_STATUS_TYPE_LEAVE:
          GST_OBJECT_LOCK (self);
          while (g_ptr_array_remove_fast (self->priv->threads,
                  g_thread_self ()));
          GST_OBJECT_UNLOCK (self);
          break;

        default:
          /* Do nothing */
          break;
      }
    }
      break;
    default:
      break;
  }

  /* forward all messages to the parent */
  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

typedef struct
{
  const gchar *prop_name;
  GType prop_type;
} CapPropertyMapItem;

/**
 * fs_codec_to_gst_caps
 * @codec: A #FsCodec to be converted
 *
 * This function converts a #FsCodec to a fixed #GstCaps.
 *
 * Return value: A newly-allocated #GstCaps or %NULL if the codec was %NULL
 */

GstCaps *
fs_raw_codec_to_gst_caps (const FsCodec *codec)
{
  GstStructure *structure;
  GList *item;
  CapPropertyMapItem *iter;

  /* TODO: Add the appropriate video properties */
  CapPropertyMapItem prop_map[] = {
      {"endianness", G_TYPE_INT},
      {"signed", G_TYPE_BOOLEAN},
      {"width", G_TYPE_INT},
      {"depth", G_TYPE_INT},
      {"rate", G_TYPE_INT},
      {NULL, G_TYPE_INVALID}
  };

  if (codec == NULL || codec->encoding_name == NULL)
    return NULL;

  structure = gst_structure_new (codec->encoding_name, NULL);

  if (codec->channels)
    gst_structure_set (structure, "channels", G_TYPE_INT, codec->channels,
      NULL);

  for (item = codec->optional_params;
       item;
       item = g_list_next (item)) {
    FsCodecParameter *param = item->data;

    for (iter = prop_map; iter->prop_name; ++iter) {  
      if (g_strcmp0 (param->name, iter->prop_name) != 0)
        continue;

      switch (iter->prop_type) {
        case G_TYPE_INT:
          gst_structure_set (structure, param->name,
              G_TYPE_INT, atoi (param->value), NULL);
          break;
        case G_TYPE_BOOLEAN: {
          int tmp = atoi (param->value);
          if (tmp > 1 || tmp < 0)
            GST_WARNING ("Caps param %s is not a boolean ('0' or '1') %s",
                param->name, param->value);
            break;
          gst_structure_set (structure, param->name,
              G_TYPE_BOOLEAN, tmp, NULL);
          break;
        }
        default:
          GST_WARNING ("No type defined for this parameter, "
              "defaulting to G_TYPE_STRING");
          gst_structure_set (structure, param->name,
              G_TYPE_STRING, param->value, NULL);
          break;
      }
    }
  }

  return gst_caps_new_full (structure, NULL);
}

gboolean
fs_raw_conference_is_internal_thread (FsRawConference *self)
{
  guint i;
  gboolean ret = FALSE;

  GST_OBJECT_LOCK (self);
  for (i = 0; i < self->priv->threads->len; i++)
  {
    if (g_ptr_array_index (self->priv->threads, i))
    {
      ret = TRUE;
      break;
    }
  }
  GST_OBJECT_UNLOCK (self);

  return ret;
}


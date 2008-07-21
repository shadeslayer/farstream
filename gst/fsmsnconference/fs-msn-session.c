/*
 * Farsight2 - Farsight MSN Session
 *
 *  @author: Richard Spiers <richard.spiers@gmail.com>
 *
 * fs-msn-session.c - A Farsight Msn Session gobject
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
 * SECTION:fs-msn-session
 * @short_description: A  MSN session in a #FsMsnConference
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>

#include "fs-msn-session.h"
#include "fs-msn-stream.h"
#include "fs-msn-participant.h"

#define GST_CAT_DEFAULT fsmsnconference_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_MEDIA_TYPE,
  PROP_ID,
  PROP_SINK_PAD,
  PROP_CODEC_PREFERENCES,
  PROP_CODECS,
  PROP_CODECS_WITHOUT_CONFIG,
  PROP_CURRENT_SEND_CODEC,
  PROP_CODECS_READY,
  PROP_CONFERENCE
};



struct _FsMsnSessionPrivate
{
  FsMediaType media_type;

  /* We dont need a reference to this one per our reference model
   * This Session object can only exist while its parent conference exists
   */
  FsMsnConference *conference;

  /* We keep references to these elements
   */

  GstElement *media_sink_valve;
  GstElement *send_tee;
 
  /* Request pads that are disposed of when the tee is disposed of */
  GstPad *send_tee_media_pad;
  GstPad *send_tee_discovery_pad;

  /* We dont keep explicit references to the pads, the Bin does that for us
   * only this element's methods can add/remove it
   */
  GstPad *media_sink_pad;


  /* Request pad to release on dispose */
  GstPad *send_msn_sink;
  GstPad *recv_msn_sink;
 
  /* These lists are protected by the session mutex */
  GList *streams;
  GList *free_substreams;

  GList *extra_sources;

  GError *construction_error;

  gboolean disposed;
};

G_DEFINE_TYPE (FsMsnSession, fs_msn_session, FS_TYPE_SESSION);

#define FS_MSN_SESSION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_MSN_SESSION, FsMsnSessionPrivate))

static void fs_msn_session_dispose (GObject *object);
static void fs_msn_session_finalize (GObject *object);

static void fs_msn_session_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_msn_session_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);

static void fs_msn_session_constructed (GObject *object);

static FsStream *fs_msn_session_new_stream (FsSession *session,
    FsParticipant *participant,
    FsStreamDirection direction,
    guint n_parameters,
    GParameter *parameters,
    GError **error);


static void _remove_stream (gpointer user_data,
    GObject *where_the_object_was);

static GObjectClass *parent_class = NULL;

//static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_msn_session_class_init (FsMsnSessionClass *klass)
{
  GObjectClass *gobject_class;
  FsSessionClass *session_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);
  session_class = FS_SESSION_CLASS (klass);

  gobject_class->set_property = fs_msn_session_set_property;
  gobject_class->get_property = fs_msn_session_get_property;
  gobject_class->constructed = fs_msn_session_constructed;

  session_class->new_stream = fs_msn_session_new_stream;

  g_object_class_override_property (gobject_class,
    PROP_MEDIA_TYPE, "media-type");
  g_object_class_override_property (gobject_class,
    PROP_ID, "id");
  g_object_class_override_property (gobject_class,
    PROP_SINK_PAD, "sink-pad");

  g_object_class_install_property (gobject_class,
    PROP_CONFERENCE,
    g_param_spec_object ("conference",
      "The Conference this stream refers to",
      "This is a convience pointer for the Conference",
      FS_TYPE_MSN_CONFERENCE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  gobject_class->dispose = fs_msn_session_dispose;
  gobject_class->finalize = fs_msn_session_finalize;

  g_type_class_add_private (klass, sizeof (FsMsnSessionPrivate));
}

static void
fs_msn_session_init (FsMsnSession *self)
{
  /* member init */
  self->priv = FS_MSN_SESSION_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
  self->priv->construction_error = NULL;

  g_static_rec_mutex_init (&self->mutex);

  self->priv->media_type = FS_MEDIA_TYPE_LAST + 1;
}

static void
stop_and_remove (GstBin *conf, GstElement **element, gboolean unref)
{
  if (*element == NULL)
    return;

  gst_element_set_locked_state (*element, TRUE);
  gst_element_set_state (*element, GST_STATE_NULL);
  gst_bin_remove (conf, *element);
  if (unref)
    gst_object_unref (*element);
  *element = NULL;
}

static void
fs_msn_session_dispose (GObject *object)
{
  FsMsnSession *self = FS_MSN_SESSION (object);
  GList *item = NULL;
  GstBin *conferencebin = NULL;

  if (self->priv->disposed)
    /* If dispose did already run, return. */
    return;

  conferencebin = GST_BIN (self->priv->conference);

  /* Lets stop all of the elements sink to source */

  /* First the send pipeline */
   //See original


  /* Now the recv pipeline */
 
  
  /* Now they should all be stopped, we can remove them in peace */

  FS_MSN_SESSION_UNLOCK (self);

  /* MAKE sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_msn_session_finalize (GObject *object)
{
  FsMsnSession *self = FS_MSN_SESSION (object);

  g_static_rec_mutex_free (&self->mutex);
  
  parent_class->finalize (object);
}

static void
fs_msn_session_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsMsnSession *self = FS_MSN_SESSION (object);

  switch (prop_id)
  {
    case PROP_MEDIA_TYPE:
      g_value_set_enum (value, self->priv->media_type);
      break;
    case PROP_ID:
      g_value_set_uint (value, self->id);
      break;
    case PROP_SINK_PAD:
      g_value_set_object (value, self->priv->media_sink_pad);
      break;
    case PROP_CONFERENCE:
      g_value_set_object (value, self->priv->conference);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_msn_session_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  FsMsnSession *self = FS_MSN_SESSION (object);

  switch (prop_id)
  {
    case PROP_MEDIA_TYPE:
      self->priv->media_type = g_value_get_enum (value);
      break;
    case PROP_ID:
      self->id = g_value_get_uint (value);
      break;
    case PROP_CONFERENCE:
      self->priv->conference = FS_MSN_CONFERENCE (g_value_dup_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_msn_session_constructed (GObject *object)
{
  FsMsnSession *self = FS_MSN_SESSION_CAST (object);
  GstElement *valve = NULL;
  GstElement *capsfilter = NULL;
  GstElement *tee = NULL;

  GstElement *fakesink = NULL;
  GstPad *tee_sink_pad = NULL;
  GstPad *valve_sink_pad = NULL;
  
  GstPad *pad1, *pad2;
  GstPadLinkReturn ret;
  gchar *tmp;

  if (self->id == 0)
  {
    g_error ("You can no instantiate this element directly, you MUST"
      " call fs_msn_session_new ()");
    return;
  }

  tmp = g_strdup_printf ("send_tee_%u", self->id);
  tee = gst_element_factory_make ("tee", tmp);
  g_free (tmp);

  if (!tee)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), tee))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the tee element to the FsMsnConference");
    gst_object_unref (tee);
    return;
  }

  gst_element_set_state (tee, GST_STATE_PLAYING);

  self->priv->send_tee = gst_object_ref (tee);


  tee_sink_pad = gst_element_get_static_pad (tee, "sink");

  tmp = g_strdup_printf ("sink_%u", self->id);
  self->priv->media_sink_pad = gst_ghost_pad_new (tmp, tee_sink_pad);
  g_free (tmp);

  if (!self->priv->media_sink_pad)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not create ghost pad for tee's sink pad");
    return;
  }

  gst_pad_set_active (self->priv->media_sink_pad, TRUE);
  if (!gst_element_add_pad (GST_ELEMENT (self->priv->conference),
          self->priv->media_sink_pad))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not add ghost pad to the conference bin");
    gst_object_unref (self->priv->media_sink_pad);
    self->priv->media_sink_pad = NULL;
    return;
  }

  gst_object_unref (tee_sink_pad);

  self->priv->send_tee_discovery_pad = gst_element_get_request_pad (tee,
      "src%d");
  self->priv->send_tee_media_pad = gst_element_get_request_pad (tee,
      "src%d");

  if (!self->priv->send_tee_discovery_pad || !self->priv->send_tee_media_pad)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not create the send tee request src pads");
  }

  tmp = g_strdup_printf ("valve_send_%u", self->id);
  valve = gst_element_factory_make ("fsvalve", tmp);
  g_free (tmp);

  if (!valve)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the fsvalve element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), valve))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the valve element to the FsMSNConference");
    gst_object_unref (valve);
    return;
  }

  g_object_set (G_OBJECT (valve), "drop", TRUE, NULL);
  gst_element_set_state (valve, GST_STATE_PLAYING);

  self->priv->media_sink_valve = gst_object_ref (valve);

  valve_sink_pad = gst_element_get_static_pad (valve, "sink");

  if (GST_PAD_LINK_FAILED (gst_pad_link (self->priv->send_tee_media_pad,
              valve_sink_pad)))
  {
    gst_object_unref (valve_sink_pad);

    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link send tee and valve");
    return;
  }

  gst_object_unref (valve_sink_pad);


  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
}


static void
_remove_stream (gpointer user_data,
    GObject *where_the_object_was)
{
  FsMsnSession *self = FS_MSN_SESSION (user_data);

  FS_MSN_SESSION_LOCK (self);
  self->priv->streams =
    g_list_remove_all (self->priv->streams, where_the_object_was);
  FS_MSN_SESSION_UNLOCK (self);
}

/**
 * fs_msn_session_new_stream:
 * @session: an #FsMsnSession
 * @participant: #FsParticipant of a participant for the new stream
 * @direction: #FsStreamDirection describing the direction of the new stream that will
 * be created for this participant
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function creates a stream for the given participant into the active session.
 *
 * Returns: the new #FsStream that has been created. User must unref the
 * #FsStream when the stream is ended. If an error occured, returns NULL.
 */
static FsStream *
fs_msn_session_new_stream (FsSession *session,
    FsParticipant *participant,
    FsStreamDirection direction,
    guint n_parameters,
    GParameter *parameters,
    GError **error)
{
  FsMsnSession *self = FS_MSN_SESSION (session);
  FsMsnParticipant *msnparticipant = NULL;
  FsStream *new_stream = NULL;


  if (!FS_IS_MSN_PARTICIPANT (participant))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "You have to provide a participant of type MSN");
    return NULL;
  }
  msnparticipant = FS_MSN_PARTICIPANT (participant);

  new_stream = FS_STREAM_CAST (fs_msn_stream_new (self, msnparticipant,
      direction,error));

  FS_MSN_SESSION_LOCK (self);
  self->priv->streams = g_list_append (self->priv->streams, new_stream);
  FS_MSN_SESSION_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_stream), _remove_stream, self);

  return new_stream;
}

FsMsnSession *
fs_msn_session_new (FsMediaType media_type, FsMsnConference *conference,
                    guint id, GError **error)
{
  FsMsnSession *session = g_object_new (FS_TYPE_MSN_SESSION,
    "media-type", media_type,
    "conference", conference,
    "id", id,
    NULL);

  if (session->priv->construction_error)
  {
    g_propagate_error (error, session->priv->construction_error);
    g_object_unref (session);
    return NULL;
  }

  return session;
}


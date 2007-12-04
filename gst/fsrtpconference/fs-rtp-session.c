/*
 * Farsight2 - Farsight RTP Session
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-session.c - A Farsight RTP Session gobject
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
 * SECTION:fs-rtp-session
 * @short_description: A  RTP session in a #FsRtpConference
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>

#include <gst/farsight/fs-transmitter.h>

#include "fs-rtp-session.h"
#include "fs-rtp-stream.h"
#include "fs-rtp-participant.h"
#include "fs-rtp-discover-codecs.h"
#include "fs-rtp-codec-negotiation.h"
#include "fs-rtp-substream.h"

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
  PROP_LOCAL_CODECS,
  PROP_LOCAL_CODECS_CONFIG,
  PROP_NEGOTIATED_CODECS,
  PROP_CURRENT_SEND_CODEC,
  PROP_CONFERENCE
};

struct _FsRtpSessionPrivate
{
  FsMediaType media_type;

  /* We dont need a reference to this one per our reference model
   * This Session object can only exist while its parent conference exists
   */
  FsRtpConference *conference;

  GHashTable *transmitters;

  /* We keep references to these elements
   */

  GstElement *media_sink_valve;
  GstElement *send_capsfilter;
  GstElement *transmitter_rtp_tee;
  GstElement *transmitter_rtcp_tee;
  GstElement *transmitter_rtp_funnel;
  GstElement *transmitter_rtcp_funnel;

  GstElement *rtpmuxer;

  /* We dont keep explicit references to the pads, the Bin does that for us
   * only this element's methods can add/remote it
   */
  GstPad *media_sink_pad;

  /* Request pad to release on dispose */
  GstPad *rtpbin_send_rtp_sink;
  GstPad *rtpbin_send_rtcp_src;

  GstPad *rtpbin_recv_rtp_sink;
  GstPad *rtpbin_recv_rtcp_sink;

  /* Protected by the session mutex */
  /* The codec bin is owned implicitely by the Conference bin for us */
  GstElement *send_codecbin;
  FsCodec *current_send_codec;
  FsCodec *requested_send_codec;

  /* This is the id of the pad probe used to blocked the stream
   * while the codec is changed
   * Protected by the session mutex
   */
  gulong send_blocking_id;

  /* These lists are protected by the session mutex */
  GList *streams;
  GList *free_substreams;

  /* The static list of all the blueprints */
  GList *blueprints;

  GList *local_codecs_configuration;

  GList *local_codecs;
  GHashTable *local_codec_associations;

  /* These are protected by the session mutex */
  GList *negotiated_codecs;
  GHashTable *negotiated_codec_associations;

  GError *construction_error;

  gboolean disposed;
};

#define FS_RTP_SESSION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_SESSION, FsRtpSessionPrivate))

static void fs_rtp_session_class_init (FsRtpSessionClass *klass);
static void fs_rtp_session_init (FsRtpSession *self);
static void fs_rtp_session_dispose (GObject *object);
static void fs_rtp_session_finalize (GObject *object);

static void fs_rtp_session_get_property (GObject *object,
                                         guint prop_id,
                                         GValue *value,
                                         GParamSpec *pspec);
static void fs_rtp_session_set_property (GObject *object,
                                         guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec);

static void fs_rtp_session_constructed (GObject *object);

static FsStream *fs_rtp_session_new_stream (FsSession *session,
                                            FsParticipant *participant,
                                            FsStreamDirection direction,
                                            gchar *transmitter,
                                            guint n_parameters,
                                            GParameter *parameters,
                                            GError **error);
static gboolean fs_rtp_session_start_telephony_event (FsSession *session,
                                                      guint8 event,
                                                      guint8 volume,
                                                      FsDTMFMethod method);
static gboolean fs_rtp_session_stop_telephony_event (FsSession *session,
                                                     FsDTMFMethod method);
static gboolean fs_rtp_session_set_send_codec (FsSession *session,
                                               FsCodec *send_codec,
                                               GError **error);

static gboolean fs_rtp_session_verify_send_codec_bin_locked (
    FsRtpSession *self,
    GError **error);


static FsStreamTransmitter *fs_rtp_session_get_new_stream_transmitter (
  FsRtpSession *self,
  gchar *transmitter_name,
  FsParticipant *participant,
  guint n_parameters,
  GParameter *parameters,
  GError **error);


static GObjectClass *parent_class = NULL;

//static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_rtp_session_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsRtpSessionClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_rtp_session_class_init,
      NULL,
      NULL,
      sizeof (FsRtpSession),
      0,
      (GInstanceInitFunc) fs_rtp_session_init
    };

    type = g_type_register_static (FS_TYPE_SESSION,
        "FsRtpSession", &info, 0);
  }

  return type;
}

static void
fs_rtp_session_class_init (FsRtpSessionClass *klass)
{
  GObjectClass *gobject_class;
  FsSessionClass *session_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);
  session_class = FS_SESSION_CLASS (klass);

  gobject_class->set_property = fs_rtp_session_set_property;
  gobject_class->get_property = fs_rtp_session_get_property;
  gobject_class->constructed = fs_rtp_session_constructed;

  session_class->new_stream = fs_rtp_session_new_stream;
  session_class->start_telephony_event = fs_rtp_session_start_telephony_event;
  session_class->stop_telephony_event = fs_rtp_session_stop_telephony_event;
  session_class->set_send_codec = fs_rtp_session_set_send_codec;

  g_object_class_override_property (gobject_class,
    PROP_MEDIA_TYPE, "media-type");
  g_object_class_override_property (gobject_class,
    PROP_ID, "id");
  g_object_class_override_property (gobject_class,
    PROP_SINK_PAD, "sink-pad");
  g_object_class_override_property (gobject_class,
    PROP_LOCAL_CODECS, "local-codecs");
  g_object_class_override_property (gobject_class,
    PROP_LOCAL_CODECS_CONFIG, "local-codecs-config");
  g_object_class_override_property (gobject_class,
    PROP_NEGOTIATED_CODECS, "negotiated-codecs");
  g_object_class_override_property (gobject_class,
    PROP_CURRENT_SEND_CODEC, "current-send-codec");

  g_object_class_install_property (gobject_class,
    PROP_CONFERENCE,
    g_param_spec_object ("conference",
      "The Conference this stream refers to",
      "This is a convience pointer for the Conference",
      FS_TYPE_RTP_CONFERENCE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  gobject_class->dispose = fs_rtp_session_dispose;
  gobject_class->finalize = fs_rtp_session_finalize;

  g_type_class_add_private (klass, sizeof (FsRtpSessionPrivate));
}

static void
fs_rtp_session_init (FsRtpSession *self)
{
  /* member init */
  self->priv = FS_RTP_SESSION_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
  self->priv->construction_error = NULL;

  self->priv->transmitters = g_hash_table_new_full (g_str_hash, g_str_equal,
    g_free, g_object_unref);

  self->mutex = g_mutex_new ();

  self->priv->media_type = FS_MEDIA_TYPE_LAST + 1;
}

static gboolean
_remove_transmitter (gpointer key, gpointer value, gpointer user_data)
{
  FsRtpSession *self = FS_RTP_SESSION (user_data);
  FsTransmitter *transmitter = FS_TRANSMITTER (value);
  GstElement *src, *sink;

  g_object_get (transmitter, "gst-sink", &sink, "gst-src", &src, NULL);

  gst_element_set_state (src, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self->priv->conference), src);
  gst_element_set_state (src, GST_STATE_NULL);

  gst_element_set_state (sink, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self->priv->conference), sink);
  gst_element_set_state (sink, GST_STATE_NULL);

  gst_object_unref (src);
  gst_object_unref (sink);

  return TRUE;
}

static void
_stop_transmitter_elem (gpointer key, gpointer value, gpointer elem_name)
{
  FsTransmitter *transmitter = FS_TRANSMITTER (value);
  GstElement *elem = NULL;

  g_object_get (transmitter, elem_name, &elem, NULL);

  gst_element_set_state (elem, GST_STATE_NULL);
}

static void
fs_rtp_session_dispose (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Lets stop all of the elements sink to source */

  /* First the send pipeline */
  if (self->priv->transmitters)
    g_hash_table_foreach (self->priv->transmitters, _stop_transmitter_elem,
      "gst-sink");
  if (self->priv->rtpbin_send_rtcp_src)
    gst_pad_set_active (self->priv->rtpbin_send_rtcp_src, FALSE);
  if (self->priv->transmitter_rtcp_tee)
    gst_element_set_state (self->priv->transmitter_rtcp_tee, GST_STATE_NULL);
  if (self->priv->transmitter_rtp_tee)
    gst_element_set_state (self->priv->transmitter_rtp_tee, GST_STATE_NULL);

  if (self->priv->rtpmuxer)
    gst_element_set_state (self->priv->rtpmuxer, GST_STATE_NULL);
  if (self->priv->send_capsfilter)
    gst_element_set_state (self->priv->send_capsfilter, GST_STATE_NULL);
  if (self->priv->send_codecbin)
    gst_element_set_state (self->priv->send_codecbin, GST_STATE_NULL);
  if (self->priv->media_sink_valve)
    gst_element_set_state (self->priv->media_sink_valve, GST_STATE_NULL);
  if (self->priv->media_sink_pad)
    gst_pad_set_active (self->priv->media_sink_pad, FALSE);


  /* Now the recv pipeline */
  if (self->priv->free_substreams)
    g_list_foreach (self->priv->free_substreams, (GFunc) fs_rtp_sub_stream_stop,
      NULL);
  if (self->priv->transmitter_rtp_funnel)
    gst_element_set_state (self->priv->transmitter_rtp_funnel, GST_STATE_NULL);
  if (self->priv->transmitter_rtcp_funnel)
    gst_element_set_state (self->priv->transmitter_rtcp_funnel, GST_STATE_NULL);
  if (self->priv->transmitters)
    g_hash_table_foreach (self->priv->transmitters, _stop_transmitter_elem,
      "gst-src");

  /* Now they should all be stopped, we can remove them in peace */

  if (self->priv->media_sink_valve) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->media_sink_valve);
    gst_element_set_state (self->priv->media_sink_valve, GST_STATE_NULL);
    gst_object_unref (self->priv->media_sink_valve);
    self->priv->media_sink_valve = NULL;
  }

  if (self->priv->rtpmuxer) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->rtpmuxer);
    gst_element_set_state (self->priv->rtpmuxer, GST_STATE_NULL);
    gst_object_unref (self->priv->rtpmuxer);
    self->priv->rtpmuxer = NULL;
  }

  if (self->priv->send_codecbin) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->send_codecbin);
    gst_element_set_state (self->priv->send_codecbin, GST_STATE_NULL);
    gst_object_unref (self->priv->send_codecbin);
    self->priv->send_codecbin = NULL;
  }

  if (self->priv->send_capsfilter) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->send_capsfilter);
    gst_element_set_state (self->priv->send_capsfilter, GST_STATE_NULL);
    gst_object_unref (self->priv->send_capsfilter);
    self->priv->send_capsfilter = NULL;
  }

  if (self->priv->media_sink_pad) {
    gst_pad_set_active (self->priv->media_sink_pad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (self->priv->conference),
      self->priv->media_sink_pad);
    self->priv->media_sink_pad = NULL;
  }

  if (self->priv->transmitter_rtp_tee) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->transmitter_rtp_tee);
    gst_element_set_state (self->priv->transmitter_rtp_tee, GST_STATE_NULL);
    gst_object_unref (self->priv->transmitter_rtp_tee);
    self->priv->transmitter_rtp_tee = NULL;
  }

  if (self->priv->transmitter_rtcp_tee) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->transmitter_rtcp_tee);
    gst_element_set_state (self->priv->transmitter_rtcp_tee, GST_STATE_NULL);
    gst_object_unref (self->priv->transmitter_rtcp_tee);
    self->priv->transmitter_rtcp_tee = NULL;
  }

  if (self->priv->rtpbin_send_rtcp_src) {
    gst_pad_set_active (self->priv->rtpbin_send_rtcp_src, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_send_rtcp_src);
    self->priv->rtpbin_send_rtcp_src = NULL;
  }

  if (self->priv->transmitter_rtp_funnel) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->transmitter_rtp_funnel);
    gst_element_set_state (self->priv->transmitter_rtp_funnel, GST_STATE_NULL);
    gst_object_unref (self->priv->transmitter_rtp_funnel);
    self->priv->transmitter_rtp_funnel = NULL;
  }

  if (self->priv->transmitter_rtcp_funnel) {
    gst_bin_remove (GST_BIN (self->priv->conference),
      self->priv->transmitter_rtcp_funnel);
    gst_element_set_state (self->priv->transmitter_rtcp_funnel, GST_STATE_NULL);
    gst_object_unref (self->priv->transmitter_rtcp_funnel);
    self->priv->transmitter_rtcp_funnel = NULL;
  }

  if (self->priv->transmitters) {
    g_hash_table_foreach_remove (self->priv->transmitters, _remove_transmitter,
      self);

    g_hash_table_destroy (self->priv->transmitters);
    self->priv->transmitters = NULL;
  }

  if (self->priv->free_substreams) {
    g_list_foreach (self->priv->free_substreams, (GFunc) g_object_unref, NULL);
    g_list_free (self->priv->free_substreams);
    self->priv->free_substreams = NULL;
  }


  if (self->priv->blueprints) {
    fs_rtp_blueprints_unref (self->priv->media_type);
    self->priv->blueprints = NULL;
  }

  /* MAKE sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rtp_session_finalize (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  g_mutex_free (self->mutex);

  if (self->priv->local_codecs_configuration)
    fs_codec_list_destroy (self->priv->local_codecs_configuration);

  if (self->priv->local_codecs)
    fs_codec_list_destroy (self->priv->local_codecs);

  if (self->priv->local_codec_associations)
    g_hash_table_destroy (self->priv->local_codec_associations);

  if (self->priv->negotiated_codecs)
    fs_codec_list_destroy (self->priv->negotiated_codecs);

  if (self->priv->negotiated_codec_associations)
    g_hash_table_destroy (self->priv->negotiated_codec_associations);

  parent_class->finalize (object);
}

static void
fs_rtp_session_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  switch (prop_id) {
    case PROP_MEDIA_TYPE:
      g_value_set_enum (value, self->priv->media_type);
      break;
    case PROP_ID:
      g_value_set_uint (value, self->id);
      break;
    case PROP_SINK_PAD:
      g_value_set_object (value, self->priv->media_sink_pad);
      break;
    case PROP_LOCAL_CODECS:
      g_value_set_boxed (value, self->priv->local_codecs);
      break;
    case PROP_LOCAL_CODECS_CONFIG:
      g_value_set_boxed (value, self->priv->local_codecs_configuration);
      break;
    case PROP_NEGOTIATED_CODECS:
      FS_RTP_SESSION_LOCK (self);
      g_value_set_boxed (value, self->priv->negotiated_codecs);
      FS_RTP_SESSION_UNLOCK (self);
      break;
    case PROP_CONFERENCE:
      g_value_take_object (value, self->priv->conference);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rtp_session_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  switch (prop_id) {
    case PROP_MEDIA_TYPE:
      self->priv->media_type = g_value_get_enum (value);
      break;
    case PROP_ID:
      self->id = g_value_get_uint (value);
      break;
    case PROP_LOCAL_CODECS_CONFIG:
      if (self->priv->local_codecs) {
        GList *new_local_codecs_configuration = g_value_get_boxed (value);
        GList *new_local_codecs = NULL;
        GHashTable  *new_local_codec_associations = NULL;

        new_local_codecs_configuration =
          validate_codecs_configuration (
              self->priv->media_type, self->priv->blueprints,
              new_local_codecs_configuration);

        new_local_codec_associations = create_local_codec_associations (
            self->priv->media_type, self->priv->blueprints,
            self->priv->local_codecs_configuration,
            self->priv->local_codec_associations,
            &new_local_codecs);

        if (new_local_codecs && new_local_codec_associations) {
          fs_codec_list_destroy (self->priv->local_codecs);
          g_hash_table_destroy (self->priv->local_codec_associations);
          self->priv->local_codec_associations = new_local_codec_associations;
          self->priv->local_codecs = new_local_codecs;

          if (self->priv->local_codecs_configuration)
            fs_codec_list_destroy (self->priv->local_codecs_configuration);
          self->priv->local_codecs_configuration =
            new_local_codecs_configuration;

        } else {
          g_warning ("Invalid new codec configurations");
        }
      } else {
        if (self->priv->local_codecs_configuration)
          fs_codec_list_destroy (self->priv->local_codecs_configuration);
        self->priv->local_codecs_configuration = g_value_get_boxed (value);
      }
      break;
    case PROP_CONFERENCE:
      self->priv->conference = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_rtp_session_constructed (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION_CAST (object);
  GstElement *valve = NULL;
  GstElement *capsfilter = NULL;
  GstElement *tee = NULL;
  GstElement *funnel = NULL;
  GstElement *muxer = NULL;
  GstPad *valve_sink_pad = NULL;
  GstPad *funnel_src_pad = NULL;
  GstPad *muxer_src_pad = NULL;
  GstPadLinkReturn ret;
  gchar *tmp;

  if (self->id == 0) {
    g_error ("You can no instantiate this element directly, you MUST"
      " call fs_rtp_session_new()");
    return;
  }

  self->priv->blueprints = fs_rtp_blueprints_get (self->priv->media_type,
    &self->priv->construction_error);

  if (!self->priv->blueprints) {
    if (!self->priv->construction_error)
      self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_INTERNAL,
        "Unknown error while trying to discover codecs");
    return;
  }

  self->priv->local_codecs_configuration = validate_codecs_configuration (
      self->priv->media_type, self->priv->blueprints,
      self->priv->local_codecs_configuration);

  self->priv->local_codec_associations = create_local_codec_associations (
      self->priv->media_type, self->priv->blueprints,
      self->priv->local_codecs_configuration,
      NULL,
      &self->priv->local_codecs);

  if (!self->priv->local_codec_associations) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_INVALID_ARGUMENTS,
      "The passed codec preferences invalidate all blueprints");
    return;
  }

  tmp = g_strdup_printf ("valve_send_%d", self->id);
  valve = gst_element_factory_make ("fsvalve", tmp);
  g_free (tmp);

  if (!valve) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the fsvalve element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), valve)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the valve element to the FsRtpConference");
    gst_object_unref (valve);
    return;
  }

  g_object_set (G_OBJECT (valve), "drop", TRUE, NULL);
  gst_element_set_state (valve, GST_STATE_PLAYING);

  self->priv->media_sink_valve = gst_object_ref (valve);

  valve_sink_pad = gst_element_get_static_pad (valve, "sink");

  tmp = g_strdup_printf ("sink_%u", self->id);
  self->priv->media_sink_pad = gst_ghost_pad_new (tmp, valve_sink_pad);
  g_free (tmp);

  gst_pad_set_active (self->priv->media_sink_pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (self->priv->conference),
    self->priv->media_sink_pad);

  gst_object_unref (valve_sink_pad);


  /* Now create the transmitter RTP tee */

  tmp = g_strdup_printf ("send_rtp_tee_%d", self->id);
  tee = gst_element_factory_make ("tee", tmp);
  g_free (tmp);

  if (!tee) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtp tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), tee)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the rtp tee element to the FsRtpConference");
    gst_object_unref (tee);
    return;
  }

  gst_element_set_state (tee, GST_STATE_PLAYING);

  self->priv->transmitter_rtp_tee = gst_object_ref (tee);


  /* Now create the transmitter RTCP tee */

  tmp = g_strdup_printf ("send_rtcp_tee_%d", self->id);
  tee = gst_element_factory_make ("tee", tmp);
  g_free (tmp);

  if (!tee) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtcp tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), tee)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the rtcp tee element to the FsRtpConference");
    gst_object_unref (tee);
    return;
  }

  gst_element_set_state (tee, GST_STATE_PLAYING);

  self->priv->transmitter_rtcp_tee = gst_object_ref (tee);


  /* Now create the transmitter RTP funnel */

  tmp = g_strdup_printf ("recv_rtp_funnel_%d", self->id);
  funnel = gst_element_factory_make ("fsfunnel", tmp);
  g_free (tmp);

  if (!funnel) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtp funnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), funnel)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the rtp funnel element to the FsRtpConference");
    gst_object_unref (funnel);
    return;
  }

  self->priv->transmitter_rtp_funnel = gst_object_ref (funnel);

  tmp = g_strdup_printf ("recv_rtp_sink_%u", self->id);
  self->priv->rtpbin_recv_rtp_sink =
    gst_element_get_request_pad (self->priv->conference->gstrtpbin,
      tmp);
  g_free (tmp);

  funnel_src_pad = gst_element_get_static_pad (funnel, "src");

  ret = gst_pad_link (funnel_src_pad, self->priv->rtpbin_recv_rtp_sink);

  if (GST_PAD_LINK_FAILED (ret)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not link pad %s (%p) with pad %s (%p)",
      GST_PAD_NAME (funnel_src_pad), GST_PAD_CAPS (funnel_src_pad),
      GST_PAD_NAME (self->priv->rtpbin_recv_rtp_sink),
      GST_PAD_CAPS (self->priv->rtpbin_recv_rtp_sink));

    gst_object_unref (funnel_src_pad);
    return;
  }

  gst_object_unref (funnel_src_pad);

  gst_element_set_state (funnel, GST_STATE_PLAYING);


  /* Now create the transmitter RTCP funnel */

  tmp = g_strdup_printf ("recv_rtcp_funnel_%d", self->id);
  funnel = gst_element_factory_make ("fsfunnel", tmp);
  g_free (tmp);

  if (!funnel) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtcp funnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), funnel)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the rtcp funnel element to the FsRtcpConference");
    gst_object_unref (funnel);
    return;
  }

  self->priv->transmitter_rtcp_funnel = gst_object_ref (funnel);

  tmp = g_strdup_printf ("recv_rtcp_sink_%u", self->id);
  self->priv->rtpbin_recv_rtcp_sink =
    gst_element_get_request_pad (self->priv->conference->gstrtpbin,
      tmp);
  g_free (tmp);

  funnel_src_pad = gst_element_get_static_pad (funnel, "src");

  ret = gst_pad_link (funnel_src_pad, self->priv->rtpbin_recv_rtcp_sink);

  if (GST_PAD_LINK_FAILED (ret)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not link pad %s (%p) with pad %s (%p)",
      GST_PAD_NAME (funnel_src_pad), GST_PAD_CAPS (funnel_src_pad),
      GST_PAD_NAME (self->priv->rtpbin_recv_rtcp_sink),
      GST_PAD_CAPS (self->priv->rtpbin_recv_rtcp_sink));

    gst_object_unref (funnel_src_pad);
    return;
  }

  gst_object_unref (funnel_src_pad);

  gst_element_set_state (funnel, GST_STATE_PLAYING);

  /* Lets now create the RTP muxer */

  tmp = g_strdup_printf ("send_rtp_muxer_%d", self->id);
  muxer = gst_element_factory_make ("rtpmux", tmp);
  g_free (tmp);

  if (!muxer) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtp muxer element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), muxer)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the rtp muxer element to the FsRtpConference");
    gst_object_unref (muxer);
    return;
  }

  self->priv->rtpmuxer = gst_object_ref (muxer);

  tmp = g_strdup_printf ("send_rtp_sink_%u", self->id);
  self->priv->rtpbin_send_rtp_sink =
    gst_element_get_request_pad (self->priv->conference->gstrtpbin,
      tmp);
  g_free (tmp);

  muxer_src_pad = gst_element_get_static_pad (muxer, "src");

  ret = gst_pad_link (muxer_src_pad, self->priv->rtpbin_send_rtp_sink);

  if (GST_PAD_LINK_FAILED (ret)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not link pad %s (%p) with pad %s (%p)",
      GST_PAD_NAME (funnel_src_pad), GST_PAD_CAPS (muxer_src_pad),
      GST_PAD_NAME (self->priv->rtpbin_send_rtp_sink),
      GST_PAD_CAPS (self->priv->rtpbin_send_rtp_sink));

    gst_object_unref (muxer_src_pad);
    return;
  }

  gst_object_unref (muxer_src_pad);

  gst_element_set_state (muxer, GST_STATE_PLAYING);


  /* Lets now do the send_capsfilter */

  tmp = g_strdup_printf ("send_rtp_capsfilter_%d", self->id);
  capsfilter = gst_element_factory_make ("capsfilter", tmp);
  g_free (tmp);

  if (!capsfilter) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtp capsfilter element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), capsfilter)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the rtp capsfilter element to the FsRtpConference");
    gst_object_unref (capsfilter);
    return;
  }

  self->priv->send_capsfilter = gst_object_ref (capsfilter);

  if (!gst_element_link_pads (capsfilter, "src", muxer, NULL))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link pad capsfilter src pad to the rtpmux");
    return;
  }

  gst_element_set_state (capsfilter, GST_STATE_PLAYING);
}



static void
_remove_stream (gpointer user_data,
                 GObject *where_the_object_was)
{
  FsRtpSession *self = FS_RTP_SESSION (user_data);

  FS_RTP_SESSION_LOCK (self);
  self->priv->streams =
    g_list_remove_all (self->priv->streams, where_the_object_was);
  FS_RTP_SESSION_UNLOCK (self);
}

/**
 * fs_rtp_session_new_stream:
 * @session: an #FsRtpSession
 * @participant: #FsParticipant of a participant for the new stream
 * @direction: #FsStreamDirection describing the direction of the new stream that will
 * be created for this participant
 * @transmitter: Name of the type of transmitter to use for this session
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function creates a stream for the given participant into the active session.
 *
 * Returns: the new #FsStream that has been created. User must unref the
 * #FsStream when the stream is ended. If an error occured, returns NULL.
 */
static FsStream *
fs_rtp_session_new_stream (FsSession *session, FsParticipant *participant,
                           FsStreamDirection direction, gchar *transmitter,
                           guint n_parameters, GParameter *parameters,
                           GError **error)
{
  FsRtpSession *self = FS_RTP_SESSION (session);
  FsRtpParticipant *rtpparticipant = NULL;
  FsStream *new_stream = NULL;
  FsStreamTransmitter *st;

  if (!FS_IS_RTP_PARTICIPANT (participant)) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "You have to provide a participant of type RTP");
    return NULL;
  }
  rtpparticipant = FS_RTP_PARTICIPANT (participant);

  st = fs_rtp_session_get_new_stream_transmitter (self, transmitter,
    participant, n_parameters, parameters, error);

  if (!st)
    return NULL;

  new_stream = FS_STREAM_CAST (fs_rtp_stream_new (self, rtpparticipant,
      direction, st, error));

  FS_RTP_SESSION_LOCK (self);
  self->priv->streams = g_list_append (self->priv->streams, new_stream);
  FS_RTP_SESSION_UNLOCK (self);

  g_object_weak_ref (G_OBJECT (new_stream), _remove_stream, self);

  return new_stream;
}

/**
 * fs_rtp_session_start_telephony_event:
 * @session: an #FsRtpSession
 * @event: A #FsStreamDTMFEvent or another number defined at
 * http://www.iana.org/assignments/audio-telephone-event-registry
 * @volume: The volume in dBm0 without the negative sign. Should be between
 * 0 and 36. Higher values mean lower volume
 * @method: The method used to send the event
 *
 * This function will start sending a telephony event (such as a DTMF
 * tone) on the #FsRtpSession. You have to call the function
 * #fs_rtp_session_stop_telephony_event() to stop it. 
 * This function will use any available method, if you want to use a specific
 * method only, use #fs_rtp_session_start_telephony_event_full()
 *
 * Returns: %TRUE if sucessful, it can return %FALSE if the #FsStream
 * does not support this telephony event.
 */
static gboolean
fs_rtp_session_start_telephony_event (FsSession *session, guint8 event,
                                      guint8 volume, FsDTMFMethod method)
{
  return FALSE;
}

/**
 * fs_rtp_session_stop_telephony_event:
 * @session: an #FsRtpSession
 * @method: The method used to send the event
 *
 * This function will stop sending a telephony event started by
 * #fs_rtp_session_start_telephony_event(). If the event was being sent
 * for less than 50ms, it will be sent for 50ms minimum. If the
 * duration was a positive and the event is not over, it will cut it
 * short.
 *
 * Returns: %TRUE if sucessful, it can return %FALSE if the #FsRtpSession
 * does not support telephony events or if no telephony event is being sent
 */
static gboolean
fs_rtp_session_stop_telephony_event (FsSession *session, FsDTMFMethod method)
{
  return FALSE;
}

/**
 * fs_rtp_session_set_send_codec:
 * @session: an #FsRtpSession
 * @send_codec: an #FsCodec representing the codec to send
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will set the currently being sent codec for all streams in this
 * session. The given #FsCodec must be taken directly from the #negotiated-codecs
 * property of the session. If the given codec is not in the negotiated codecs
 * list, @error will be set and %FALSE will be returned. The @send_codec will be
 * copied so it must be free'd using fs_codec_destroy() when done.
 *
 * Returns: %FALSE if the send codec couldn't be set.
 */
static gboolean
fs_rtp_session_set_send_codec (FsSession *session, FsCodec *send_codec,
                               GError **error)
{
  GList *elem;
  FsRtpSession *self = FS_RTP_SESSION (session);
  gboolean ret = FALSE;

  FS_RTP_SESSION_LOCK (self);
  for (elem = g_list_first (self->priv->negotiated_codecs);
       elem;
       elem = g_list_next (elem))
    if (fs_codec_are_equal (elem->data, send_codec))
      break;

  if (elem)
  {
    if (self->priv->requested_send_codec)
      fs_codec_destroy (self->priv->requested_send_codec);

    self->priv->requested_send_codec = fs_codec_copy (send_codec);

    ret = fs_rtp_session_verify_send_codec_bin_locked (self, error);
  }
  else
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "The passed codec is not part of the list of negotiated codecs");
  }

  FS_RTP_SESSION_UNLOCK (self);

  return ret;
}

FsRtpSession *
fs_rtp_session_new (FsMediaType media_type, FsRtpConference *conference,
                    guint id, GError **error)
{
  FsRtpSession *session = g_object_new (FS_TYPE_RTP_SESSION,
    "media-type", media_type,
    "conference", conference,
    "id", id,
    NULL);

  if (session->priv->construction_error) {
    g_propagate_error (error, session->priv->construction_error);
    g_object_unref (session);
    return NULL;
  }

  return session;
}


GstCaps *
fs_rtp_session_request_pt_map (FsRtpSession *session, guint pt)
{
  GstCaps *caps = NULL;
  CodecAssociation *ca = NULL;

  FS_RTP_SESSION_LOCK (session);

  if (session->priv->negotiated_codec_associations) {
    ca = g_hash_table_lookup (session->priv->negotiated_codec_associations,
      GINT_TO_POINTER (pt));

    if (ca)
      caps = fs_codec_to_gst_caps (ca->codec);
  }

  FS_RTP_SESSION_UNLOCK (session);

  return caps;
}

/**
 * fs_rtp_session_link_network_sink:
 * @session: a #FsRtpSession
 * @src_pad: the new source pad from the #GstRtpBin
 *
 * Links a new source pad from the GstRtpBin to the transmitter tee
 */

void
fs_rtp_session_link_network_sink (FsRtpSession *session, GstPad *src_pad)
{
  GstPad *transmitter_rtp_tee_sink_pad;
  GstPad *transmitter_rtcp_tee_sink_pad;
  GstPadLinkReturn ret;
  gchar *tmp;

  transmitter_rtp_tee_sink_pad =
    gst_element_get_static_pad (session->priv->transmitter_rtp_tee, "sink");
  g_assert (transmitter_rtp_tee_sink_pad);

  ret = gst_pad_link (src_pad, transmitter_rtp_tee_sink_pad);

  if (GST_PAD_LINK_FAILED (ret)) {
    tmp = g_strdup_printf ("Could not link pad %s (%p) with pad %s (%p)",
      GST_PAD_NAME (src_pad), GST_PAD_CAPS (src_pad),
      GST_PAD_NAME (transmitter_rtp_tee_sink_pad),
      GST_PAD_CAPS (transmitter_rtp_tee_sink_pad));
    fs_session_emit_error (FS_SESSION (session), FS_ERROR_CONSTRUCTION,
      "Could not link rtpbin network src to tee", tmp);
    g_free (tmp);

    gst_object_unref (transmitter_rtp_tee_sink_pad);
    return;
  }

  gst_object_unref (transmitter_rtp_tee_sink_pad);


  transmitter_rtcp_tee_sink_pad =
    gst_element_get_static_pad (session->priv->transmitter_rtcp_tee, "sink");
  g_assert (transmitter_rtcp_tee_sink_pad);

  tmp = g_strdup_printf ("send_rtcp_src_%u", session->id);
  session->priv->rtpbin_send_rtcp_src =
    gst_element_get_request_pad (session->priv->conference->gstrtpbin, tmp);

  ret = gst_pad_link (session->priv->rtpbin_send_rtcp_src,
    transmitter_rtcp_tee_sink_pad);

  if (GST_PAD_LINK_FAILED (ret)) {
    tmp = g_strdup_printf ("Could not link pad %s (%p) with pad %s (%p)",
      GST_PAD_NAME (session->priv->rtpbin_send_rtcp_src),
      GST_PAD_CAPS (session->priv->rtpbin_send_rtcp_src),
      GST_PAD_NAME (transmitter_rtcp_tee_sink_pad),
      GST_PAD_CAPS (transmitter_rtcp_tee_sink_pad));
    fs_session_emit_error (FS_SESSION (session), FS_ERROR_CONSTRUCTION,
      "Could not link rtpbin network rtcp src to tee", tmp);
    g_free (tmp);

    gst_object_unref (transmitter_rtcp_tee_sink_pad);
    return;
  }

  gst_object_unref (transmitter_rtcp_tee_sink_pad);

}

static gboolean
_get_request_pad_and_link (GstElement *tee_funnel, const gchar *tee_funnel_name,
  GstElement *sinksrc, const gchar *sinksrc_padname, GstPadDirection direction,
  GError **error)
{
  GstPad *requestpad = NULL;
  GstPad *transpad = NULL;
  GstPadLinkReturn ret;
  gchar *requestpad_name = (direction == GST_PAD_SINK) ? "src%d" : "sink%d";

  /* The transmitter will only be removed when the whole session is disposed,
   * then the
   */
  requestpad = gst_element_get_request_pad (tee_funnel, requestpad_name);


  if (!requestpad) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Can not get the %s pad from the transmitter %s element",
      requestpad_name, tee_funnel_name);
    goto error;
  }

  transpad = gst_element_get_static_pad (sinksrc, sinksrc_padname);

  if (direction == GST_PAD_SINK)
    ret = gst_pad_link (requestpad, transpad);
  else
    ret = gst_pad_link (transpad, requestpad);

  gst_object_unref (transpad);

  if (GST_PAD_LINK_FAILED(ret)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Can not link the %s to the transmitter %s", tee_funnel_name,
      (direction == GST_PAD_SINK) ? "sink" : "src");
    goto error;
  }

  return TRUE;

 error:
  if (requestpad)
    gst_element_release_request_pad (tee_funnel, requestpad);

  return FALSE;
}

/**
 * fs_rtp_session_get_new_stream_transmitter:
 * @self: a #FsRtpSession
 * @transmitter_name: The name of the transmitter to create a stream for
 * @participant: The #FsRtpParticipant for this stream
 * @n_parameters: the number of parameters
 * @parameters: a table of n_parameters #GParameter structs
 *
 * This function will create a new #FsStreamTransmitter, possibly creating
 * and inserting into the pipeline its parent #FsTransmitter
 *
 * Returns: a newly allocated #FsStreamTransmitter
 */

static FsStreamTransmitter *
fs_rtp_session_get_new_stream_transmitter (FsRtpSession *self,
  gchar *transmitter_name, FsParticipant *participant, guint n_parameters,
  GParameter *parameters, GError **error)
{
  FsTransmitter *transmitter;
  GstElement *src, *sink;

  transmitter = g_hash_table_lookup (self->priv->transmitters,
    transmitter_name);

  if (transmitter) {
    return fs_transmitter_new_stream_transmitter (transmitter, participant,
      n_parameters, parameters, error);
  }

  transmitter = fs_transmitter_new (transmitter_name, 2, error);
  if (!transmitter)
    return NULL;

  g_object_get (transmitter, "gst-sink", &sink, "gst-src", &src, NULL);

  if(!gst_bin_add (GST_BIN (self->priv->conference), sink)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the transmitter sink for %s to the conference",
      transmitter_name);
    goto error;
  }

  if(!gst_bin_add (GST_BIN (self->priv->conference), src)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the transmitter src for %s to the conference",
      transmitter_name);
    goto error;
  }

  if(!_get_request_pad_and_link (self->priv->transmitter_rtp_tee,
      "rtp tee", sink, "sink1", GST_PAD_SINK, error))
    goto error;

  if(!_get_request_pad_and_link (self->priv->transmitter_rtcp_tee,
      "rtcp tee", sink, "sink2", GST_PAD_SINK, error))
    goto error;

  if(!_get_request_pad_and_link (self->priv->transmitter_rtp_funnel,
      "rtp funnel", src, "src1", GST_PAD_SRC, error))
    goto error;

  if(!_get_request_pad_and_link (self->priv->transmitter_rtcp_funnel,
      "rtcp funnel", src, "src2", GST_PAD_SRC, error))
    goto error;

  gst_element_sync_state_with_parent (src);
  gst_element_sync_state_with_parent (sink);

  g_hash_table_insert (self->priv->transmitters, g_strdup (transmitter_name),
    transmitter);

  gst_object_unref (src);
  gst_object_unref (sink);

  return fs_transmitter_new_stream_transmitter (transmitter, participant,
    n_parameters, parameters, error);

 error:
  if (src)
    gst_object_unref (src);
  if (sink)
    gst_object_unref (sink);
  if (transmitter)
    g_object_unref (transmitter);

  return NULL;
}


/**
 * fs_rtp_session_get_stream_by_ssrc_locked
 * @self: The #FsRtpSession
 * @stream_ssrc: The stream ssrc
 *
 * Gets the #FsRtpStream from a list of streams or NULL if it doesnt exist
 * You have to hold the GST_OBJECT_LOCK to call this function.
 *
 * Return value: A #FsRtpStream (unref after use) or NULL if it doesn't exist
 */
static FsRtpStream *
fs_rtp_session_get_stream_by_ssrc_locked (FsRtpSession *self,
    guint32 ssrc)
{
  GList *item = NULL;

  for (item = g_list_first (self->priv->streams);
       item;
       item = g_list_next (item))
    if (fs_rtp_stream_knows_ssrc_locked (item->data, ssrc))
      break;


  if (item)
    return FS_RTP_STREAM (gst_object_ref (item->data));
  else
    return NULL;
}

/**
 * fs_rtp_session_invalidate_pt:
 * @session: A #FsRtpSession
 * @pt: the PT to invalidate
 * @codec: the new codec
 *
 * Invalidates all codec bins for the selected payload type, because its
 * definition has changed
 */

static void
fs_rtp_session_invalidate_pt (FsRtpSession *session, gint pt,
    const FsCodec *codec)
{
  GList *item;
  FS_RTP_SESSION_LOCK (session);

  for (item = g_list_first (session->priv->free_substreams);
       item;
       item = g_list_next (item))
    fs_rtp_sub_stream_invalidate_codec_locked (item->data, pt, codec);


  for (item = g_list_first (session->priv->streams);
       item;
       item = g_list_next (item))
    fs_rtp_stream_invalidate_codec_locked (item->data, pt, codec);

  FS_RTP_SESSION_UNLOCK (session);
}

static gboolean
_compare_codec_lists (GList *list1, GList *list2)
{
  for (; list1 && list2;
       list1 = g_list_next (list1),
       list2 = g_list_next (list2)) {
    if (!fs_codec_are_equal (list1->data, list2->data))
      return FALSE;
  }

  if (list1 == NULL && list2 == NULL)
    return TRUE;
  else
    return FALSE;
}

/**
 * fs_rtp_session_negotiate_codecs:
 * @session: a #FsRtpSession
 * @remote_codecs: a #GList of #FsCodec
 *
 * Negotiates the codecs using the current (stored) codecs
 * and the new remote codecs.
 *
 * MT safe
 *
 * Returns: TRUE if the negotiation succeeds, FALSE otherwise
 */

gboolean
fs_rtp_session_negotiate_codecs (FsRtpSession *session, GList *remote_codecs,
  GError **error)
{
  gboolean has_many_streams;
  GHashTable *new_negotiated_codec_associations = NULL;;
  GList *new_negotiated_codecs = NULL;

  FS_RTP_SESSION_LOCK (session);

  has_many_streams =
    (g_list_next (g_list_first (session->priv->streams)) != NULL);

  new_negotiated_codec_associations = negotiate_codecs (remote_codecs,
    session->priv->negotiated_codec_associations,
    session->priv->local_codec_associations,
    session->priv->local_codecs,
    has_many_streams,
    &new_negotiated_codecs);

  if (new_negotiated_codec_associations) {
    gboolean is_new = FALSE;
    gboolean clear_pts = FALSE;
    int pt;

    is_new = _compare_codec_lists (session->priv->negotiated_codecs,
      new_negotiated_codecs);

    /* Lets remove the codec bin for any PT that has changed type */
    for (pt = 0; pt < 128; pt++) {
      CodecAssociation *old_codec_association = g_hash_table_lookup (
          session->priv->negotiated_codec_associations, GINT_TO_POINTER (pt));
      CodecAssociation *new_codec_association = g_hash_table_lookup (
          new_negotiated_codec_associations, GINT_TO_POINTER (pt));

      if (old_codec_association == NULL && new_codec_association == NULL)
        continue;

      if (old_codec_association == NULL || new_codec_association == NULL) {
        fs_rtp_session_invalidate_pt (session, pt, NULL);
        clear_pts = TRUE;
        continue;
      }

      if (!fs_codec_are_equal (old_codec_association->codec,
              new_codec_association->codec)) {
        fs_rtp_session_invalidate_pt (session, pt,
            new_codec_association->codec);
        clear_pts = TRUE;
        continue;
      }
    }

    if (clear_pts)
      g_signal_emit_by_name (session->priv->conference->gstrtpbin,
        "clear-pt-map");

    if (session->priv->negotiated_codec_associations)
      g_hash_table_destroy (session->priv->negotiated_codec_associations);
    if (session->priv->negotiated_codecs)
      fs_codec_list_destroy (session->priv->negotiated_codecs);

    session->priv->negotiated_codec_associations =
      new_negotiated_codec_associations;
    session->priv->negotiated_codecs = new_negotiated_codecs;

    if (!fs_rtp_session_verify_send_codec_bin_locked (session, error))
    {
      FS_RTP_SESSION_UNLOCK (session);
      return FALSE;
    }

    FS_RTP_SESSION_UNLOCK (session);

    if (is_new)
      g_signal_emit_by_name (session, "new-negotiated-codec");

    return TRUE;
  } else {
    FS_RTP_SESSION_UNLOCK (session);

    g_set_error (error, FS_ERROR, FS_ERROR_NEGOTIATION_FAILED,
      "There was no intersection between the remote codecs and the local ones");
    return FALSE;
  }
}


/**
 * fs_rtp_session_new_recv_pad:
 * @session: a #FsSession
 * @new_pad: the newly created pad
 * @ssrc: the ssrc for this new pad
 * @pt: the pt for this new pad
 *
 * This function is called by the #FsRtpConference when a new src pad appears.
 * It can will be called on the streaming thread.
 *
 * MT safe.
 */

void
fs_rtp_session_new_recv_pad (FsRtpSession *session, GstPad *new_pad,
  guint32 ssrc, guint pt)
{
  FsRtpSubStream *substream = NULL;
  FsRtpStream *stream = NULL;
  GError *error = NULL;

  substream = fs_rtp_sub_stream_new (session->priv->conference, session,
      new_pad, ssrc, pt, &error);

  if (substream == NULL) {
    if (error && error->domain == FS_ERROR)
      fs_session_emit_error (FS_SESSION (session), error->code,
        "Could not create a substream for the new pad", error->message);
    else
      fs_session_emit_error (FS_SESSION (session), FS_ERROR_CONSTRUCTION,
        "Could not create a substream for the new pad",
        "No error details returned");

    g_clear_error (&error);
    return;
  }

  if (!fs_rtp_sub_stream_add_codecbin (substream, &error)) {
    if (error)
      fs_session_emit_error (FS_SESSION (session), error->code,
          "Could not add the codec bin to the new substream", error->message);
    else
      fs_session_emit_error (FS_SESSION (session), FS_ERROR_CONSTRUCTION,
          "Could not add the codec bin to the new substream",
          "No error details returned");

    fs_rtp_sub_stream_block (substream, NULL, NULL);
  }

  g_clear_error (&error);

  /* Lets find the FsRtpStream for this substream, if no Stream claims it
   * then we just store it
   */

  FS_RTP_SESSION_LOCK (session);
  stream = fs_rtp_session_get_stream_by_ssrc_locked (session, ssrc);

  if (!stream)
    session->priv->free_substreams =
      g_list_prepend (session->priv->free_substreams, substream);
  FS_RTP_SESSION_UNLOCK (session);
  if (stream) {
    if (!fs_rtp_stream_add_substream (stream, substream, &error)) {
      fs_session_emit_error (FS_SESSION (session), error->code,
          "Could not add the output ghostpad to the new substream",
          error->message);
    }
    g_clear_error (&error);
    g_object_unref (stream);
  }
}



static gboolean
_g_object_has_property (GObject *object, const gchar *property)
{
 GObjectClass *klass;

  klass = G_OBJECT_GET_CLASS (object);
  return NULL != g_object_class_find_property (klass, property);
}


static gboolean
_create_ghost_pad (GstElement *current_element, const gchar *padname, GstElement
  *codec_bin, GError **error)
{
  GstPad *ghostpad;
  GstPad *pad = gst_element_get_static_pad (current_element, padname);
  gboolean ret = FALSE;

  if (!pad) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not find the %s on the element", padname);
      return FALSE;
  }

  ghostpad = gst_ghost_pad_new (padname, pad);
  if (!ghostpad) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not create a ghost pad for pad %s", padname);
    goto done;
  }

  if (!gst_pad_set_active (ghostpad, TRUE)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not active ghostpad %s", padname);
    gst_object_unref (ghostpad);
    goto done;
  }

  if (!gst_element_add_pad (codec_bin, ghostpad))
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add ghostpad %s to the codec bin", padname);

  ret = TRUE;
 done:
  gst_object_unref (pad);

  return ret;
}

/*
 * Builds a codec bin in the specified direction for the specified codec
 * using the specified blueprint
 */

static GstElement *
_create_codec_bin (CodecBlueprint *blueprint, const FsCodec *codec,
    const gchar *name, gboolean is_send, GError **error)
{
  GList *pipeline_factory = NULL;
  GList *walk = NULL;
  GstElement *codec_bin = NULL;
  GstElement *current_element = NULL;
  GstElement *previous_element = NULL;
  gchar *direction_str = (is_send == TRUE) ? "send" : "receive";

  if (is_send)
    pipeline_factory = blueprint->send_pipeline_factory;
  else
    pipeline_factory = blueprint->receive_pipeline_factory;

  g_debug ("creating %s codec bin for id %d, pipeline_factory %p",
    direction_str, codec->id, pipeline_factory);
  codec_bin = gst_bin_new (name);

  for (walk = g_list_first (pipeline_factory); walk; walk = g_list_next (walk))
  {
    if (g_list_next (g_list_first (walk->data))) {
      /* We have to check some kind of configuration to see if we have a
         favorite */
      current_element = gst_element_factory_make ("fsselector", NULL);

      if (!current_element) {
        g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not create fsselector element");
        goto error;
      }

      g_object_set (current_element, "factories", walk->data, NULL);
    } else {
      current_element =
        gst_element_factory_create (
            GST_ELEMENT_FACTORY (g_list_first (walk->data)->data), NULL);
      if (!current_element) {
        g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not create element for pt %d", codec->id);
        goto error;
      }
    }

    if (!gst_bin_add (GST_BIN (codec_bin), current_element)) {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add new element to %s codec_bin for pt %d",
        direction_str, codec->id);
      goto error;
    }

    /* TODO: Element options
     *  set_options_on_element (walk->data, current_element);
     */

    /* queue delay to 0 on all depayloaders until I remove that property
     * all-together */

    if (_g_object_has_property (G_OBJECT (current_element), "queue-delay"))
      g_object_set (G_OBJECT (current_element), "queue-delay", 0, NULL);

    if (_g_object_has_property (G_OBJECT (current_element), "pt"))
      g_object_set (current_element, "pt", codec->id,
        NULL);

    /* Lets create the ghost pads on the codec bin */

    if (g_list_first (pipeline_factory) == walk)
      /* if its the first element of the codec bin */
      if (!_create_ghost_pad (current_element, direction_str,
          codec_bin, error))
        goto error;

    if (g_list_next (g_list_first (pipeline_factory)) == NULL)
      /* if its the last element of the codec bin */
      if (!_create_ghost_pad (current_element, direction_str, codec_bin, error))
        goto error;


    /* let's link them together using the specified media_caps if any
     * this will ensure that multi-codec encoders/decoders will select the
     * appropriate codec based on caps negotiation */
    if (previous_element) {
      GstPad *sinkpad;
      GstPad *srcpad;
      GstPadLinkReturn ret;

      if (is_send)
        sinkpad = gst_element_get_static_pad (current_element, "sink");
      else
        sinkpad = gst_element_get_static_pad (previous_element, "sink");

      if (!sinkpad) {
        g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not get the sink pad one of the elements in the %s codec bin"
          " for pt %d", direction_str, codec->id);
        goto error;
      }


      if (is_send)
        srcpad = gst_element_get_static_pad (previous_element, "src");
      else
        srcpad = gst_element_get_static_pad (current_element, "src");

      if (!srcpad) {
        g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not get the src pad one of the elements in the %s codec bin"
          " for pt %d", direction_str, codec->id);
        gst_object_unref (sinkpad);
        goto error;
      }

      ret = gst_pad_link (srcpad, sinkpad);

      gst_object_unref (srcpad);
      gst_object_unref (sinkpad);

      if (GST_PAD_LINK_FAILED (ret)) {
        g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not link element inside the %s codec bin for pt %d",
          direction_str, codec->id);
        goto error;
      }
    }

    previous_element = current_element;
  }

  return codec_bin;

 error:
  gst_object_unref (codec_bin);
  return NULL;
}

/**
 * fs_rtp_session_new_recv_codec_bin:
 * @session: a #FsRtpSession
 * @ssrc: The SSRC that this codec bin will receive from
 * @pt: The payload type to create a codec bin for
 * @out_codec: The address where a newly-allocated copy of the #FsCodec
 *   this codec bin is for
 * @error: the location where a #GError can be stored (or NULL)
 *
 * This function will create a new reception codec bin for the specified codec
 *
 * MT safe.
 *
 * Returns: a newly-allocated codec bin
 */

GstElement *
fs_rtp_session_new_recv_codec_bin (FsRtpSession *session,
    guint32 ssrc,
    guint pt,
    FsCodec **out_codec,
    GError **error)
{
  GstElement *codec_bin = NULL;
  CodecAssociation *ca = NULL;
  CodecBlueprint *blueprint = NULL;
  FsCodec *codec = NULL;
  gchar *name;

  FS_RTP_SESSION_LOCK (session);

  if (!session->priv->negotiated_codec_associations) {
    FS_RTP_SESSION_UNLOCK (session);
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
      "No negotiated codecs yet");
      return NULL;
  }

  ca = g_hash_table_lookup (session->priv->negotiated_codec_associations,
    GINT_TO_POINTER (pt));

  if (ca) {
    /* We don't need to copy the blueprint because its static
     * as long as the session object exists */
    blueprint = ca->blueprint;
    codec = fs_codec_copy (ca->codec);
  }

  FS_RTP_SESSION_UNLOCK (session);

  if (!ca) {
    g_set_error (error, FS_ERROR, FS_ERROR_UNKNOWN_CODEC,
      "There is no negotiated codec with pt %d", pt);
    return NULL;
  }

  name = g_strdup_printf ("recv%d_%d", ssrc, pt);
  codec_bin = _create_codec_bin (blueprint, codec, name, FALSE, error);
  g_free (name);

  if (out_codec)
    *out_codec = codec;

  return codec_bin;
}


/**
 * fs_rtp_session_select_send_codec_locked:
 *
 * This function selects the codec to send using either the user preference
 * or the remote preference (from the negotiation).
 *
 * YOU must own the FsRtpSession mutex to call this function
 *
 * Returns: a newly-allocated #FsCodec
 */

static FsCodec *
fs_rtp_session_select_send_codec_locked (FsRtpSession *session,
    CodecBlueprint **blueprint,
    GError **error)
{
  FsCodec *codec = NULL;

  if (!session->priv->negotiated_codecs)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Tried to call fs_rtp_session_select_send_codec_bin before the codec"
        " negotiation has taken place");
    return NULL;
  }

  if (session->priv->requested_send_codec) {
    GList *elem = NULL;

    for (elem = g_list_first (session->priv->negotiated_codecs);
         elem;
         elem = g_list_next (elem))
      if (fs_codec_are_equal (elem->data, session->priv->requested_send_codec))
        break;

    if (elem)
    {
      codec = fs_codec_copy (session->priv->requested_send_codec);
    }
    else
    {
      /* The requested send codec no longer exists */
      fs_codec_destroy (session->priv->requested_send_codec);
      session->priv->requested_send_codec = NULL;
    }
  }

  if (codec == NULL)
    codec = fs_codec_copy (
        g_list_first (session->priv->negotiated_codecs)->data);

  if (blueprint)
  {
    CodecAssociation *codec_association = g_hash_table_lookup (
        session->priv->negotiated_codec_associations,
        GINT_TO_POINTER (codec->id));
    g_assert (codec_association);
    *blueprint = codec_association->blueprint;
  }

  FS_RTP_SESSION_UNLOCK (session);

  return codec;
}


/**
 * fs_rtp_session_add_send_codec_bin:
 * @session: a #FsRtpSession
 * @codec: a #FsCodec
 * @blueprint: the #CodecBlueprint to use
 *
 * This function creates, adds and links a codec bin for the current send remote
 * codec
 *
 * MT safe.
 *
 * Returns: The new codec bin (or NULL if there is an error)
 */

static GstElement *
fs_rtp_session_add_send_codec_bin (FsRtpSession *session,
    const FsCodec *codec,
    CodecBlueprint *blueprint,
    GError **error)
{
  GstElement *codecbin = NULL;
  gchar *name;
  GstCaps *sendcaps;

  name = g_strdup_printf ("send%d", codec->id);
  codecbin = _create_codec_bin (blueprint, codec, name, TRUE, error);
  g_free (name);

  if (!codecbin)
  {
    return NULL;
  }

  if (!gst_bin_add (GST_BIN (session->priv->conference), codecbin))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the send codec bin for pt %d to the pipeline",
        codec->id);
    gst_object_unref (codecbin);
    return NULL;
  }

  if (!gst_element_link_pads (session->priv->media_sink_valve, "src",
          codecbin, "sink"))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the send valve to the codec bin for pt %d", codec->id);
    goto error;
  }

  sendcaps = fs_codec_to_gst_caps (codec);

  g_object_set (G_OBJECT (session->priv->send_capsfilter),
      "caps", sendcaps, NULL);

  gst_caps_unref (sendcaps);

  if (!gst_element_link_pads (codecbin, "src",
          session->priv->send_capsfilter, "sink"))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the send codec bin for pt %d to the send capsfilter",
        codec->id);
    goto error;
  }

  if (!gst_element_sync_state_with_parent (codecbin)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not sync the state of the codec bin for pt %d with the state"
        " of the conference", codec->id);
    goto error;
  }

  g_object_set (session->priv->media_sink_valve, "drop", FALSE, NULL);

  return codecbin;

 error:
  gst_element_set_state (codecbin, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (session->priv->conference), codecbin);
  return NULL;
}

/**
 * _send_src_pad_have_data_callback:
 *
 * This is the pad probe callback on the sink pad of the valve.
 * It is used to replace the codec bin when the send codec has been changed.
 *
 * Its a callback, it returns TRUE to let the data through and FALSE to drop it
 * (See the "have-data" signal documentation of #GstPad).
 */

static gboolean
_send_src_pad_have_data_callback (GstPad *pad, GstMiniObject *miniobj,
    gpointer user_data)
{
  FsRtpSession *self = FS_RTP_SESSION (user_data);
  FsCodec *codec = NULL;
  CodecBlueprint *blueprint = NULL;
  GError *error = NULL;
  GstElement *codecbin = NULL;
  gboolean ret = TRUE;

  FS_RTP_SESSION_LOCK (self);
  codec = fs_rtp_session_select_send_codec_locked(self, &blueprint, &error);

  if (!codec)
  {
    fs_session_emit_error (FS_SESSION (self), error->code,
        "Could not select a new send codec", error->message);
    goto done;
  }

  g_clear_error (&error);

  if (fs_codec_are_equal (codec, self->priv->current_send_codec))
    goto done;


  if (gst_element_set_state (self->priv->send_codecbin, GST_STATE_NULL) !=
      GST_STATE_CHANGE_SUCCESS)
  {
    fs_session_emit_error (FS_SESSION (self), FS_ERROR_INTERNAL,
        "Could not stop the codec bin",
        "Setting the codec bin to NULL did not succeed" );
    goto done;
  }

  gst_bin_remove (GST_BIN (self->priv->conference), self->priv->send_codecbin);
  self->priv->send_codecbin = NULL;

  fs_codec_destroy (self->priv->current_send_codec);
  self->priv->current_send_codec = NULL;


  codecbin = fs_rtp_session_add_send_codec_bin (self, codec, blueprint,
      &error);

  if (codecbin)
  {
    self->priv->send_codecbin = codecbin;
    self->priv->current_send_codec = fs_codec_copy (codec);
  }
  else
  {
    fs_session_emit_error (FS_SESSION (self), error->code,
        "Could not build a new send codec bin", error->message);
  }

  g_clear_error (&error);

 done:
  if (codec)
  {
    if (GST_IS_BUFFER (miniobj)) {
      GstCaps *caps = fs_codec_to_gst_caps (codec);
      GstCaps *intersection = gst_caps_intersect (GST_BUFFER_CAPS (miniobj),
          caps);

      if (gst_caps_is_empty (intersection))
          ret = FALSE;
      gst_caps_unref (intersection);
      gst_caps_unref (caps);
    }
    fs_codec_destroy (codec);
  }

  if (ret && self->priv->send_blocking_id)
  {
    gst_pad_remove_data_probe (pad, self->priv->send_blocking_id);
    self->priv->send_blocking_id = 0;
  }

  FS_RTP_SESSION_UNLOCK (self);

  return ret;

}

/**
 * fs_rtp_session_verify_send_codec_bin_locked:
 *
 * Verify that the current send codec is still valid and if it is not
 * do whats required to have the right one be used.
 *
 * Must be called with the FsRtpSession lock taken
 *
 * Returns: TRUE if it succeeds, FALSE on an error
 */

static gboolean
fs_rtp_session_verify_send_codec_bin_locked (FsRtpSession *self, GError **error)
{
  FsCodec *codec = NULL;
  CodecBlueprint *blueprint = NULL;
  GstElement *codecbin = NULL;
  gboolean ret = FALSE;

  FS_RTP_SESSION_LOCK (self);
  codec = fs_rtp_session_select_send_codec_locked(self, &blueprint, error);

  if (!codec)
    goto done;

  if (self->priv->current_send_codec) {
    if (fs_codec_are_equal (codec, self->priv->current_send_codec))
    {
      ret = TRUE;
      goto done;
    }

    /* If we have to change an already made pipeline,
     * we have to make sure that is it blocked
     */

    if (!self->priv->send_blocking_id)
    {
      GstPad *pad;

      pad = gst_element_get_static_pad (self->priv->media_sink_valve, "src");

      self->priv->send_blocking_id = gst_pad_add_data_probe (pad,
          G_CALLBACK (_send_src_pad_have_data_callback), self);

      gst_object_unref (pad);
    }
  }
  else
  {
    /* The codec does exist yet, lets just create it */

    codecbin = fs_rtp_session_add_send_codec_bin (self, codec, blueprint,
        error);

    if (codecbin) {
      self->priv->send_codecbin = codecbin;
      self->priv->current_send_codec = codec;
    }
    else
    {
      /* We have an error !! */
      goto done;
    }
  }

  ret = TRUE;
 done:

  FS_RTP_SESSION_UNLOCK (self);

  return ret;
}



FsCodec *
fs_rtp_session_get_recv_codec_for_pt_locked (FsRtpSession *session,
    gint pt,
    GError **error)
{
  CodecAssociation *codec_association = g_hash_table_lookup (
      session->priv->negotiated_codec_associations, GINT_TO_POINTER (pt));
  g_assert (codec_association);

  return fs_codec_copy (codec_association->codec);
}


void
fs_rtp_session_associate_ssrc_cname (FsRtpSession *session,
    guint32 ssrc,
    gchar *cname)
{
  FsRtpStream *stream = NULL;
  FsRtpSubStream *substream = NULL;
  GList *item;
  GError *error = NULL;

  FS_RTP_SESSION_LOCK (session);
  for (item = g_list_first (session->priv->streams);
       item;
       item = g_list_next (item))
  {
    FsRtpStream *localstream = item->data;
    FsRtpParticipant *participant = NULL;
    gchar *localcname = NULL;

    g_object_get (localstream, "participant", &participant, NULL);
    g_object_get (participant, "cname", &localcname, NULL);
    g_object_unref (participant);

    if (!strcmp (localcname, cname))
    {
      stream = localstream;
      break;
    }
  }

  if (!stream) {
    gchar *str = g_strdup_printf ("There is no particpant with cname %s for"
        " ssrc %u", cname, ssrc);
    fs_session_emit_error (FS_SESSION (session),FS_ERROR_UNKNOWN_CNAME,
        str, str);
    g_free (str);
    goto done;
  }

  for (item = g_list_first (session->priv->free_substreams);
       item;
       item = g_list_next (item))
  {
    FsRtpSubStream *localsubstream = item->data;
    guint32 localssrc;

    g_object_get (localsubstream, "ssrc", &localssrc, NULL);
    if (ssrc == localssrc) {
      substream = localsubstream;
      break;
    }
  }

  if (!substream)
    goto done;

  if (!fs_rtp_stream_add_substream (stream, substream, &error))
    fs_session_emit_error (FS_SESSION (session), error->code,
        "Could not associate a substream with its stream",
        error->message);
  g_clear_error (&error);

 done:
  FS_RTP_SESSION_UNLOCK (session);
}

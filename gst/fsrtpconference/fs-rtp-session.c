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

  /* These lists are protected by the session mutex */
  GList *streams;
  GList *free_substreams;

  GList *blueprints;

  GList *local_codecs_configuration;

  GList *local_codecs;
  GHashTable *local_codec_associations;

  /* These are protected by the session mutex */
  GList *negotiated_codecs;
  GHashTable *negotiated_codec_associations;

  GError *construction_error;

  GMutex *mutex;

  gboolean disposed;
};

#define FS_RTP_SESSION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_SESSION, FsRtpSessionPrivate))

#define FS_SESSION_LOCK(session)   g_mutex_lock ((session)->priv->mutex)
#define FS_SESSION_UNLOCK(session) g_mutex_unlock ((session)->priv->mutex)

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


static FsStreamTransmitter *fs_rtp_session_get_new_stream_transmitter (
    FsRtpSession *self, gchar *transmitter_name, FsParticipant *participant,
    guint n_parameters, GParameter *parameters, GError **error);


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

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsRtpSession", &info, G_TYPE_FLAG_ABSTRACT);
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

  self->priv->mutex = g_mutex_new ();

  self->priv->media_type = FS_MEDIA_TYPE_LAST + 1;
}

static gboolean
_remove_transmitter (gpointer key, gpointer value, gpointer user_data)
{
  FsRtpSession *self = FS_RTP_SESSION (user_data);
  FsTransmitter *transmitter = FS_TRANSMITTER (value);
  GstElement *src, *sink;

  g_object_get (transmitter, "gst-sink", &sink, "gst-src", &src, NULL);

  gst_bin_remove (GST_BIN (self->priv->conference), src);
  gst_element_set_state (src, GST_STATE_NULL);

  gst_bin_remove (GST_BIN (self->priv->conference), sink);
  gst_element_set_state (sink, GST_STATE_NULL);

  gst_object_unref (src);
  gst_object_unref (sink);

  return TRUE;
}

static void
fs_rtp_session_dispose (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  if (self->priv->blueprints) {
    fs_rtp_blueprints_unref (self->priv->media_type);
    self->priv->blueprints = NULL;
  }

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

  /* MAKE sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rtp_session_finalize (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  g_mutex_free (self->priv->mutex);

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
      FS_SESSION_LOCK (self);
      g_value_set_boxed (value, self->priv->negotiated_codecs);
      FS_SESSION_UNLOCK (self);
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
  GstElement *tee = NULL;
  GstElement *funnel = NULL;
  GstElement *muxer = NULL;
  GstPad *valve_sink_pad = NULL;
  GstPad *funnel_src_pad = NULL;
  GstPad *muxer_src_pad = NULL;
  GstPadLinkReturn ret;
  gchar *tmp;

  G_OBJECT_CLASS (parent_class)->constructed (object);

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
  muxer = gst_element_factory_make ("rtpmuxer", tmp);
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
}



static void
_remove_stream (gpointer user_data,
                 GObject *where_the_object_was)
{
  FsRtpSession *self = FS_RTP_SESSION (user_data);

  FS_SESSION_LOCK (self);
  self->priv->streams =
    g_list_remove_all (self->priv->streams, where_the_object_was);
  FS_SESSION_UNLOCK (self);
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

  FS_SESSION_LOCK (self);
  self->priv->streams = g_list_append (self->priv->streams, new_stream);
  FS_SESSION_UNLOCK (self);

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
  return FALSE;
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

  FS_SESSION_LOCK (session);

  if (session->priv->negotiated_codec_associations) {
    ca = g_hash_table_lookup (session->priv->negotiated_codec_associations,
      GINT_TO_POINTER (pt));

    if (ca)
      caps = fs_codec_to_gst_caps (ca->codec);
  }

  FS_SESSION_UNLOCK (session);

  return caps;
}


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
                                            guint stream_ssrc)
{
  GList *item = NULL;

  for (item = g_list_first (self->priv->streams);
       item;
       item = g_list_next (item)) {
    FsRtpStream *stream = item->data;
    guint ssrc = 0;

    g_object_get (stream, "id", &ssrc, NULL);

    if (ssrc == stream_ssrc) {
      g_object_ref(stream);
      break;
    }
  }

  if (item)
    return FS_RTP_STREAM (item->data);
  else
    return NULL;
}

static void
fs_rtp_session_invalidate_pt (FsRtpSession *session, guint pt)
{
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

gboolean
fs_rtp_session_negotiate_codecs (FsRtpSession *session, GList *remote_codecs,
  GError **error)
{
  gboolean has_many_streams;
  GHashTable *new_negotiated_codec_associations = NULL;;
  GList *new_negotiated_codecs = NULL;

  FS_SESSION_LOCK (session);

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
      FsCodec *old_codec = g_hash_table_lookup (
          session->priv->negotiated_codec_associations, GINT_TO_POINTER (pt));
      FsCodec *new_codec = g_hash_table_lookup (
          new_negotiated_codec_associations, GINT_TO_POINTER (pt));

      if (old_codec == NULL && new_codec == NULL)
        continue;

      if (old_codec == NULL || new_codec == NULL) {
        fs_rtp_session_invalidate_pt (session, pt);
        clear_pts = TRUE;
        continue;
      }

      if (!fs_codec_are_equal (old_codec, new_codec)) {
        fs_rtp_session_invalidate_pt (session, pt);
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
    FS_SESSION_UNLOCK (session);

    if (is_new)
      g_signal_emit_by_name (session, "new-negotiated-codec");

    return TRUE;
  } else {
    FS_SESSION_UNLOCK (session);

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
 */

void
fs_rtp_session_new_recv_pad (FsRtpSession *session, GstPad *new_pad,
  guint32 ssrc, guint pt)
{
  FsRtpSubStream *substream = NULL;
  FsRtpStream *stream = NULL;
  GError *error = NULL;

  substream = fs_rtp_substream_new (session->priv->conference, new_pad,
    ssrc, pt, &error);

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


  /* Lets find the FsRtpStream for this substream, if no Stream claims it
   * then we just store it
   */

  FS_SESSION_LOCK (session);
  stream = fs_rtp_session_get_stream_by_ssrc_locked (session, ssrc);

  if (!stream)
    session->priv->free_substreams =
      g_list_prepend (session->priv->free_substreams, substream);
  FS_SESSION_UNLOCK (session);
  if (stream) {
    fs_rtp_stream_add_substream (stream, substream);
    g_object_unref (stream);
  }
}


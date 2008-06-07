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
#include "fs-rtp-special-source.h"
#include "fs-rtp-specific-nego.h"

#define GST_CAT_DEFAULT fsrtpconference_debug

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
  PROP_CODECS_READY,
  PROP_CONFERENCE,
  PROP_NO_RTCP_TIMEOUT
};

#define DEFAULT_NO_RTCP_TIMEOUT (7000)

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
  GstElement *send_tee;
  GstElement *send_capsfilter;
  GstElement *transmitter_rtp_tee;
  GstElement *transmitter_rtcp_tee;
  GstElement *transmitter_rtp_funnel;
  GstElement *transmitter_rtcp_funnel;
  GstElement *transmitter_rtp_fakesink;
  GstElement *transmitter_rtcp_fakesink;

  GstElement *rtpmuxer;

  /* Request pads that are disposed of when the tee is disposed of */
  GstPad *send_tee_media_pad;
  GstPad *send_tee_discovery_pad;

  /* We dont keep explicit references to the pads, the Bin does that for us
   * only this element's methods can add/remote it
   */
  GstPad *media_sink_pad;


  /* The discovery elements are only created when codec parameter discovery is
   * under progress.
   * They are normally destroyed when the caps are found but may be destroyed
   * by the dispose function too, we hold refs to them
   */
  GstElement *discovery_fakesink;
  GstElement *discovery_capsfilter;
  GstElement *discovery_codecbin;
  FsCodec *discovery_codec;
  gulong discovery_blocking_id;

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

  /* These are protected by the session mutex */
  GList *codec_associations;

  /* Protected by the session mutex */
  gint no_rtcp_timeout;

  GList *extra_sources;

  GError *construction_error;

  gboolean disposed;
};

G_DEFINE_TYPE (FsRtpSession, fs_rtp_session, FS_TYPE_SESSION);

#define FS_RTP_SESSION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_SESSION, FsRtpSessionPrivate))

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
    const gchar *transmitter,
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
static gboolean fs_rtp_session_set_local_codecs_config (FsSession *session,
    GList *local_codecs_config,
    GError **error);
static gboolean fs_rtp_session_verify_send_codec_bin_locked (
    FsRtpSession *self,
    GError **error);
static void fs_rtp_session_send_codec_changed (FsRtpSession *self);

static void _substream_no_rtcp_timedout_cb (FsRtpSubStream *substream,
    FsRtpSession *session);
static void _substream_blocked (FsRtpSubStream *substream, FsRtpStream *stream,
    FsRtpSession *session);

static GError* _stream_new_remote_codecs (FsRtpStream *stream, GList *codecs,
    FsRtpSession *session);


static FsStreamTransmitter *fs_rtp_session_get_new_stream_transmitter (
    FsRtpSession *self,
    const gchar *transmitter_name,
    FsParticipant *participant,
    guint n_parameters,
    GParameter *parameters,
    GError **error);

static gboolean fs_rtp_session_substream_add_codec_bin (FsRtpSession *session,
    FsRtpSubStream *substream,
    guint32 ssrc,
    guint pt,
    GError **error);

static void _remove_stream (gpointer user_data,
    GObject *where_the_object_was);

static gboolean
fs_rtp_session_update_codecs (FsRtpSession *session,
    FsRtpStream *stream,
    GList *remote_codecs,
    GError **error);

static void
fs_rtp_session_start_codec_param_gathering (FsRtpSession *session);
static void
fs_rtp_session_stop_codec_param_gathering (FsRtpSession *session);


static GObjectClass *parent_class = NULL;

//static guint signals[LAST_SIGNAL] = { 0 };

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
  session_class->set_local_codecs_config =
    fs_rtp_session_set_local_codecs_config;

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
  g_object_class_override_property (gobject_class,
    PROP_CODECS_READY, "codecs-ready");

  g_object_class_install_property (gobject_class,
    PROP_CONFERENCE,
    g_param_spec_object ("conference",
      "The Conference this stream refers to",
      "This is a convience pointer for the Conference",
      FS_TYPE_RTP_CONFERENCE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_NO_RTCP_TIMEOUT,
      g_param_spec_int ("no-rtcp-timeout",
          "The timeout (in ms) before no RTCP is assumed",
          "This is the time (in ms) after which data received without RTCP"
          " is attached the FsStream, this only works if there is only one"
          " FsStream. -1 will wait forever. 0 will not wait for RTCP and"
          " attach it immediataly to the FsStream and prohibit the creation"
          " of a second FsStream",
          -1, G_MAXINT, DEFAULT_NO_RTCP_TIMEOUT,
          G_PARAM_READWRITE));

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

  g_static_rec_mutex_init (&self->mutex);

  self->priv->media_type = FS_MEDIA_TYPE_LAST + 1;

  self->priv->no_rtcp_timeout = DEFAULT_NO_RTCP_TIMEOUT;
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

  gst_element_set_locked_state (elem, TRUE);
  gst_element_set_state (elem, GST_STATE_NULL);
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
fs_rtp_session_dispose (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);
  GList *item = NULL;
  GstBin *conferencebin = NULL;

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  conferencebin = GST_BIN (self->priv->conference);

  /* Lets stop all of the elements sink to source */

  /* First the send pipeline */
  if (self->priv->transmitters)
    g_hash_table_foreach (self->priv->transmitters, _stop_transmitter_elem,
      "gst-sink");

  stop_and_remove (conferencebin, &self->priv->transmitter_rtp_fakesink, TRUE);
  stop_and_remove (conferencebin, &self->priv->transmitter_rtcp_fakesink, TRUE);
  stop_and_remove (conferencebin, &self->priv->transmitter_rtp_tee, TRUE);
  stop_and_remove (conferencebin, &self->priv->transmitter_rtcp_tee, TRUE);

  if (self->priv->rtpbin_send_rtcp_src)
    gst_pad_set_active (self->priv->rtpbin_send_rtcp_src, FALSE);
  if (self->priv->rtpbin_send_rtp_sink)
    gst_pad_set_active (self->priv->rtpbin_send_rtp_sink, FALSE);

  fs_rtp_session_stop_codec_param_gathering (self);

  stop_and_remove (conferencebin, &self->priv->rtpmuxer, TRUE);
  stop_and_remove (conferencebin, &self->priv->send_capsfilter, TRUE);
  stop_and_remove (conferencebin, &self->priv->send_codecbin, FALSE);
  stop_and_remove (conferencebin, &self->priv->send_tee, TRUE);
  stop_and_remove (conferencebin, &self->priv->media_sink_valve, TRUE);

  if (self->priv->media_sink_pad)
    gst_pad_set_active (self->priv->media_sink_pad, FALSE);


  /* Now the recv pipeline */
  if (self->priv->free_substreams)
    g_list_foreach (self->priv->free_substreams, (GFunc) fs_rtp_sub_stream_stop,
      NULL);
  if (self->priv->rtpbin_recv_rtp_sink)
    gst_pad_set_active (self->priv->rtpbin_recv_rtp_sink, FALSE);
  if (self->priv->rtpbin_recv_rtcp_sink)
    gst_pad_set_active (self->priv->rtpbin_recv_rtcp_sink, FALSE);

  stop_and_remove (conferencebin, &self->priv->transmitter_rtp_funnel, TRUE);
  stop_and_remove (conferencebin, &self->priv->transmitter_rtcp_funnel, TRUE);

  if (self->priv->transmitters)
    g_hash_table_foreach (self->priv->transmitters, _stop_transmitter_elem,
      "gst-src");

  self->priv->extra_sources =
    fs_rtp_special_sources_destroy (self->priv->extra_sources);

  /* Now they should all be stopped, we can remove them in peace */


  if (self->priv->media_sink_pad) {
    gst_pad_set_active (self->priv->media_sink_pad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (self->priv->conference),
      self->priv->media_sink_pad);
    self->priv->media_sink_pad = NULL;
  }


  if (self->priv->rtpbin_send_rtcp_src) {
    gst_pad_set_active (self->priv->rtpbin_send_rtcp_src, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_send_rtcp_src);
    gst_object_unref (self->priv->rtpbin_send_rtcp_src);
    self->priv->rtpbin_send_rtcp_src = NULL;
  }

  if (self->priv->rtpbin_send_rtp_sink) {
    gst_pad_set_active (self->priv->rtpbin_send_rtp_sink, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_send_rtp_sink);
    gst_object_unref (self->priv->rtpbin_send_rtp_sink);
    self->priv->rtpbin_send_rtp_sink = NULL;
  }

  if (self->priv->rtpbin_recv_rtp_sink) {
    gst_pad_set_active (self->priv->rtpbin_recv_rtp_sink, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_recv_rtp_sink);
    gst_object_unref (self->priv->rtpbin_recv_rtp_sink);
    self->priv->rtpbin_recv_rtp_sink = NULL;
  }

  if (self->priv->rtpbin_recv_rtcp_sink) {
    gst_pad_set_active (self->priv->rtpbin_recv_rtcp_sink, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_recv_rtcp_sink);
    gst_object_unref (self->priv->rtpbin_recv_rtcp_sink);
    self->priv->rtpbin_recv_rtcp_sink = NULL;
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

  if (self->priv->conference)
  {
    g_object_unref (self->priv->conference);
    self->priv->conference = NULL;
  }

  for (item = g_list_first (self->priv->streams);
       item;
       item = g_list_next (item))
    g_object_weak_unref (G_OBJECT (item->data), _remove_stream, self);
  g_list_free (self->priv->streams);
  self->priv->streams = NULL;

  self->priv->disposed = TRUE;


  FS_RTP_SESSION_UNLOCK (self);

  /* MAKE sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rtp_session_finalize (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  g_static_rec_mutex_free (&self->mutex);

  if (self->priv->local_codecs_configuration)
    fs_codec_list_destroy (self->priv->local_codecs_configuration);

  if (self->priv->codec_associations)
    codec_association_list_destroy (self->priv->codec_associations);

  if (self->priv->current_send_codec)
    fs_codec_destroy (self->priv->current_send_codec);

  if (self->priv->requested_send_codec)
    fs_codec_destroy (self->priv->requested_send_codec);

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
      {
        GList *local_codecs = NULL;
        GList *local_codec_associations = NULL;

        FS_RTP_SESSION_LOCK (self);
        local_codec_associations = create_local_codec_associations (
            self->priv->blueprints,
            self->priv->local_codecs_configuration,
            self->priv->codec_associations);
        local_codecs = codec_associations_to_codecs (local_codec_associations);
        codec_association_list_destroy (local_codec_associations);
        FS_RTP_SESSION_UNLOCK (self);

        g_value_take_boxed (value, local_codecs);
      }
      break;
    case PROP_LOCAL_CODECS_CONFIG:
      g_value_set_boxed (value, self->priv->local_codecs_configuration);
      break;
    case PROP_NEGOTIATED_CODECS:
      {
        GList *negotiated_codecs = NULL;
        FS_RTP_SESSION_LOCK (self);
        negotiated_codecs = codec_associations_to_codecs (
            self->priv->codec_associations);
        FS_RTP_SESSION_UNLOCK (self);
        g_value_take_boxed (value, negotiated_codecs);
      }
      break;
    case PROP_CODECS_READY:
      {
        GList *item = NULL;

        FS_RTP_SESSION_LOCK (self);
        for (item = g_list_first (self->priv->codec_associations);
             item;
             item = g_list_next (item))
        {
          CodecAssociation *ca = item->data;
          if (!ca->disable && ca->need_config)
            break;
        }
        FS_RTP_SESSION_UNLOCK (self);

        g_value_set_boolean (value, item == NULL);
      }
      break;
    case PROP_CONFERENCE:
      g_value_set_object (value, self->priv->conference);
      break;
    case PROP_CURRENT_SEND_CODEC:
      FS_RTP_SESSION_LOCK (self);
      g_value_set_boxed (value, self->priv->current_send_codec);
      FS_RTP_SESSION_UNLOCK (self);
      break;
    case PROP_NO_RTCP_TIMEOUT:
      FS_RTP_SESSION_LOCK (self);
      g_value_set_int (value, self->priv->no_rtcp_timeout);
      FS_RTP_SESSION_UNLOCK (self);
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
    case PROP_CONFERENCE:
      self->priv->conference = FS_RTP_CONFERENCE (g_value_dup_object (value));
      break;
    case PROP_NO_RTCP_TIMEOUT:
      FS_RTP_SESSION_LOCK (self);
      self->priv->no_rtcp_timeout = g_value_get_int (value);
      FS_RTP_SESSION_UNLOCK (self);
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
  GstElement *fakesink = NULL;
  GstPad *valve_sink_pad = NULL;
  GstPad *funnel_src_pad = NULL;
  GstPad *muxer_src_pad = NULL;
  GstPad *transmitter_rtcp_tee_sink_pad;
  GstPad *pad1, *pad2;
  GstPadLinkReturn ret;
  gchar *tmp;

  if (self->id == 0) {
    g_error ("You can no instantiate this element directly, you MUST"
      " call fs_rtp_session_new ()");
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

  /* Create an initial list of local codec associations */
  self->priv->codec_associations = create_local_codec_associations (
      self->priv->blueprints, NULL, NULL);

  if (!self->priv->codec_associations)
  {
    self->priv->construction_error = g_error_new (FS_ERROR, FS_ERROR_INTERNAL,
        "Unable to create initial codec associations"
        " from the discovered codecs");
    return;
  }

  tmp = g_strdup_printf ("valve_send_%u", self->id);
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


  tmp = g_strdup_printf ("send_tee_%u", self->id);
  tee = gst_element_factory_make ("tee", tmp);
  g_free (tmp);

  if (!tee) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), tee)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the tee element to the FsRtpConference");
    gst_object_unref (tee);
    return;
  }

  self->priv->send_tee = gst_object_ref (tee);

  if (!gst_element_link_pads (valve, "src", tee, "sink"))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not ");
    return;
  }

  self->priv->send_tee_discovery_pad = gst_element_get_request_pad (tee,
      "src%d");
  self->priv->send_tee_media_pad = gst_element_get_request_pad (tee,
      "src%d");

  /* Now create the transmitter RTP funnel */

  tmp = g_strdup_printf ("recv_rtp_funnel_%u", self->id);
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

  tmp = g_strdup_printf ("recv_rtcp_funnel_%u", self->id);
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

  tmp = g_strdup_printf ("send_rtp_muxer_%u", self->id);
  muxer = gst_element_factory_make ("rtpdtmfmux", tmp);
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
      GST_PAD_NAME (muxer_src_pad), GST_PAD_CAPS (muxer_src_pad),
      GST_PAD_NAME (self->priv->rtpbin_send_rtp_sink),
      GST_PAD_CAPS (self->priv->rtpbin_send_rtp_sink));

    gst_object_unref (muxer_src_pad);
    return;
  }

  gst_object_unref (muxer_src_pad);

  gst_element_set_state (muxer, GST_STATE_PLAYING);


  /* Now create the transmitter RTP tee */

  tmp = g_strdup_printf ("send_rtp_tee_%u", self->id);
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

  tmp = g_strdup_printf ("send_rtp_src_%u", self->id);
  if (!gst_element_link_pads (
          self->priv->conference->gstrtpbin, tmp,
          self->priv->transmitter_rtp_tee, "sink")) {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link rtpbin %s pad to tee sink", tmp);
    g_free (tmp);
    return;
  }
  g_free (tmp);

  /* Now add a fakesink to the RTP tee */

  tmp = g_strdup_printf ("sink_rtp_fakesink_%u", self->id);
  fakesink = gst_element_factory_make ("fakesink", tmp);
  g_free (tmp);

  if (!fakesink) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtp fakesink element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), fakesink)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the rtp fakesink element to the FsRtpConference");
    gst_object_unref (tee);
    return;
  }

  g_object_set (fakesink, "sync", FALSE, "async", FALSE, NULL);

  gst_element_set_state (fakesink, GST_STATE_PLAYING);

  self->priv->transmitter_rtp_fakesink = gst_object_ref (fakesink);

  pad1 = gst_element_get_request_pad (tee, "src%d");
  pad2 = gst_element_get_static_pad (fakesink, "sink");

  ret = gst_pad_link (pad1, pad2);

  gst_object_unref (pad2);
  gst_object_unref (pad1);

  if (GST_PAD_LINK_FAILED (ret)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
        "Could not link the rtp tee to its fakesink");
    return;
  }

  /* Now create the transmitter RTCP tee */

  tmp = g_strdup_printf ("send_rtcp_tee_%u", self->id);
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

  tmp = g_strdup_printf ("send_rtcp_src_%u", self->id);
  self->priv->rtpbin_send_rtcp_src =
    gst_element_get_request_pad (self->priv->conference->gstrtpbin, tmp);

  if (!self->priv->rtpbin_send_rtcp_src)
  {
     self->priv->construction_error = g_error_new (FS_ERROR,
         FS_ERROR_CONSTRUCTION,
         "Could not get %s request pad from the gstrtpbin", tmp);
    g_free (tmp);
    return;
  }
  g_free (tmp);


  transmitter_rtcp_tee_sink_pad =
    gst_element_get_static_pad (self->priv->transmitter_rtcp_tee, "sink");
  g_assert (transmitter_rtcp_tee_sink_pad);

  ret = gst_pad_link (self->priv->rtpbin_send_rtcp_src,
    transmitter_rtcp_tee_sink_pad);

  gst_object_unref (transmitter_rtcp_tee_sink_pad);

  if (GST_PAD_LINK_FAILED (ret))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link rtpbin network rtcp src to tee");
    return;
  }

  /* Now add a fakesink to the RTCP tee */

  tmp = g_strdup_printf ("sink_rtcp_fakesink_%u", self->id);
  fakesink = gst_element_factory_make ("fakesink", tmp);
  g_free (tmp);

  if (!fakesink) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtcp fakesink element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), fakesink)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the rtcp fakesink element to the FsRtcpConference");
    gst_object_unref (tee);
    return;
  }

  g_object_set (fakesink, "sync", FALSE, "async", FALSE, NULL);

  gst_element_set_state (fakesink, GST_STATE_PLAYING);

  self->priv->transmitter_rtcp_fakesink = gst_object_ref (fakesink);

  pad1 = gst_element_get_request_pad (tee, "src%d");
  pad2 = gst_element_get_static_pad (fakesink, "sink");

  ret = gst_pad_link (pad1, pad2);

  gst_object_unref (pad2);
  gst_object_unref (pad1);

  if (GST_PAD_LINK_FAILED (ret)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
        "Could not link the rtcp tee to its fakesink");
    return;
  }

  /* Lets now do the send_capsfilter */

  tmp = g_strdup_printf ("send_rtp_capsfilter_%u", self->id);
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

  fs_rtp_session_start_codec_param_gathering (self);

  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
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
fs_rtp_session_new_stream (FsSession *session,
    FsParticipant *participant,
    FsStreamDirection direction,
    const gchar *transmitter,
    guint n_parameters,
    GParameter *parameters,
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

  g_signal_connect (new_stream, "new-remote-codecs",
      G_CALLBACK (_stream_new_remote_codecs), self);

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
 * #fs_rtp_session_stop_telephony_event () to stop it.
 * This function will use any available method, if you want to use a specific
 * method only, use #fs_rtp_session_start_telephony_event_full ()
 *
 * Returns: %TRUE if sucessful, it can return %FALSE if the #FsStream
 * does not support this telephony event.
 */
static gboolean
fs_rtp_session_start_telephony_event (FsSession *session, guint8 event,
                                      guint8 volume, FsDTMFMethod method)
{
  FsRtpSession *self = FS_RTP_SESSION (session);
  gboolean ret = FALSE;

  FS_RTP_SESSION_LOCK (self);
  ret = fs_rtp_special_sources_start_telephony_event (
      self->priv->extra_sources, event, volume, method);
  FS_RTP_SESSION_UNLOCK (self);

  return ret;
}

/**
 * fs_rtp_session_stop_telephony_event:
 * @session: an #FsRtpSession
 * @method: The method used to send the event
 *
 * This function will stop sending a telephony event started by
 * #fs_rtp_session_start_telephony_event (). If the event was being sent
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
  FsRtpSession *self = FS_RTP_SESSION (session);
  gboolean ret = FALSE;

  FS_RTP_SESSION_LOCK (self);
  ret = fs_rtp_special_sources_stop_telephony_event (
      self->priv->extra_sources, method);
  FS_RTP_SESSION_UNLOCK (self);

  return ret;
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
 * copied so it must be free'd using fs_codec_destroy () when done.
 *
 * Returns: %FALSE if the send codec couldn't be set.
 */
static gboolean
fs_rtp_session_set_send_codec (FsSession *session, FsCodec *send_codec,
                               GError **error)
{
  FsRtpSession *self = FS_RTP_SESSION (session);
  gboolean ret = FALSE;

  FS_RTP_SESSION_LOCK (self);

  if (lookup_codec_association_by_codec (
          self->priv->codec_associations, send_codec))
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

static gboolean
fs_rtp_session_set_local_codecs_config (FsSession *session,
    GList *local_codecs_config,
    GError **error)
{
  FsRtpSession *self = FS_RTP_SESSION (session);
  GList *old_codec_configs = NULL;
  GList *new_local_codecs_configuration =
    fs_codec_list_copy (local_codecs_config);
  gboolean ret;

  new_local_codecs_configuration =
    validate_codecs_configuration (
        self->priv->media_type, self->priv->blueprints,
        new_local_codecs_configuration);

  if (new_local_codecs_configuration == NULL)
    GST_DEBUG ("None of the local codecs configuration passed are usable,"
        " this will restore the original list of detected codecs");

  FS_RTP_SESSION_LOCK (self);

  old_codec_configs = self->priv->local_codecs_configuration;

  self->priv->local_codecs_configuration = new_local_codecs_configuration;

  ret = fs_rtp_session_update_codecs (self, NULL, NULL, error);
  if (ret)
  {
    fs_codec_list_destroy (old_codec_configs);

    g_object_notify ((GObject*) self, "local-codecs");
    g_object_notify ((GObject*) self, "local-codecs-config");

    gst_element_post_message (GST_ELEMENT (self->priv->conference),
        gst_message_new_element (GST_OBJECT (self->priv->conference),
            gst_structure_new ("farsight-codecs-changed",
                "session", FS_TYPE_SESSION, self,
                NULL)));

  }
  else
  {
    fs_codec_list_destroy (new_local_codecs_configuration);
    self->priv->local_codecs_configuration = old_codec_configs;
    GST_WARNING ("Invalid new codec configurations");
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

  ca = lookup_codec_association_by_pt (
      session->priv->codec_associations, pt);

  if (ca)
    caps = fs_codec_to_gst_caps (ca->codec);

  FS_RTP_SESSION_UNLOCK (session);

  if (!caps)
    GST_WARNING ("Could not get caps for payload type %u in session %d",
        pt, session->id);

  return caps;
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
    return FALSE;
  }

  transpad = gst_element_get_static_pad (sinksrc, sinksrc_padname);

  if (direction == GST_PAD_SINK)
    ret = gst_pad_link (requestpad, transpad);
  else
    ret = gst_pad_link (transpad, requestpad);

  gst_object_unref (requestpad);
  gst_object_unref (transpad);

  if (GST_PAD_LINK_FAILED (ret)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Can not link the %s to the transmitter %s", tee_funnel_name,
      (direction == GST_PAD_SINK) ? "sink" : "src");
    return FALSE;
  }

  return TRUE;
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
  const gchar *transmitter_name, FsParticipant *participant, guint n_parameters,
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

  if (!gst_bin_add (GST_BIN (self->priv->conference), sink)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the transmitter sink for %s to the conference",
      transmitter_name);
    goto error;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), src)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the transmitter src for %s to the conference",
      transmitter_name);
    goto error;
  }

  if (!_get_request_pad_and_link (self->priv->transmitter_rtp_tee,
      "rtp tee", sink, "sink1", GST_PAD_SINK, error))
    goto error;

  if (!_get_request_pad_and_link (self->priv->transmitter_rtcp_tee,
      "rtcp tee", sink, "sink2", GST_PAD_SINK, error))
    goto error;

  if (!_get_request_pad_and_link (self->priv->transmitter_rtp_funnel,
      "rtp funnel", src, "src1", GST_PAD_SRC, error))
    goto error;

  if (!_get_request_pad_and_link (self->priv->transmitter_rtcp_funnel,
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
 *
 * Return value: A #FsRtpStream (unref after use) or NULL if it doesn't exist
 */
static FsRtpStream *
fs_rtp_session_get_stream_by_ssrc (FsRtpSession *self,
    guint32 ssrc)
{
  GList *item = NULL;
  FsRtpStream *stream = NULL;

  FS_RTP_SESSION_LOCK (self);

  for (item = g_list_first (self->priv->streams);
       item;
       item = g_list_next (item))
    if (fs_rtp_stream_knows_ssrc_locked (item->data, ssrc))
      break;

  if (item)
    stream = FS_RTP_STREAM (gst_object_ref (item->data));

  FS_RTP_SESSION_UNLOCK (self);

  return stream;
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

/**
 * fs_rtp_session_negotiate_codecs:
 * @session: a #FsRtpSession
 * @stream: The #FsRtpStream to which the new remote codecs belong
 * @remote_codecs: The #GList of remote codecs to use for that stream
 *
 * Negotiates the codecs using the current (stored) codecs
 * and the remote codecs from each stream.
 * If a stream is specified, it will use the specified remote codecs
 * instead of the ones currently in the stream
 *
 * MT safe
 *
 * Returns: the newly negotiated codec associations or %NULL on error
 */

static GList *
fs_rtp_session_negotiate_codecs (FsRtpSession *session,
    FsRtpStream *stream,
    GList *remote_codecs,
    gboolean *has_remotes,
    GError **error)
{
  gint streams_with_codecs = 0;
  gboolean has_many_streams = FALSE;
  GList *new_negotiated_codec_associations = NULL;
  GList *item;

  *has_remotes = FALSE;

  FS_RTP_SESSION_LOCK (session);

  for (item = g_list_first (session->priv->streams);
       item;
       item = g_list_next (item))
  {
    FsRtpStream *mystream = item->data;
    if (mystream == stream)
    {
      if (remote_codecs)
        streams_with_codecs ++;
    }
    else if (mystream->remote_codecs)
    {
      streams_with_codecs ++;
    }
  }

  if (streams_with_codecs >= 2)
    has_many_streams = TRUE;

  new_negotiated_codec_associations = create_local_codec_associations (
      session->priv->blueprints, session->priv->local_codecs_configuration,
      session->priv->codec_associations);

  if (!new_negotiated_codec_associations)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_NO_CODECS,
        "Codec config would leave no valid local codecs");
    goto error;
  }

  for (item = g_list_first (session->priv->streams);
       item;
       item = g_list_next (item))
  {
    FsRtpStream *mystream = item->data;
    GList *codecs = NULL;

    if (mystream == stream)
      codecs = remote_codecs;
    else
      codecs = mystream->remote_codecs;

    if (codecs)
    {
      GList *tmp_codec_associations = NULL;

      *has_remotes = TRUE;

      tmp_codec_associations = negotiate_stream_codecs (codecs,
          new_negotiated_codec_associations, has_many_streams);

      codec_association_list_destroy (new_negotiated_codec_associations);
      new_negotiated_codec_associations = tmp_codec_associations;

      if (!new_negotiated_codec_associations)
        break;
    }
  }

  if (!new_negotiated_codec_associations)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_NEGOTIATION_FAILED,
        "There was no intersection between the remote codecs"
        " and the local ones");
    goto error;
  }

  new_negotiated_codec_associations = finish_codec_negotiation (
      session->priv->codec_associations,
      new_negotiated_codec_associations);


  FS_RTP_SESSION_UNLOCK (session);

  return new_negotiated_codec_associations;

 error:

  FS_RTP_SESSION_UNLOCK (session);

  return NULL;;
}



/**
 * fs_rtp_session_update_codecs:
 * @session: a #FsRtpSession
 * @stream: The #FsRtpStream to which the new remote codecs belong
 * @remote_codecs: The #GList of remote codecs to use for that stream
 *
 * Negotiates the codecs using the current (stored) codecs
 * and the remote codecs from each stream.
 * If a stream is specified, it will use the specified remote codecs
 * instead of the ones currently in the stream
 *
 * MT safe
 *
 * Returns: TRUE if the negotiation succeeds, FALSE otherwise
 */

static gboolean
fs_rtp_session_update_codecs (FsRtpSession *session,
    FsRtpStream *stream,
    GList *remote_codecs,
    GError **error)
{
  GList *new_negotiated_codec_associations = NULL;
  gboolean is_new = TRUE;
  GList *old_negotiated_codec_associations;
  gboolean has_remotes = FALSE;

  FS_RTP_SESSION_LOCK (session);

  old_negotiated_codec_associations =
    session->priv->codec_associations;

  new_negotiated_codec_associations = fs_rtp_session_negotiate_codecs (
      session, stream, remote_codecs, &has_remotes, error);

  if (!new_negotiated_codec_associations)
  {
    FS_RTP_SESSION_UNLOCK (session);
    return FALSE;
  }

  session->priv->codec_associations = new_negotiated_codec_associations;

  if (old_negotiated_codec_associations)
  {
    gboolean clear_pts = FALSE;
    int pt;

    is_new = !codec_associations_list_are_equal (
        old_negotiated_codec_associations,
        new_negotiated_codec_associations);

    /* Lets remove the codec bin for any PT that has changed type */
    for (pt = 0; pt < 128; pt++) {
      CodecAssociation *old_codec_association =
        lookup_codec_association_by_pt (
            old_negotiated_codec_associations, pt);
      CodecAssociation *new_codec_association =
        lookup_codec_association_by_pt (
            new_negotiated_codec_associations, pt);

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
  }

  if (old_negotiated_codec_associations)
    codec_association_list_destroy (old_negotiated_codec_associations);

  fs_rtp_session_start_codec_param_gathering (session);

  if (has_remotes)
    if (!fs_rtp_session_verify_send_codec_bin_locked (session, error))
    {
      FS_RTP_SESSION_UNLOCK (session);
      return FALSE;
    }

  FS_RTP_SESSION_UNLOCK (session);

  if (is_new)
  {
    if (has_remotes)
      g_object_notify (G_OBJECT (session), "negotiated-codecs");

    gst_element_post_message (GST_ELEMENT (session->priv->conference),
        gst_message_new_element (GST_OBJECT (session->priv->conference),
            gst_structure_new ("farsight-codecs-changed",
                "session", FS_TYPE_SESSION, session,
                NULL)));
  }

  return TRUE;
}

static GError *
_stream_new_remote_codecs (FsRtpStream *stream,
    GList *codecs,
    FsRtpSession *session)
{
  GError *error = NULL;
  gboolean rv;

  rv = fs_rtp_session_update_codecs (session, stream, codecs, &error);

  if (!rv && !error)
    error = g_error_new (FS_ERROR, FS_ERROR_INTERNAL,
        "Unknown error while negotiating codecs");

  return error;
}


static void
_substream_error (FsRtpSubStream *substream,
    gint errorno,
    gchar *error_msg,
    gchar *debug_msg,
    gpointer user_data)
{
  FsSession *session = FS_SESSION (user_data);

  fs_session_emit_error (session, errorno, error_msg, debug_msg);
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
  gint no_rtcp_timeout;

  FS_RTP_SESSION_LOCK (session);
  no_rtcp_timeout = session->priv->no_rtcp_timeout;
  FS_RTP_SESSION_UNLOCK (session);

  substream = fs_rtp_sub_stream_new (session->priv->conference, session,
      new_pad, ssrc, pt, no_rtcp_timeout, &error);

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

  g_signal_connect (substream, "blocked", G_CALLBACK (_substream_blocked),
      session);

  if (!fs_rtp_session_substream_add_codec_bin (session, substream, ssrc, pt,
          &error))
  {
    if (error)
      fs_session_emit_error (FS_SESSION (session), error->code,
          "Could not add the codec bin to the new substream", error->message);
    else
      fs_session_emit_error (FS_SESSION (session), FS_ERROR_CONSTRUCTION,
          "Could not add the codec bin to the new substream",
          "No error details returned");
  }

  g_clear_error (&error);

  /* Lets find the FsRtpStream for this substream, if no Stream claims it
   * then we just store it
   */

  FS_RTP_SESSION_LOCK (session);
  stream = fs_rtp_session_get_stream_by_ssrc (session, ssrc);

  if (stream)
    GST_DEBUG ("Already have a stream with SSRC %x, using it", ssrc);

  /* Add the substream directly if the no_rtcp_timeout is 0 and
   * there is only one stream */
  if (!stream)
  {
    if (no_rtcp_timeout == 0 &&
        g_list_length (session->priv->streams) == 1)
    {
      stream = g_object_ref (g_list_first (session->priv->streams)->data);
      GST_DEBUG ("No RTCP timeout and only one stream, giving it substream"
          " for SSRC %x in session %u", ssrc, session->id);
    }
    else
    {
      session->priv->free_substreams =
        g_list_prepend (session->priv->free_substreams, substream);

      g_signal_connect (substream, "error", G_CALLBACK (_substream_error),
          session);

      if (no_rtcp_timeout > 0)
      {
        g_signal_connect (substream, "no-rtcp-timedout",
            G_CALLBACK (_substream_no_rtcp_timedout_cb), session);
        GST_DEBUG ("No stream for SSRC %x, waiting for %d ms before associating"
            "in session %u", ssrc, no_rtcp_timeout, session->id);
      }
      else if (no_rtcp_timeout < 0)
      {
        GST_DEBUG ("No RTCP timeout is < 0, we will wait forever for an"
            " RTCP SDES to arrive for SSRC %x in session %u",
            ssrc, session->id);
      }
      else
      {
        GST_WARNING ("No RTCP timeout is 0, but there is more than one stream,"
            " we will wait forever for an RTCP SDES to arrive for SSRC %u in"
            " session %u", ssrc, session->id);
      }
    }
  }

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
      "Could not find the %s pad on the element", padname);
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

  if (!pipeline_factory)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_UNKNOWN_CODEC,
        "The %s codec %s does not have a pipeline,"
        " its probably a special codec",
        fs_media_type_to_string (codec->media_type),
        codec->encoding_name);
    return NULL;
  }


  GST_DEBUG ("creating %s codec bin for id %d, pipeline_factory %p",
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

    /* queue delay to 0 on all depayloaders until I remove that property
     * all-together */

    if (_g_object_has_property (G_OBJECT (current_element), "queue-delay"))
      g_object_set (G_OBJECT (current_element), "queue-delay", 0, NULL);

    if (_g_object_has_property (G_OBJECT (current_element), "pt"))
      g_object_set (current_element, "pt", codec->id,
        NULL);

    /* Lets create the ghost pads on the codec bin */

    if (g_list_previous (walk) == NULL)
      /* if its the first element of the codec bin */
      if (!_create_ghost_pad (current_element,
              is_send ? "src" : "sink", codec_bin, error))
        goto error;

    if (g_list_next (walk) == NULL)
      /* if its the last element of the codec bin */
      if (!_create_ghost_pad (current_element,
              is_send ? "sink" : "src" , codec_bin, error))
        goto error;


    /* let's link them together using the specified media_caps if any
     * this will ensure that multi-codec encoders/decoders will select the
     * appropriate codec based on caps negotiation */
    if (previous_element) {
      GstPad *sinkpad;
      GstPad *srcpad;
      GstPadLinkReturn ret;

      if (is_send)
        sinkpad = gst_element_get_static_pad (previous_element, "sink");
      else
        sinkpad = gst_element_get_static_pad (current_element, "sink");

      if (!sinkpad) {
        g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not get the sink pad one of the elements in the %s codec bin"
          " for pt %d", direction_str, codec->id);
        goto error;
      }


      if (is_send)
        srcpad = gst_element_get_static_pad (current_element, "src");
      else
        srcpad = gst_element_get_static_pad (previous_element, "src");

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
 * fs_rtp_session_get_recv_codec_locked:
 * @session: a #FsRtpSession
 * @pt: The payload type to find the codec for
 * @stream: an optional #FsRtpStream for which this data is received
 * @bp: Then returned CodecBlueprint to create a codecbin
 *
 * This function returns the codec and blueprint that will be used to receive
 * data on a specific payload type, optionally from a specific stream.
 *
 * Returns: A new #FsCodec or %NULL on error
 */

static FsCodec *
fs_rtp_session_get_recv_codec_locked (FsRtpSession *session,
    guint pt,
    FsRtpStream *stream,
    CodecBlueprint **bp,
    GError **error)
{
  FsCodec *recv_codec = NULL;
  CodecAssociation *ca = NULL;
  GList *item = NULL;


  if (!session->priv->codec_associations)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "No negotiated codecs yet");
    return NULL;
  }

  ca = lookup_codec_association_by_pt (session->priv->codec_associations, pt);

  if (!ca)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_UNKNOWN_CODEC,
      "There is no negotiated codec with pt %d", pt);
    return NULL;
  }

  recv_codec = codec_copy_without_config (ca->codec);

  if (stream)
  {
    GList *remote_codecs = NULL;
    FsCodec *remote_codec = NULL;

    g_object_get (stream, "remote-codecs", &remote_codecs, NULL);


    for (item = remote_codecs; item; item = g_list_next (item))
    {
      FsCodec *tmpcodec = NULL;
      remote_codec = item->data;

      tmpcodec = sdp_is_compat (ca->codec, remote_codec);
      if (tmpcodec)
      {
        fs_codec_destroy (tmpcodec);
        break;
      }
    }

    if (item == NULL)
      remote_codec = NULL;

    if (remote_codec)
    {
      for (item = remote_codec->optional_params; item;
           item = g_list_next (item))
      {
        FsCodecParameter *param = item->data;
        if (codec_has_config_data_named (recv_codec, param->name))
          fs_codec_add_optional_parameter (recv_codec, param->name,
              param->value);
      }
    }

    fs_codec_list_destroy (remote_codecs);
  }

  *bp = ca->blueprint;

  return recv_codec;
}

/**
 * fs_rtp_session_substream_add_codec_bin:
 * @session: a #FsRtpSession
 * @substream: a #FsRtpSubStream
 * @ssrc: the ssrc of the substream
 * @pt: the payload type of the substream
 * @error: location of a #GError, or NULL if no error occured
 *
 * Adds a codecbin to a substream according to the currently negotiated codecs
 *
 * Returns: %TRUE on success, %FALSE on error
 */

static gboolean
fs_rtp_session_substream_add_codec_bin (FsRtpSession *session,
    FsRtpSubStream *substream,
    guint32 ssrc,
    guint pt,
    GError **error)
{
  gboolean ret = FALSE;
  GstElement *codecbin = NULL;
  gchar *name;
  FsRtpStream *stream = NULL;
  FsCodec *current_codec = NULL;
  FsCodec *new_codec = NULL;
  CodecBlueprint *bp = NULL;

  FS_RTP_SESSION_LOCK (session);

  g_object_get (substream,
      "codec", &current_codec,
      "stream", &stream,
      NULL);

  new_codec = fs_rtp_session_get_recv_codec_locked (session, pt, stream, &bp,
      error);

  if (!new_codec)
    goto out;

  if (fs_codec_are_equal_including_config (new_codec, current_codec))
  {
    ret = TRUE;
    goto out;
  }

  name = g_strdup_printf ("recv%u_%d", ssrc, pt);
  codecbin = _create_codec_bin (bp, new_codec, name, FALSE, error);
  g_free (name);

  if (!codecbin)
    goto out;

  ret = fs_rtp_sub_stream_set_codecbin (substream, new_codec, codecbin, error);

 out:
  if (stream)
    g_object_unref (stream);

  fs_codec_destroy (new_codec);
  fs_codec_destroy (current_codec);

  FS_RTP_SESSION_UNLOCK (session);

  return ret;
}


/**
 * fs_rtp_session_select_send_codec_locked:
 * @session: the #FsRtpSession
 * @blueprint: a pointer where the current #CodecBlueprint can be stored
 *
 * This function selects the codec to send using either the user preference
 * or the remote preference (from the negotiation).
 *
 * YOU must own the FsRtpSession mutex to call this function
 *
 * Returns: a pointer to a #CodecAssociation
 */

static CodecAssociation *
fs_rtp_session_select_send_codec_locked (FsRtpSession *session,
    GError **error)
{
  CodecAssociation *ca = NULL;
  GList *ca_e = NULL;

  if (!session->priv->codec_associations)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Tried to call fs_rtp_session_select_send_codec_bin before the codec"
        " negotiation has taken place");
    return NULL;
  }

  if (session->priv->requested_send_codec) {
    ca = lookup_codec_association_by_codec (
        session->priv->codec_associations,
        session->priv->requested_send_codec);
    if (ca)
    {
      if (!codec_association_is_valid_for_sending (ca))
      {
        fs_codec_destroy (session->priv->requested_send_codec);
        session->priv->requested_send_codec = NULL;
        ca = NULL;
        GST_DEBUG_OBJECT (session->priv->conference,
            "The current requested codec is not a valid main send codec,"
            " ignoring");
      }
      else
      {
        goto out;
      }
    }
    else
    {
      /* The requested send codec no longer exists */
      fs_codec_destroy (session->priv->requested_send_codec);
      session->priv->requested_send_codec = NULL;

      GST_WARNING_OBJECT (session->priv->conference,
          "The current requested codec no longer exists, resetting");
    }
  }

  for (ca_e = g_list_first (session->priv->codec_associations);
       ca_e;
       ca_e = g_list_next (ca_e))
  {
    if (codec_association_is_valid_for_sending (ca_e->data))
    {
      ca = ca_e->data;
      break;
    }
  }

  if (ca == NULL)
    g_set_error (error, FS_ERROR, FS_ERROR_NEGOTIATION_FAILED,
        "Could not get a valid send codec");

out:

  return ca;
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
  GstPad *pad = NULL;

  name = g_strdup_printf ("send_%d_%d", session->id, codec->id);
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

  pad = gst_element_get_static_pad (codecbin, "sink");
  if (!pad)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not get the sink for the send codec bin");
    gst_object_unref (codecbin);
    return NULL;
  }

  if (GST_PAD_LINK_FAILED (gst_pad_link (session->priv->send_tee_media_pad,
              pad)))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link media tee pad ");

    gst_object_unref (codecbin);
    gst_object_unref (pad);

    return NULL;
  }

  gst_object_unref (pad);

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

  session->priv->send_codecbin = codecbin;
  session->priv->current_send_codec = fs_codec_copy (codec);

  fs_rtp_session_send_codec_changed (session);

  return codecbin;

 error:
  gst_element_set_state (codecbin, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (session->priv->conference), codecbin);
  return NULL;
}

/**
 * fs_rtp_session_send_codec_changed:
 * @self: The #FsRtpSession that changed its codec
 *
 * Call this function when the value of the #FsSession:current-send-codec
 * changes
 */

static void
fs_rtp_session_send_codec_changed (FsRtpSession *self)
{
  FsCodec *codec = NULL;

  FS_RTP_SESSION_LOCK (self);
  codec = fs_codec_copy (self->priv->current_send_codec);
  FS_RTP_SESSION_UNLOCK (self);

  g_object_notify (G_OBJECT (self), "current-send-codec");

  gst_element_post_message (GST_ELEMENT (self->priv->conference),
      gst_message_new_element (GST_OBJECT (self->priv->conference),
          gst_structure_new ("farsight-current-send-codec-changed",
              "session", FS_TYPE_SESSION, self,
              "codec", FS_TYPE_CODEC, codec,
              NULL)));

  fs_codec_destroy (codec);
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
  CodecAssociation *ca = NULL;
  GError *error = NULL;
  GstElement *codecbin = NULL;
  gboolean ret = TRUE;

  FS_RTP_SESSION_LOCK (self);
  ca = fs_rtp_session_select_send_codec_locked (self, &error);

  if (!ca)
  {
    fs_session_emit_error (FS_SESSION (self), error->code,
        "Could not select a new send codec", error->message);
    goto done;
  }

  g_clear_error (&error);

  if (fs_codec_are_equal (ca->codec, self->priv->current_send_codec))
    goto done;


  gst_element_set_locked_state (self->priv->send_codecbin, TRUE);
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


  codecbin = fs_rtp_session_add_send_codec_bin (self, ca->codec, ca->blueprint,
      &error);

  if (!codecbin)
  {
    fs_session_emit_error (FS_SESSION (self), error->code,
        "Could not build a new send codec bin", error->message);
  }

  g_clear_error (&error);

 done:
  /* If we have a codec bin, the required/preferred caps may have changed,
   * in this case, we need to drop the current buffer and wait for a buffer
   * with the right caps to come in. Only then can we drop the pad probe
   */

  if (codecbin)
  {
    if (GST_IS_BUFFER (miniobj)) {
      GstPad *codecbin_sink_pad = gst_pad_get_peer (pad);

      if (!gst_pad_accept_caps (codecbin_sink_pad, GST_BUFFER_CAPS (miniobj))) {
        ret = FALSE;
        GST_WARNING ("Dropping buffer because its caps do not match the"
            " requirements of the new send codec bin");
      }
      gst_object_unref (codecbin_sink_pad);
    }
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
  GstElement *codecbin = NULL;
  GError *local_gerror = NULL;
  CodecAssociation *ca = NULL;

  ca = fs_rtp_session_select_send_codec_locked (self, error);
  if (!ca)
    return FALSE;

  self->priv->extra_sources = fs_rtp_special_sources_update (
      self->priv->extra_sources,
      self->priv->codec_associations, ca->codec,
      GST_ELEMENT (self->priv->conference),
      self->priv->rtpmuxer, error);
  if (local_gerror)
  {
    g_propagate_error (error, local_gerror);
    return FALSE;
  }

  if (self->priv->current_send_codec) {
    if (fs_codec_are_equal (ca->codec, self->priv->current_send_codec))
      return TRUE;

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

    codecbin = fs_rtp_session_add_send_codec_bin (self, ca->codec,
        ca->blueprint,
        error);

    if (!codecbin)
    {
      /* We have an error !! */
      return FALSE;
    }
  }

  return TRUE;
}

/*
 * This callback is called when the pad of a substream has been locked because
 * the codec needs to be changed. It will see if there is a new codec to be set,
 * if there is, it will change the codec bin.
 */

static void
_substream_blocked (FsRtpSubStream *substream, FsRtpStream *stream,
    FsRtpSession *session)
{
  GError *error = NULL;
  gint pt;
  guint32 ssrc;

  g_object_get (substream,
      "pt", &pt,
      "ssrc", &ssrc,
      NULL);

  if (!fs_rtp_session_substream_add_codec_bin (session, substream, ssrc, pt,
          &error))
  {
    gchar *str = g_strdup_printf ("Could not add the new recv codec bin for"
        " ssrc %u and payload type %d to the state NULL", ssrc, pt);

    if (stream)
      fs_stream_emit_error (FS_STREAM (stream), FS_ERROR_CONSTRUCTION,
          "Could not add the new recv codec bin", error->message);
    else
      fs_session_emit_error (FS_SESSION (session), FS_ERROR_CONSTRUCTION,
          "Could not add the new recv codec bin", error->message);
    g_free (str);
    goto done;
  }

 done:

  g_clear_error (&error);
}


void
fs_rtp_session_associate_ssrc_cname (FsRtpSession *session,
    guint32 ssrc,
    const gchar *cname)
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

    g_assert (localcname);

    if (!strcmp (localcname, cname))
    {
      stream = localstream;
      g_free (localcname);
      break;
    }
    g_free (localcname);
  }

  if (!stream) {
    gchar *str = g_strdup_printf ("There is no particpant with cname %s for"
        " ssrc %u", cname, ssrc);
    fs_session_emit_error (FS_SESSION (session), FS_ERROR_UNKNOWN_CNAME,
        str, str);
    g_free (str);
    FS_RTP_SESSION_UNLOCK (session);
    return;
  }

  fs_rtp_stream_add_known_ssrc (stream, ssrc);

  for (item = g_list_first (session->priv->free_substreams);
       item;
       item = g_list_next (item))
  {
    FsRtpSubStream *localsubstream = item->data;
    guint32 localssrc;

    g_object_get (localsubstream, "ssrc", &localssrc, NULL);
    GST_LOG ("Have substream with ssrc %x, looking for %x", localssrc, ssrc);
    if (ssrc == localssrc) {
      substream = localsubstream;
      session->priv->free_substreams = g_list_delete_link (
          session->priv->free_substreams, item);
      break;
    }
  }
  FS_RTP_SESSION_UNLOCK (session);

  if (!substream)
  {
    GST_DEBUG ("No free substream with SSRC %x in session %u",
        ssrc, session->id);
    return;
  }

  while (
      g_signal_handlers_disconnect_by_func (substream, "error", session) > 0) {}
  while (
      g_signal_handlers_disconnect_by_func (substream, "no-rtcp-timedout", session) > 0) {}

  if (!fs_rtp_stream_add_substream (stream, substream, &error))
    fs_session_emit_error (FS_SESSION (session), error->code,
        "Could not associate a substream with its stream",
        error->message);
  g_clear_error (&error);

  GST_DEBUG ("Associated SSRC %x to session %u", ssrc, session->id);
}

static void
_substream_no_rtcp_timedout_cb (FsRtpSubStream *substream,
    FsRtpSession *session)
{
  GError *error = NULL;

  FS_RTP_SESSION_LOCK (session);

  if (g_list_length (session->priv->streams) != 1)
  {
    guint ssrc, pt;
    gint timeout;
    g_object_get (substream,
        "ssrc", &ssrc,
        "pt", &pt,
        "no-rtcp-timeout", &timeout,
        NULL);
    GST_WARNING ("The substream for SSRC %x and pt %u did not receive RTCP"
        " for %d milliseconds, but we have more than one stream so we can"
        " not associate it.", ssrc, pt, timeout);
    goto done;
  }

  if (!g_list_find (session->priv->free_substreams, substream))
  {
    GST_WARNING ("Could not find substream %p in the list of free substreams",
        substream);
    goto done;
  }

  session->priv->free_substreams =
    g_list_remove (session->priv->free_substreams,
        substream);

  while (
      g_signal_handlers_disconnect_by_func (substream, "error", session) > 0) {}
  while (
      g_signal_handlers_disconnect_by_func (substream, "no-rtcp-timedout", session) > 0) {}

  if (!fs_rtp_stream_add_substream (
          g_list_first (session->priv->streams)->data,
          substream, &error))
  {
    fs_session_emit_error (FS_SESSION (session),
        error ? error->code : FS_ERROR_INTERNAL,
        "Could not link the substream to a stream",
        error ? error->message : "No error message");
  }
  g_clear_error (&error);


 done:

  FS_RTP_SESSION_UNLOCK (session);
}

/**
 * fs_rtp_session_bye_ssrc:
 * @session: a #FsRtpSession
 * @ssrc: The ssrc
 *
 * This function is called when a RTCP BYE is received
 */
void
fs_rtp_session_bye_ssrc (FsRtpSession *session,
    guint32 ssrc)
{
  GList *item;

  /* First remove it from the known SSRCs */

  FS_RTP_SESSION_LOCK (session);

  for (item = g_list_first (session->priv->streams);
       item;
       item = g_list_next (item))
  {
    FsRtpStream *stream = FS_RTP_STREAM (item->data);

    fs_rtp_stream_remove_known_ssrc (stream, ssrc);
  }

  FS_RTP_SESSION_UNLOCK (session);

  /*
   * TODO:
   *
   * Remove running streams with that SSRC .. lets also check if they
   * come from the right ip/port/etc ??
   */
}

static void
_discovery_caps_changed (GstPad *pad, GParamSpec *pspec, FsRtpSession *session)
{
  GstCaps *caps = NULL;
  GstStructure *s = NULL;
  int i;
  CodecAssociation *ca = NULL;

  g_object_get (pad, "caps", &caps, NULL);

  g_return_if_fail (GST_CAPS_IS_SIMPLE(caps));

  s = gst_caps_get_structure (caps, 0);

  FS_RTP_SESSION_LOCK (session);

  if (!session->priv->discovery_codec)
  {
    fs_session_emit_error (FS_SESSION (session), FS_ERROR_INTERNAL,
        "Internal error while discovering codecs configurations",
        "Got notify::caps signal on the discovery codecs whith no codecs"
        " being discovered");
    goto out;
  }

  ca = lookup_codec_association_by_codec (session->priv->codec_associations,
      session->priv->discovery_codec);

  fs_codec_destroy (session->priv->discovery_codec);
  session->priv->discovery_codec = NULL;

  if (!ca)
    goto out;

  for (i = 0; i < gst_structure_n_fields (s); i++)
  {
    const gchar *name = gst_structure_nth_field_name (s, i);
    if (name)
    {
      const gchar *value = gst_structure_get_string (s, name);
      if (value)
      {
        if (codec_has_config_data_named (ca->codec, name))
        {
          GList *item = NULL;

          for (item = ca->codec->optional_params; item;
               item = g_list_next (item))
          {
            FsCodecParameter *param = item->data;
            if (!g_ascii_strcasecmp (param->name, name))
            {
              if (!g_ascii_strcasecmp (param->value, value))
                break;

              /* replace the value if its different */
              fs_codec_remove_optional_parameter (ca->codec, param);
              fs_codec_add_optional_parameter (ca->codec, name, value);
              break;
            }
          }

          /* Add it if it wasn't there */
          if (item == NULL)
            fs_codec_add_optional_parameter (ca->codec, name, value);
        }
      }
    }
  }

  ca->need_config = FALSE;

 out:

  FS_RTP_SESSION_UNLOCK (session);

  if (caps)
    gst_caps_unref (caps);
}

/**
 * fs_rtp_session_get_codec_params:
 * @session: a #FsRtpSession
 * @ca: the #CodecAssociaton to get params for
 *
 * Gets the parameters for the specified #CodecAssociation
 *
 * Returns: %TRUE on success, %FALSE on error
 */

static gboolean
fs_rtp_session_get_codec_params (FsRtpSession *session, CodecAssociation *ca,
    GError **error)
{
  GstPad *pad = NULL;
  gchar *tmp;

  FS_RTP_SESSION_LOCK (session);

  if (session->priv->discovery_codecbin)
  {
    gst_element_set_locked_state (session->priv->discovery_codecbin, TRUE);
    gst_element_set_state (session->priv->discovery_codecbin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (session->priv->conference),
        session->priv->discovery_codecbin);
    session->priv->discovery_codecbin = NULL;
  }

  /* They must both exist or neither exists, anything else is wrong */
  if ((session->priv->discovery_fakesink == NULL ||
          session->priv->discovery_capsfilter == NULL) &&
      session->priv->discovery_fakesink != session->priv->discovery_capsfilter)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Capsfilter and fakesink not synchronized, fakesink:%p capsfilter:%p",
        session->priv->discovery_fakesink, session->priv->discovery_capsfilter);
    goto error;
  }

  if (session->priv->discovery_fakesink == NULL &&
      session->priv->discovery_capsfilter == NULL)
  {
    GstCaps *caps;

    tmp = g_strdup_printf ("discovery_fakesink_%d", session->id);
    session->priv->discovery_fakesink =
      gst_element_factory_make ("fakesink", tmp);
    g_free (tmp);
    if (!session->priv->discovery_fakesink)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not make fakesink element");
      goto error;
    }
    g_object_set (session->priv->discovery_fakesink,
        "sync", FALSE,
        "async", FALSE,
        NULL);

    if (!gst_bin_add (GST_BIN (session->priv->conference),
            session->priv->discovery_fakesink))
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not add the discovery fakesink to the bin");
      goto error;
    }

    if (!gst_element_sync_state_with_parent (session->priv->discovery_fakesink))
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not sync the discovery fakesink's state with its parent");
      goto error;
    }

    tmp = g_strdup_printf ("discovery_capsfilter_%d", session->id);
    session->priv->discovery_capsfilter =
      gst_element_factory_make ("capsfilter", tmp);
    g_free (tmp);
    if (!session->priv->discovery_capsfilter)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not make capsfilter element");
      goto error;
    }

    caps = fs_codec_to_gst_caps (ca->codec);
    g_object_set (session->priv->discovery_capsfilter,
        "caps", caps,
        NULL);
    gst_caps_unref (caps);

    if (!gst_bin_add (GST_BIN (session->priv->conference),
            session->priv->discovery_capsfilter))
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not add the discovery capsfilter to the bin");
      goto error;
    }

    if (!gst_element_sync_state_with_parent (session->priv->discovery_fakesink))
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not sync the discovery capsfilter's state with its parent");
      goto error;
    }

    if (!gst_element_link_pads (session->priv->discovery_capsfilter, "src",
            session->priv->discovery_fakesink, "sink"))
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not link discovery capsfilter and fakesink");
      goto error;
    }

    pad = gst_element_get_static_pad (session->priv->discovery_capsfilter,
        "src");
    g_signal_connect (pad, "notify::caps", G_CALLBACK (_discovery_caps_changed),
        session);
    gst_object_unref (pad);
  }

  tmp = g_strdup_printf ("discover_%d_%d", session->id, ca->codec->id);
  session->priv->discovery_codecbin = _create_codec_bin (ca->blueprint,
      ca->codec, tmp, TRUE, error);
  g_free (tmp);

  if (!session->priv->discovery_codecbin)
    goto error;

  if (!gst_bin_add (GST_BIN (session->priv->conference),
            session->priv->discovery_codecbin))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the discovery codecbin to the bin");
    goto error;
  }

  if (!gst_element_sync_state_with_parent (session->priv->discovery_codecbin))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not sync the discovery codecbin's state with its parent");
    goto error;
  }

  if (!gst_element_link_pads (session->priv->discovery_codecbin, "src",
            session->priv->discovery_capsfilter, "sink"))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link discovery codecbin and capsfilter");
    goto error;
  }

  pad = gst_element_get_static_pad (session->priv->discovery_codecbin, "sink");

  if (GST_PAD_LINK_FAILED (gst_pad_link (session->priv->send_tee_discovery_pad,
              pad)))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the tee and the discovery codecbin");
    gst_object_unref (pad);
    goto error;
  }

  gst_object_unref (pad);

  session->priv->discovery_codec = fs_codec_copy (ca->codec);

  g_object_set (session->priv->media_sink_valve, "drop", FALSE, NULL);

  FS_RTP_SESSION_UNLOCK (session);

  return TRUE;

 error:

  if (session->priv->discovery_fakesink)
  {
    gst_element_set_locked_state (session->priv->discovery_fakesink, TRUE);
    gst_element_set_state (session->priv->discovery_fakesink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (session->priv->conference),
        session->priv->discovery_fakesink);
    session->priv->discovery_fakesink = NULL;
  }

  if (session->priv->discovery_capsfilter)
  {
    gst_element_set_locked_state (session->priv->discovery_capsfilter, TRUE);
    gst_element_set_state (session->priv->discovery_capsfilter, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (session->priv->conference),
        session->priv->discovery_capsfilter);
    session->priv->discovery_capsfilter = NULL;
  }

  if (session->priv->discovery_codecbin)
  {
    gst_element_set_locked_state (session->priv->discovery_codecbin, TRUE);
    gst_element_set_state (session->priv->discovery_codecbin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (session->priv->conference),
        session->priv->discovery_codecbin);
    session->priv->discovery_codecbin = NULL;
  }

  FS_RTP_SESSION_UNLOCK (session);

  return FALSE;
}

/**
 * _send_sink_pad_have_data_callback:
 *
 * This is a callback function for the "have-data" signal, it returns always
 * %TRUE, because we never drop buffers at this stage
 */

static gboolean
_send_sink_pad_have_data_callback (GstPad *pad, GstMiniObject *obj,
    FsRtpSession *session)
{
  GError *error = NULL;
  GList *item = NULL;
  CodecAssociation *ca = NULL;

  FS_RTP_SESSION_LOCK (session);

  if (session->priv->discovery_codec)
    goto out;


  /* Find out if there is a codec that needs the config to be fetched */
  for (item = g_list_first (session->priv->codec_associations);
       item;
       item = g_list_next (item))
  {
    ca = item->data;
    if (ca->need_config)
      break;
  }
  if (!item)
  {
    fs_rtp_session_stop_codec_param_gathering (session);
    g_object_notify (G_OBJECT (session), "codecs-ready");
    gst_element_post_message (GST_ELEMENT (session->priv->conference),
        gst_message_new_element (GST_OBJECT (session->priv->conference),
            gst_structure_new ("farsight-codecs-ready",
                "session", FS_TYPE_SESSION, session,
                NULL)));

    goto out;
  }

  if (!fs_rtp_session_get_codec_params (session, ca, &error))
  {
    fs_rtp_session_stop_codec_param_gathering (session);
    fs_session_emit_error (FS_SESSION (session), error->code,
        "Error while discovering codec data, discovery cancelled",
        error->message);
  }

 out:

  g_clear_error (&error);

  FS_RTP_SESSION_UNLOCK (session);
  return TRUE;
}

/**
 * fs_rtp_session_start_codec_param_gathering
 * @session: a #FsRtpSession
 *
 * Check if there is any codec associations that requires codec discovery and
 * if there is, starts the gathering process by adding a pad probe to the
 * send valve
 */

static void
fs_rtp_session_start_codec_param_gathering (FsRtpSession *session)
{
  GList *item = NULL;

  FS_RTP_SESSION_LOCK (session);

  /* Find out if there is a codec that needs the config to be fetched */
  for (item = g_list_first (session->priv->codec_associations);
       item;
       item = g_list_next (item))
  {
    CodecAssociation *ca = item->data;
    if (ca->need_config)
      break;
  }
  if (!item)
    goto out;

  if (!session->priv->discovery_blocking_id)
    session->priv->discovery_blocking_id = gst_pad_add_data_probe (
        session->priv->media_sink_pad,
        G_CALLBACK (_send_sink_pad_have_data_callback), session);

 out:

  FS_RTP_SESSION_UNLOCK (session);
}


/**
 * fs_rtp_session_stop_codec_param_gathering
 * @session: a #FsRtpSession
 *
 * Check if there is any codec associations that requires codec discovery and
 * if there is, starts the gathering process by adding a pad probe to the
 * send valve
 */

static void
fs_rtp_session_stop_codec_param_gathering (FsRtpSession *session)
{

  FS_RTP_SESSION_LOCK (session);

  if (session->priv->discovery_codec)
  {
    fs_codec_destroy (session->priv->discovery_codec);
    session->priv->discovery_codec = NULL;
  }

  if (session->priv->discovery_blocking_id)
  {
    gst_pad_remove_data_probe (session->priv->media_sink_pad,
        session->priv->discovery_blocking_id);
    session->priv->discovery_blocking_id = 0;
  }

  if (!session->priv->send_codecbin)
    g_object_set (session->priv->media_sink_valve, "drop", TRUE, NULL);

  if (session->priv->discovery_fakesink)
  {
    gst_element_set_locked_state (session->priv->discovery_fakesink, TRUE);
    gst_element_set_state (session->priv->discovery_fakesink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (session->priv->conference),
        session->priv->discovery_fakesink);
    session->priv->discovery_fakesink = NULL;
  }

  if (session->priv->discovery_capsfilter)
  {
    gst_element_set_locked_state (session->priv->discovery_capsfilter, TRUE);
    gst_element_set_state (session->priv->discovery_capsfilter, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (session->priv->conference),
        session->priv->discovery_capsfilter);
    session->priv->discovery_capsfilter = NULL;
  }

  if (session->priv->discovery_codecbin)
  {
    gst_element_set_locked_state (session->priv->discovery_codecbin, TRUE);
    gst_element_set_state (session->priv->discovery_codecbin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (session->priv->conference),
        session->priv->discovery_codecbin);
    session->priv->discovery_codecbin = NULL;
  }

  FS_RTP_SESSION_UNLOCK (session);
}

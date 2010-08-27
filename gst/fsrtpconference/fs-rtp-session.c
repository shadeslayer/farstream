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
 * This object represents one session, it is created by called
 * fs_conference_new_session() on a #FsRtpConference. It can be either
 * Audio or Video. It also represents data send with one and only one
 * SSRC (although if there is a SSRC collision, that SSRC may change).
 * </para>
 * <refsect2><title>Codec profiles</title>
 * <para>
 * It is possible to define "codec profiles", that is non-autodetected
 * encoding and decoding pipelines for codecs. It is even possible to declare
 * entirely new codecs using this method.
 *
 * To create a profile for a codec, add it to the codec-preferences with
 * special optional parameters called "farsight-send-profile" and
 * "farsight-recv-profile", these should contain gst-launch style descriptions
 * of the encoding or decoding bin.
 *
 * As a special case, encoding profiles can have more than one unconnected
 * source pad, all of these pads should produce application/x-rtp of some kind.
 * The profile will be ignored if not ALL pads match currently negotiated
 * codecs.
 *
 * Also, it is possible to declare profiles with only a decoding pipeline,
 * you will only be able to receive from this codec, the encoding may be a
 * secondary pad of some other codec.
 * </para></refsect2><para>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtp-session.h"

#include <string.h>

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

#include <gst/farsight/fs-transmitter.h>
#include "gst/farsight/fs-utils.h"
#include <gst/farsight/fs-rtp.h>

#include "fs-rtp-stream.h"
#include "fs-rtp-participant.h"
#include "fs-rtp-discover-codecs.h"
#include "fs-rtp-codec-negotiation.h"
#include "fs-rtp-substream.h"
#include "fs-rtp-special-source.h"
#include "fs-rtp-codec-specific.h"
#include "fs-rtp-tfrc.h"

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
  PROP_CODEC_PREFERENCES,
  PROP_CODECS,
  PROP_CODECS_WITHOUT_CONFIG,
  PROP_CURRENT_SEND_CODEC,
  PROP_CODECS_READY,
  PROP_CONFERENCE,
  PROP_NO_RTCP_TIMEOUT,
  PROP_SSRC,
  PROP_TOS,
  PROP_SEND_BITRATE,
  PROP_RTP_HEADER_EXTENSIONS,
  PROP_RTP_HEADER_EXTENSION_PREFERENCES
};

#define DEFAULT_NO_RTCP_TIMEOUT (7000)

struct _FsRtpSessionPrivate
{
  FsMediaType media_type;

  /* We hold a ref to this, needs the lock to access it */
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

  GstElement *rtpmuxer;

  GObject *rtpbin_internal_session;

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
   * These three elements can only be modified from the streaming threads
   * and are protected by the stream lock
   */
  GstElement *discovery_fakesink;
  GstElement *discovery_capsfilter;
  GstElement *discovery_codecbin;
  /* This one is protected by the session lock */
  FsCodec *discovery_codec;

  /* Request pad to release on dispose */
  GstPad *rtpbin_send_rtp_sink;
  GstPad *rtpbin_send_rtcp_src;

  GstPad *rtpbin_recv_rtp_sink;
  GstPad *rtpbin_recv_rtcp_sink;

  /* Protected by the session mutex */
  /* The codec bin is owned implicitely by the Conference bin for us */
  FsCodec *current_send_codec;
  FsCodec *requested_send_codec;

  /* Can only be modified by the streaming thread with the pad blocked */
  GstElement *send_codecbin;
  GList *extra_send_capsfilters;

  /* These lists are protected by the session mutex */
  GList *streams;
  guint streams_cookie;
  GList *free_substreams;
  guint streams_sending;

  /* The static list of all the blueprints */
  GList *blueprints;

  GList *codec_preferences;
  guint codec_preferences_generation;

  /* These are protected by the session mutex */
  GList *codec_associations;

  GList *hdrext_negotiated;
  GList *hdrext_preferences;

  /* Protected by the session mutex */
  gint no_rtcp_timeout;

  GList *extra_sources;

  /* This is a ht of ssrc->streams
   * It is protected by the session mutex */
  GHashTable *ssrc_streams;
  GHashTable *ssrc_streams_manual;

  GError *construction_error;

  GMutex *send_pad_blocked_mutex;
  GMutex *discovery_pad_blocked_mutex;

  /* IP Type of Service, protext by session mutex */
  guint tos;

  /* Protected by session mutex */
  guint send_bitrate;

  /* Set at construction time, can not change */
  GstElement *send_filter;
  FsRtpTfrc *rtp_tfrc;

  /* Can only be used while using the lock */
  GStaticRWLock disposed_lock;
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
static gboolean fs_rtp_session_set_codec_preferences (FsSession *session,
    GList *codec_preferences,
    GError **error);
static void fs_rtp_session_verify_send_codec_bin (FsRtpSession *self);

static gchar **fs_rtp_session_list_transmitters (FsSession *session);
static GType fs_rtp_session_get_stream_transmitter_type (FsSession *session,
    const gchar *transmitter);

static void _substream_no_rtcp_timedout_cb (FsRtpSubStream *substream,
    FsRtpSession *session);
static GstElement *_substream_get_codec_bin (FsRtpSubStream *substream,
    FsRtpStream *stream, FsCodec *current_codec, FsCodec **new_codec,
    guint current_builder_hash, guint *new_builder_hash,
    GError **error, FsRtpSession *session);

static gboolean _stream_new_remote_codecs (FsRtpStream *stream,
    GList *codecs, GError **error, gpointer user_data);


static FsStreamTransmitter *fs_rtp_session_get_new_stream_transmitter (
    FsRtpSession *self,
    const gchar *transmitter_name,
    FsParticipant *participant,
    guint n_parameters,
    GParameter *parameters,
    GError **error);

static GList *fs_rtp_session_get_codecs_need_resend (FsSession *session,
    GList *old_codecs, GList *new_codecs);


static void _remove_stream (gpointer user_data,
    GObject *where_the_object_was);

static gboolean
fs_rtp_session_update_codecs (FsRtpSession *session,
    FsRtpStream *stream,
    GList *remote_codecs,
    GError **error);

static CodecAssociation *
fs_rtp_session_get_recv_codec_locked (FsRtpSession *session,
    guint pt,
    FsRtpStream *stream,
    FsCodec **recv_codec,
    GError **error);

static void
fs_rtp_session_start_codec_param_gathering_locked (FsRtpSession *session);
static void
fs_rtp_session_stop_codec_param_gathering_unlock (FsRtpSession *session);

static void
fs_rtp_session_associate_free_substreams (FsRtpSession *session,
    FsRtpStream *stream, guint32 ssrc);

static void
_send_caps_changed (GstPad *pad, GParamSpec *pspec, FsRtpSession *session);
static void
_discovery_pad_blocked_callback (GstPad *pad, gboolean blocked,
    gpointer user_data);

static void
fs_rtp_session_set_send_bitrate (FsRtpSession *self, guint bitrate);
static gboolean
codecbin_set_bitrate (GstElement *codecbin, guint bitrate);

//static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_rtp_session_class_init (FsRtpSessionClass *klass)
{
  GObjectClass *gobject_class;
  FsSessionClass *session_class;

  gobject_class = (GObjectClass *) klass;
  session_class = FS_SESSION_CLASS (klass);

  gobject_class->set_property = fs_rtp_session_set_property;
  gobject_class->get_property = fs_rtp_session_get_property;
  gobject_class->constructed = fs_rtp_session_constructed;

  session_class->new_stream = fs_rtp_session_new_stream;
  session_class->start_telephony_event = fs_rtp_session_start_telephony_event;
  session_class->stop_telephony_event = fs_rtp_session_stop_telephony_event;
  session_class->set_send_codec = fs_rtp_session_set_send_codec;
  session_class->set_codec_preferences =
    fs_rtp_session_set_codec_preferences;
  session_class->list_transmitters = fs_rtp_session_list_transmitters;
  session_class->get_stream_transmitter_type =
    fs_rtp_session_get_stream_transmitter_type;
  session_class->codecs_need_resend = fs_rtp_session_get_codecs_need_resend;

  g_object_class_override_property (gobject_class,
    PROP_MEDIA_TYPE, "media-type");
  g_object_class_override_property (gobject_class,
    PROP_ID, "id");
  g_object_class_override_property (gobject_class,
    PROP_SINK_PAD, "sink-pad");
  g_object_class_override_property (gobject_class,
    PROP_CODEC_PREFERENCES, "codec-preferences");
  g_object_class_override_property (gobject_class,
    PROP_CODECS, "codecs");
  g_object_class_override_property (gobject_class,
    PROP_CODECS_WITHOUT_CONFIG, "codecs-without-config");
  g_object_class_override_property (gobject_class,
    PROP_CURRENT_SEND_CODEC, "current-send-codec");
  g_object_class_override_property (gobject_class,
    PROP_CODECS_READY, "codecs-ready");
  g_object_class_override_property (gobject_class,
    PROP_TOS, "tos");

  g_object_class_install_property (gobject_class,
    PROP_CONFERENCE,
    g_param_spec_object ("conference",
      "The Conference this stream refers to",
      "This is a convience pointer for the Conference",
      FS_TYPE_RTP_CONFERENCE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SSRC,
      g_param_spec_uint ("ssrc",
          "The SSRC of the sent data",
          "This is the current SSRC used to send data"
          " (defaults to a random value)",
          0, G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SEND_BITRATE,
      g_param_spec_uint ("send-bitrate",
          "The bitrate at which data will be sent",
          "The bitrate that the session will try to send at in bits/sec",
          0, G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_RTP_HEADER_EXTENSIONS,
      g_param_spec_boxed ("rtp-header-extensions",
          "Currently negotiated RTP header extensions",
          "GList of RTP Header extensions that have been negotiated and will"
          " be used when sending of receiving RTP packets",
          FS_TYPE_RTP_HEADER_EXTENSION_LIST,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_RTP_HEADER_EXTENSION_PREFERENCES,
      g_param_spec_boxed ("rtp-header-extension-preferences",
          "Desired RTP header extensions",
          "GList of RTP Header extensions that are locally supported and"
          " desired by the application",
          FS_TYPE_RTP_HEADER_EXTENSION_LIST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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

  self->priv->send_pad_blocked_mutex = g_mutex_new ();
  self->priv->discovery_pad_blocked_mutex = g_mutex_new ();
  g_static_rw_lock_init (&self->priv->disposed_lock);

  self->priv->media_type = FS_MEDIA_TYPE_LAST + 1;

  self->priv->no_rtcp_timeout = DEFAULT_NO_RTCP_TIMEOUT;

  self->priv->ssrc_streams = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->priv->ssrc_streams_manual = g_hash_table_new (g_direct_hash,
      g_direct_equal);
}

static gboolean
_remove_transmitter (gpointer key, gpointer value, gpointer user_data)
{
  FsRtpSession *self = FS_RTP_SESSION (user_data);
  FsTransmitter *transmitter = FS_TRANSMITTER (value);
  GstElement *src, *sink;

  g_object_get (transmitter, "gst-sink", &sink, "gst-src", &src, NULL);

  gst_element_set_locked_state (src, TRUE);
  gst_element_set_state (src, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self->priv->conference), src);

  gst_element_set_locked_state (sink, TRUE);
  gst_element_set_state (sink, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self->priv->conference), sink);

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

  gst_object_unref (elem);
}

static void
stop_and_remove (GstBin *conf, GstElement **element, gboolean unref)
{
  if (*element == NULL)
    return;

  gst_element_set_locked_state (*element, TRUE);
  if (gst_element_set_state (*element, GST_STATE_NULL) !=
      GST_STATE_CHANGE_SUCCESS)
  {
    gchar *elemname = gst_element_get_name (*element);
    GST_WARNING ("Could not set %s to GST_STATE_NULL", elemname);
    g_free (elemname);
  }
  if (!gst_bin_remove (conf, *element))
  {
    gchar *binname = gst_element_get_name (conf);
    gchar *elemname = gst_element_get_name (*element);
    GST_WARNING ("Could not remove %s from %s", binname, elemname);
    g_free (binname);
    g_free (elemname);
  }
  if (unref)
    gst_object_unref (*element);
  *element = NULL;
}


static gpointer
trigger_dispose (gpointer data)
{
  g_object_unref (data);
  return NULL;
}

static void
fs_rtp_session_real_dispose (FsRtpSession *self)
{
  GList *item = NULL;
  GstBin *conferencebin = NULL;

  g_static_rw_lock_writer_lock (&self->priv->disposed_lock);
  if (self->priv->disposed)
  {
    g_static_rw_lock_writer_unlock (&self->priv->disposed_lock);
    return;
  }
  self->priv->disposed = TRUE;
  g_static_rw_lock_writer_unlock (&self->priv->disposed_lock);

  conferencebin = GST_BIN (self->priv->conference);

  if (self->priv->rtpbin_internal_session)
    g_object_unref (self->priv->rtpbin_internal_session);
  self->priv->rtpbin_internal_session = NULL;

  /* Lets stop all of the elements sink to source */

  /* First the send pipeline */
  if (self->priv->transmitters)
    g_hash_table_foreach (self->priv->transmitters, _stop_transmitter_elem,
      "gst-sink");

  stop_and_remove (conferencebin, &self->priv->transmitter_rtp_tee, TRUE);
  stop_and_remove (conferencebin, &self->priv->transmitter_rtcp_tee, TRUE);

  if (self->priv->rtpbin_send_rtcp_src)
    gst_pad_set_active (self->priv->rtpbin_send_rtcp_src, FALSE);
  if (self->priv->rtpbin_send_rtp_sink)
    gst_pad_set_active (self->priv->rtpbin_send_rtp_sink, FALSE);

  if (self->priv->send_filter)
    stop_and_remove (conferencebin, &self->priv->send_filter, FALSE);

  if (self->priv->rtp_tfrc)
    gst_object_unref (self->priv->rtp_tfrc);
  self->priv->rtp_tfrc = NULL;


  FS_RTP_SESSION_LOCK (self);
  fs_rtp_session_stop_codec_param_gathering_unlock (self);

  if (self->priv->send_tee_discovery_pad)
  {
    gst_object_unref (self->priv->send_tee_discovery_pad);
    self->priv->send_tee_discovery_pad = NULL;
  }

  if (self->priv->send_tee_media_pad)
  {
    gst_object_unref (self->priv->send_tee_media_pad);
    self->priv->send_tee_media_pad = NULL;
  }

  if (self->priv->send_capsfilter && self->priv->rtpmuxer)
  {
    GstPad *srcpad = gst_element_get_static_pad (self->priv->send_capsfilter,
        "src");
    if (srcpad)
    {
      GstPad *otherpad = gst_pad_get_peer (srcpad);
      if (otherpad)
      {
        gst_element_release_request_pad (self->priv->rtpmuxer, otherpad);
        gst_object_unref (otherpad);
      }
      gst_object_unref (srcpad);
    }
  }

  for (item = self->priv->extra_send_capsfilters;
       item;
       item = g_list_next (item))
  {
    GstElement *cf = item->data;
    GstPad *ourpad = gst_element_get_static_pad (cf, "src");
    GstPad *pad = NULL;

    if (ourpad)
    {
      pad = gst_pad_get_peer (ourpad);
      if (pad)
      {
        gst_element_release_request_pad (self->priv->rtpmuxer, pad);
        gst_object_unref (pad);
      }
      gst_object_unref (ourpad);
    }
  }

  stop_and_remove (conferencebin, &self->priv->rtpmuxer, TRUE);
  stop_and_remove (conferencebin, &self->priv->send_capsfilter, TRUE);

  while (self->priv->extra_send_capsfilters)
  {
    GstElement *cf = self->priv->extra_send_capsfilters->data;

    stop_and_remove (conferencebin, &cf, FALSE);
    self->priv->extra_send_capsfilters = g_list_delete_link (
        self->priv->extra_send_capsfilters,
        self->priv->extra_send_capsfilters);
  }

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


  if (self->priv->media_sink_pad)
  {
    gst_pad_set_active (self->priv->media_sink_pad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (self->priv->conference),
      self->priv->media_sink_pad);
    self->priv->media_sink_pad = NULL;
  }


  if (self->priv->rtpbin_send_rtcp_src)
  {
    gst_pad_set_active (self->priv->rtpbin_send_rtcp_src, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_send_rtcp_src);
    gst_object_unref (self->priv->rtpbin_send_rtcp_src);
    self->priv->rtpbin_send_rtcp_src = NULL;
  }

  if (self->priv->rtpbin_send_rtp_sink)
  {
    gst_pad_set_active (self->priv->rtpbin_send_rtp_sink, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_send_rtp_sink);
    gst_object_unref (self->priv->rtpbin_send_rtp_sink);
    self->priv->rtpbin_send_rtp_sink = NULL;
  }

  if (self->priv->rtpbin_recv_rtp_sink)
  {
    gst_pad_set_active (self->priv->rtpbin_recv_rtp_sink, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_recv_rtp_sink);
    gst_object_unref (self->priv->rtpbin_recv_rtp_sink);
    self->priv->rtpbin_recv_rtp_sink = NULL;
  }

  if (self->priv->rtpbin_recv_rtcp_sink)
  {
    gst_pad_set_active (self->priv->rtpbin_recv_rtcp_sink, FALSE);
    gst_element_release_request_pad (self->priv->conference->gstrtpbin,
      self->priv->rtpbin_recv_rtcp_sink);
    gst_object_unref (self->priv->rtpbin_recv_rtcp_sink);
    self->priv->rtpbin_recv_rtcp_sink = NULL;
  }

  if (self->priv->transmitters)
  {
    g_hash_table_foreach_remove (self->priv->transmitters, _remove_transmitter,
      self);

    g_hash_table_destroy (self->priv->transmitters);
    self->priv->transmitters = NULL;
  }

  if (self->priv->free_substreams)
  {
    g_list_foreach (self->priv->free_substreams, (GFunc) g_object_unref, NULL);
    g_list_free (self->priv->free_substreams);
    self->priv->free_substreams = NULL;
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
  self->priv->streams_cookie++;
  g_hash_table_remove_all (self->priv->ssrc_streams);
  g_hash_table_remove_all (self->priv->ssrc_streams_manual);


  G_OBJECT_CLASS (fs_rtp_session_parent_class)->dispose (G_OBJECT (self));
}

static void
fs_rtp_session_dispose (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return;

  if (fs_rtp_conference_is_internal_thread (self->priv->conference))
  {
    g_object_ref (self);
    if (!g_thread_create (trigger_dispose, self, FALSE, NULL))
      g_error ("Could not create dispose thread");
    fs_rtp_session_has_disposed_exit (self);
  }
  else
  {
    fs_rtp_session_has_disposed_exit (self);
    fs_rtp_session_real_dispose (self);
  }
}

static void
fs_rtp_session_finalize (GObject *object)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  g_mutex_free (self->mutex);
  self->mutex = NULL;

  if (self->priv->blueprints)
  {
    fs_rtp_blueprints_unref (self->priv->media_type);
    self->priv->blueprints = NULL;
  }

  fs_codec_list_destroy (self->priv->codec_preferences);
  codec_association_list_destroy (self->priv->codec_associations);

  fs_rtp_header_extension_list_destroy (self->priv->hdrext_preferences);
  fs_rtp_header_extension_list_destroy (self->priv->hdrext_negotiated);

  if (self->priv->current_send_codec)
    fs_codec_destroy (self->priv->current_send_codec);

  if (self->priv->requested_send_codec)
    fs_codec_destroy (self->priv->requested_send_codec);

  if (self->priv->ssrc_streams)
    g_hash_table_destroy (self->priv->ssrc_streams);

  if (self->priv->ssrc_streams_manual)
    g_hash_table_destroy (self->priv->ssrc_streams_manual);

  g_mutex_free (self->priv->send_pad_blocked_mutex);
  g_mutex_free (self->priv->discovery_pad_blocked_mutex);


  g_static_rw_lock_free (&self->priv->disposed_lock);

  G_OBJECT_CLASS (fs_rtp_session_parent_class)->finalize (object);
}

gboolean
fs_rtp_session_has_disposed_enter (FsRtpSession *self, GError **error)
{
  g_static_rw_lock_reader_lock (&self->priv->disposed_lock);

  if (self->priv->disposed)
  {
    g_static_rw_lock_reader_unlock (&self->priv->disposed_lock);
    g_set_error (error, FS_ERROR, FS_ERROR_DISPOSED,
        "Called function after session has been disposed");
    return TRUE;
  }

  return FALSE;
}


void
fs_rtp_session_has_disposed_exit (FsRtpSession *self)
{
  g_static_rw_lock_reader_unlock (&self->priv->disposed_lock);
}

static void
fs_rtp_session_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return;

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
    case PROP_CODEC_PREFERENCES:
      FS_RTP_SESSION_LOCK (self);
      g_value_set_boxed (value, self->priv->codec_preferences);
      FS_RTP_SESSION_UNLOCK (self);
      break;
    case PROP_CODECS:
      {
        GList *codecs = NULL;
        FS_RTP_SESSION_LOCK (self);
        codecs = codec_associations_to_codecs (self->priv->codec_associations,
            TRUE);
        FS_RTP_SESSION_UNLOCK (self);
        g_value_take_boxed (value, codecs);
      }
      break;
    case PROP_CODECS_WITHOUT_CONFIG:
      {
        GList *codecs = NULL;
        FS_RTP_SESSION_LOCK (self);
        codecs = codec_associations_to_codecs (self->priv->codec_associations,
            FALSE);
        FS_RTP_SESSION_UNLOCK (self);
        g_value_take_boxed (value, codecs);
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
    case PROP_SSRC:
      g_object_get_property (G_OBJECT (self->priv->rtpbin_internal_session),
          "internal-ssrc", value);
      break;
    case PROP_TOS:
      FS_RTP_SESSION_LOCK (self);
      g_value_set_uint (value, self->priv->tos);
      FS_RTP_SESSION_UNLOCK (self);
      break;
    case PROP_SEND_BITRATE:
      FS_RTP_SESSION_LOCK (self);
      g_value_set_uint (value, self->priv->send_bitrate);
      FS_RTP_SESSION_UNLOCK (self);
      break;
    case PROP_RTP_HEADER_EXTENSIONS:
      FS_RTP_SESSION_LOCK (self);
      g_value_set_boxed (value, self->priv->hdrext_negotiated);
      FS_RTP_SESSION_UNLOCK (self);
      break;
    case PROP_RTP_HEADER_EXTENSION_PREFERENCES:
      FS_RTP_SESSION_LOCK (self);
      g_value_set_boxed (value, self->priv->hdrext_preferences);
      FS_RTP_SESSION_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  fs_rtp_session_has_disposed_exit (self);
}

static void
set_tos (gpointer key, gpointer val, gpointer user_data)
{
  FsTransmitter *trans = val;
  guint tos = GPOINTER_TO_UINT (user_data);

  g_object_set (trans, "tos", tos, NULL);
}

static void
fs_rtp_session_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  FsRtpSession *self = FS_RTP_SESSION (object);

  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return;

  switch (prop_id)
  {
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
    case PROP_SSRC:
      g_object_set_property (G_OBJECT (self->priv->rtpbin_internal_session),
          "internal-ssrc", value);
      break;
    case PROP_TOS:
      FS_RTP_SESSION_LOCK (self);
      self->priv->tos = g_value_get_uint (value);
      g_hash_table_foreach (self->priv->transmitters, set_tos,
          GUINT_TO_POINTER (self->priv->tos));
      FS_RTP_SESSION_UNLOCK (self);
      break;
    case PROP_SEND_BITRATE:
      fs_rtp_session_set_send_bitrate (self, g_value_get_uint (value));
      break;
    case PROP_RTP_HEADER_EXTENSION_PREFERENCES:
      FS_RTP_SESSION_LOCK (self);
      fs_rtp_header_extension_list_destroy (self->priv->hdrext_preferences);
      self->priv->hdrext_preferences = g_value_dup_boxed (value);
      FS_RTP_SESSION_UNLOCK (self);
      /* This call can't fail because the codecs do NOT change */
      fs_rtp_session_update_codecs (self, NULL, NULL, NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  fs_rtp_session_has_disposed_exit (self);
}

static void
_rtpbin_internal_session_notify_internal_ssrc (GObject *internal_session,
    GParamSpec *pspec, gpointer self)
{
  g_object_notify (G_OBJECT (self), "ssrc");
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
  GstPad *tee_sink_pad = NULL;
  GstPad *valve_sink_pad = NULL;
  GstPad *funnel_src_pad = NULL;
  GstPad *muxer_src_pad = NULL;
  GstPad *transmitter_rtcp_tee_sink_pad;
  GstPad *pad;
  GstPadLinkReturn ret;
  gchar *tmp;

  if (self->id == 0)
  {
    g_error ("You can no instantiate this element directly, you MUST"
      " call fs_rtp_session_new ()");
    return;
  }

  self->priv->blueprints = fs_rtp_blueprints_get (self->priv->media_type,
    &self->priv->construction_error);

  if (!self->priv->blueprints)
  {
    if (!self->priv->construction_error)
      self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_INTERNAL,
        "Unknown error while trying to discover codecs");
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
      "Could not add the tee element to the FsRtpConference");
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
  valve = gst_element_factory_make ("valve", tmp);
  g_free (tmp);

  if (!valve)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the valve element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), valve))
  {
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




  /* Now create the transmitter RTP funnel */

  tmp = g_strdup_printf ("recv_rtp_funnel_%u", self->id);
  funnel = gst_element_factory_make ("fsfunnel", tmp);
  g_free (tmp);

  if (!funnel)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtp funnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), funnel))
  {
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

  if (GST_PAD_LINK_FAILED (ret))
  {
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

  if (!funnel)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtcp funnel element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), funnel))
  {
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

  if (GST_PAD_LINK_FAILED (ret))
  {
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


  /* Lets get the internal RTP session */

  g_signal_emit_by_name (self->priv->conference->gstrtpbin,
      "get-internal-session", self->id, &self->priv->rtpbin_internal_session);

  if (!self->priv->rtpbin_internal_session)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not get the rtpbin's internal session");
    return;
  }

  g_signal_connect (self->priv->rtpbin_internal_session,
      "notify::internal-ssrc",
      G_CALLBACK (_rtpbin_internal_session_notify_internal_ssrc), self);

  g_object_set (self->priv->rtpbin_internal_session,
      "favor-new", TRUE,
      "bandwidth", (gdouble) 0,
      "rtcp-fraction", (gdouble) 0.05,
      NULL);

  self->priv->rtp_tfrc = fs_rtp_tfrc_new (self->priv->rtpbin_internal_session,
      self->priv->rtpbin_recv_rtp_sink, self->priv->rtpbin_recv_rtcp_sink,
      &self->priv->send_filter);


  /* Lets now create the RTP muxer */

  tmp = g_strdup_printf ("send_rtp_muxer_%u", self->id);
  muxer = gst_element_factory_make ("rtpdtmfmux", tmp);
  g_free (tmp);

  if (!muxer)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtp muxer element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), muxer))
  {
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

  if (self->priv->send_filter)
  {
    if (!gst_bin_add (GST_BIN (self->priv->conference),
            self->priv->send_filter))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the rtp send filter element to the FsRtpConference");
      return;
    }

    if (!gst_element_link (muxer, self->priv->send_filter))
    {
       self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
           "Could not link rtp muxer and send filter ");
      return;
    }

    gst_element_set_state (self->priv->send_filter, GST_STATE_PLAYING);

    muxer_src_pad = gst_element_get_static_pad (self->priv->send_filter, "src");
  }
  else
  {
    muxer_src_pad = gst_element_get_static_pad (muxer, "src");
  }

  ret = gst_pad_link (muxer_src_pad, self->priv->rtpbin_send_rtp_sink);

  if (GST_PAD_LINK_FAILED (ret))
  {
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

  if (!tee)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtp tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), tee))
  {
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
          self->priv->transmitter_rtp_tee, "sink"))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link rtpbin %s pad to tee sink", tmp);
    g_free (tmp);
    return;
  }
  g_free (tmp);

  /* Now create the transmitter RTCP tee */

  tmp = g_strdup_printf ("send_rtcp_tee_%u", self->id);
  tee = gst_element_factory_make ("tee", tmp);
  g_free (tmp);

  if (!tee)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtcp tee element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), tee))
  {
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

  /* Lets now do the send_capsfilter */

  tmp = g_strdup_printf ("send_rtp_capsfilter_%u", self->id);
  capsfilter = gst_element_factory_make ("capsfilter", tmp);
  g_free (tmp);

  if (!capsfilter)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not create the rtp capsfilter element");
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), capsfilter))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not add the rtp capsfilter element to the FsRtpConference");
    gst_object_unref (capsfilter);
    return;
  }

  pad = gst_element_get_static_pad (capsfilter, "src");
  g_signal_connect (pad, "notify::caps", G_CALLBACK (_send_caps_changed),
      self);
  gst_object_unref (pad);

  self->priv->send_capsfilter = gst_object_ref (capsfilter);

  if (!gst_element_link_pads (capsfilter, "src", muxer, "sink_%d"))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link pad capsfilter src pad to the rtpmux");
    return;
  }

  gst_element_set_state (capsfilter, GST_STATE_PLAYING);

  if (!fs_rtp_session_update_codecs (self, NULL, NULL,
          &self->priv->construction_error))
  {
    g_assert (self->priv->construction_error);
    return;
  }

  if (G_OBJECT_CLASS (fs_rtp_session_parent_class)->constructed)
    G_OBJECT_CLASS (fs_rtp_session_parent_class)->constructed(object);
}

static void
_stream_known_source_packet_received (FsRtpStream *stream, guint component,
    GstBuffer *buffer, gpointer user_data)
{
  guint32 ssrc;
  FsRtpSession *self = FS_RTP_SESSION_CAST (user_data);

  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return;

  if (component == 1)
  {
    if (gst_rtp_buffer_validate (buffer))
    {
      ssrc = gst_rtp_buffer_get_ssrc (buffer);
      goto ok;
    }
  }
  else if (component == 2)
  {
    GstRTCPPacket rtcppacket;

    if (gst_rtcp_buffer_validate (buffer))
    {
      if (gst_rtcp_buffer_get_first_packet (buffer, &rtcppacket))
      {
        do {
          if (gst_rtcp_packet_get_type (&rtcppacket) == GST_RTCP_TYPE_SDES)
          {
            ssrc = gst_rtcp_packet_sdes_get_ssrc (&rtcppacket);
            goto ok;
          }
        } while (gst_rtcp_packet_move_to_next (&rtcppacket));
      }
    }
  }

  /* We would have jumped to OK if we had a valid packet */
  fs_rtp_session_has_disposed_exit (self);
  return;

 ok:

  FS_RTP_SESSION_LOCK (self);

  if (!g_hash_table_lookup (self->priv->ssrc_streams,  GUINT_TO_POINTER (ssrc)))
  {
    GST_DEBUG ("Associating SSRC %x in session %d", ssrc, self->id);
    g_hash_table_insert (self->priv->ssrc_streams, GUINT_TO_POINTER (ssrc),
        stream);

    FS_RTP_SESSION_UNLOCK (self);

    fs_rtp_session_associate_free_substreams (self, stream, ssrc);
  }
  else
  {
    FS_RTP_SESSION_UNLOCK (self);
  }

  fs_rtp_session_has_disposed_exit (self);
}

static void
_stream_sending_changed_locked (FsRtpStream *stream, gboolean sending,
    gpointer user_data)
{
  FsRtpSession *session = user_data;

  if (sending)
    session->priv->streams_sending++;
  else
    session->priv->streams_sending--;

  if (fs_rtp_session_has_disposed_enter (session, NULL))
    return;

  if (session->priv->streams_sending && session->priv->send_codecbin)
    g_object_set (session->priv->media_sink_valve, "drop", FALSE, NULL);
  else
    g_object_set (session->priv->media_sink_valve, "drop", TRUE, NULL);

  fs_rtp_session_has_disposed_exit (session);
}

static void
_stream_ssrc_added_cb (FsRtpStream *stream, guint32 ssrc, gpointer user_data)
{
  FsRtpSession *self = user_data;

  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return;

  FS_RTP_SESSION_LOCK (self);
  g_hash_table_insert (self->priv->ssrc_streams, GUINT_TO_POINTER (ssrc),
      stream);
  g_hash_table_insert (self->priv->ssrc_streams_manual, GUINT_TO_POINTER (ssrc),
      stream);
  FS_RTP_SESSION_UNLOCK (self);

  fs_rtp_session_associate_free_substreams (self, stream, ssrc);

  fs_rtp_session_has_disposed_exit (self);
}


static gboolean
_remove_stream_from_ht (gpointer key, gpointer value, gpointer user_data)
{
  return (value == user_data);
}

static void
_remove_stream (gpointer user_data,
    GObject *where_the_object_was)
{
  FsRtpSession *self = FS_RTP_SESSION (user_data);

  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return;

  FS_RTP_SESSION_LOCK (self);
  self->priv->streams =
    g_list_remove_all (self->priv->streams, where_the_object_was);
  self->priv->streams_cookie++;

  g_hash_table_foreach_remove (self->priv->ssrc_streams, _remove_stream_from_ht,
      where_the_object_was);
  g_hash_table_foreach_remove (self->priv->ssrc_streams_manual,
      _remove_stream_from_ht, where_the_object_was);
  FS_RTP_SESSION_UNLOCK (self);

  fs_rtp_session_has_disposed_exit (self);
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

  if (!FS_IS_RTP_PARTICIPANT (participant))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "You have to provide a participant of type RTP");
    return NULL;
  }

  if (fs_rtp_session_has_disposed_enter (self, error))
    return NULL;

  rtpparticipant = FS_RTP_PARTICIPANT (participant);

  st = fs_rtp_session_get_new_stream_transmitter (self, transmitter,
      participant, n_parameters, parameters, error);

  if (!st)
  {
    fs_rtp_session_has_disposed_exit (self);
    return NULL;
  }

  new_stream = FS_STREAM_CAST (fs_rtp_stream_new (self, rtpparticipant,
          direction, st, _stream_new_remote_codecs,
          _stream_known_source_packet_received,
          _stream_sending_changed_locked,
          _stream_ssrc_added_cb,
          self, error));


  if (new_stream)
  {
    FS_RTP_SESSION_LOCK (self);
    if (direction & FS_DIRECTION_SEND)
    {
      self->priv->streams_sending++;
      if (self->priv->send_codecbin)
        g_object_set (self->priv->media_sink_valve, "drop", FALSE, NULL);
    }

    self->priv->streams = g_list_append (self->priv->streams, new_stream);
    self->priv->streams_cookie++;
    FS_RTP_SESSION_UNLOCK (self);

    g_object_weak_ref (G_OBJECT (new_stream), _remove_stream, self);
  }
  fs_rtp_session_has_disposed_exit (self);
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
  GstElement *rtpmuxer = NULL;
  GstPad *pad;
  gboolean ret = FALSE;
  gchar *method_str;
  gint method_int;

  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return FALSE;

  switch (method)
  {
    case FS_DTMF_METHOD_AUTO:
      method_str = "default";
      method_int = 1;
      break;
    case FS_DTMF_METHOD_RTP_RFC4733:
      method_str="RFC4733";
      method_int = 1;
      break;
    case FS_DTMF_METHOD_IN_BAND:
      method_str="sound";
      method_int = 2;
      break;
    default:
      GST_WARNING ("Invalid telephony event method %d", method);
      goto out;
  }

  FS_RTP_SESSION_LOCK (self);
  g_assert (self->priv->rtpmuxer);
  rtpmuxer = gst_object_ref (self->priv->rtpmuxer);
  FS_RTP_SESSION_UNLOCK (self);

  GST_DEBUG ("sending telephony event %d using method=%s",
      event, method_str);

  pad = gst_element_get_static_pad (rtpmuxer, "src");

  ret = gst_pad_send_event (pad,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("dtmf-event",
              "number", G_TYPE_INT, event,
              "volume", G_TYPE_INT, volume,
              "start", G_TYPE_BOOLEAN, TRUE,
              "type", G_TYPE_INT, 1,
              "method", G_TYPE_INT, method_int,
              NULL)));

  if (!ret && method == FS_DTMF_METHOD_AUTO)
    ret = gst_pad_send_event (pad,
        gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
            gst_structure_new ("dtmf-event",
                "number", G_TYPE_INT, event,
                "volume", G_TYPE_INT, volume,
                "start", G_TYPE_BOOLEAN, TRUE,
                "type", G_TYPE_INT, 1,
                "method", G_TYPE_INT, 2,
                NULL)));

  gst_object_unref (pad);
  gst_object_unref (rtpmuxer);

  out:

  fs_rtp_session_has_disposed_exit (self);
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
  GstElement *rtpmuxer = NULL;
  GstPad *pad;
  gboolean ret = FALSE;

  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return FALSE;

  switch (method)
  {
    case FS_DTMF_METHOD_AUTO:
    case FS_DTMF_METHOD_RTP_RFC4733:
    case FS_DTMF_METHOD_IN_BAND:
      break;
    default:
      GST_WARNING ("Invalid telephony event method %d", method);
      goto out;
  }

  FS_RTP_SESSION_LOCK (self);
  g_assert (self->priv->rtpmuxer);
  rtpmuxer = gst_object_ref (self->priv->rtpmuxer);
  FS_RTP_SESSION_UNLOCK (self);

  GST_DEBUG ("stopping telephony event");

  pad = gst_element_get_static_pad (rtpmuxer, "src");

  ret = gst_pad_send_event (pad,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("dtmf-event",
              "start", G_TYPE_BOOLEAN, FALSE,
              "type", G_TYPE_INT, 1,
              NULL)));

  gst_object_unref (pad);
  gst_object_unref (rtpmuxer);

out:
  fs_rtp_session_has_disposed_exit (self);
  return ret;
}

/**
 * fs_rtp_session_set_send_codec:
 * @session: an #FsRtpSession
 * @send_codec: an #FsCodec representing the codec to send
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function will set the currently being sent codec for all streams in this
 * session. The given #FsCodec must be taken directly from the #FsSession:codecs
 * property of the session. If the given codec is not in the codecs
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
  gboolean ret;

  if (fs_rtp_session_has_disposed_enter (self, error))
    return FALSE;

  FS_RTP_SESSION_LOCK (self);

  if (lookup_codec_association_by_codec_for_sending (
          self->priv->codec_associations, send_codec))
  {
    if (self->priv->requested_send_codec)
      fs_codec_destroy (self->priv->requested_send_codec);

    self->priv->requested_send_codec = fs_codec_copy (send_codec);

    fs_rtp_session_verify_send_codec_bin (self);
    ret = TRUE;
  }
  else
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "The passed codec is not part of the list of codecs");
    ret = FALSE;
  }

  FS_RTP_SESSION_UNLOCK (self);

  fs_rtp_session_has_disposed_exit (self);
  return ret;
}

static gboolean
fs_rtp_session_set_codec_preferences (FsSession *session,
    GList *codec_preferences,
    GError **error)
{
  FsRtpSession *self = FS_RTP_SESSION (session);
  GList *old_codec_prefs = NULL;
  GList *new_codec_prefs = NULL;
  gboolean ret;
  guint current_generation;

  if (fs_rtp_session_has_disposed_enter (self, error))
    return FALSE;

  new_codec_prefs = codecs_copy_with_new_ptime (codec_preferences);

  new_codec_prefs =
    validate_codecs_configuration (
        self->priv->media_type, self->priv->blueprints,
        new_codec_prefs);

  if (new_codec_prefs == NULL)
    GST_DEBUG ("None of the new codec preferences passed are usable,"
        " this will restore the original list of detected codecs");

  FS_RTP_SESSION_LOCK (self);
  old_codec_prefs = self->priv->codec_preferences;
  self->priv->codec_preferences = new_codec_prefs;
  current_generation = self->priv->codec_preferences_generation;
  self->priv->codec_preferences_generation++;
  FS_RTP_SESSION_UNLOCK (self);

  ret = fs_rtp_session_update_codecs (self, NULL, NULL, error);

  if (ret)
  {
    fs_codec_list_destroy (old_codec_prefs);

    g_object_notify ((GObject*) self, "codec-preferences");
  }
  else
  {
    FS_RTP_SESSION_LOCK (self);
    if (self->priv->codec_preferences_generation == current_generation)
    {
      fs_codec_list_destroy (self->priv->codec_preferences);
      self->priv->codec_preferences = old_codec_prefs;
      self->priv->codec_preferences_generation++;
    }
    else
    {
      fs_codec_list_destroy (old_codec_prefs);
    }
    FS_RTP_SESSION_UNLOCK (self);
    GST_WARNING ("Invalid new codec preferences");
  }

  fs_rtp_session_has_disposed_exit (self);
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

  if (session->priv->construction_error)
  {
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

  if (fs_rtp_session_has_disposed_enter (session, NULL))
    return NULL;

  FS_RTP_SESSION_LOCK (session);

  ca = lookup_codec_association_by_pt (
      session->priv->codec_associations, pt);

  if (ca)
  {
    FsCodec *tmpcodec = codec_copy_filtered (ca->codec, FS_PARAM_TYPE_CONFIG);
    caps = fs_codec_to_gst_caps (tmpcodec);
    fs_codec_destroy (tmpcodec);
  }

  FS_RTP_SESSION_UNLOCK (session);

  if (!caps)
    GST_WARNING ("Could not get caps for payload type %u in session %d",
        pt, session->id);


  fs_rtp_session_has_disposed_exit (session);
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


  if (!requestpad)
  {
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

  if (GST_PAD_LINK_FAILED (ret))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Can not link the %s to the transmitter %s", tee_funnel_name,
      (direction == GST_PAD_SINK) ? "sink" : "src");
    return FALSE;
  }

  return TRUE;
}

static void
_transmitter_error (
    FsStreamTransmitter *stream_transmitter,
    gint errorno,
    gchar *error_msg,
    gchar *debug_msg,
    gpointer user_data)
{
  FsSession *session = FS_SESSION (user_data);

  fs_session_emit_error (session, errorno, error_msg, debug_msg);
}

static GstElement *
_get_recvonly_filter (FsTransmitter *transmitter, guint component,
    gpointer user_data)
{
  if (component == FS_COMPONENT_RTCP)
    return gst_element_factory_make ("fsrtcpfilter", NULL);
  else
    return NULL;
}

static gboolean
fs_rtp_session_add_transmitter_gst_sink (FsRtpSession *self,
    FsTransmitter *transmitter,
    GError **error)
{
  GstElement *sink;

  g_object_get (transmitter, "gst-sink", &sink, NULL);

  if (!gst_bin_add (GST_BIN (self->priv->conference), sink))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the transmitter sink for %s to the conference",
        G_OBJECT_TYPE_NAME(transmitter));
    goto error;
  }

  gst_element_sync_state_with_parent (sink);

  if (!_get_request_pad_and_link (self->priv->transmitter_rtp_tee,
      "rtp tee", sink, "sink1", GST_PAD_SINK, error))
    goto error;

  if (!_get_request_pad_and_link (self->priv->transmitter_rtcp_tee,
      "rtcp tee", sink, "sink2", GST_PAD_SINK, error))
    goto error;

  gst_object_unref (sink);

  return TRUE;

 error:
  if (sink)
    gst_object_unref (sink);

  return FALSE;
}

/**
 * fs_rtp_session_get_transmitter:
 * @self: a #FsRtpSession
 * @transmitter_name: The name of the transmitter
 * @error: a #GError or %NULL
 *
 * Returns the requested #FsTransmitter, possibly creating it if it
 * does not exist.
 *
 * Returns: a #FsTransmitter or %NULL on error
 */
static FsTransmitter *
fs_rtp_session_get_transmitter (FsRtpSession *self,
    const gchar *transmitter_name,
    GError **error)
{
  FsTransmitter *transmitter;
  GstElement *src = NULL;
  guint tos;

  FS_RTP_SESSION_LOCK (self);
  transmitter = g_hash_table_lookup (self->priv->transmitters,
    transmitter_name);

  if (transmitter)
  {
    g_object_ref (transmitter);
    FS_RTP_SESSION_UNLOCK (self);
    return transmitter;
  }
  tos = self->priv->tos;
  FS_RTP_SESSION_UNLOCK (self);

  transmitter = fs_transmitter_new (transmitter_name, 2, tos, error);
  if (!transmitter)
    return NULL;

  g_signal_connect (transmitter, "error", G_CALLBACK (_transmitter_error),
      self);
  g_signal_connect (transmitter, "get-recvonly-filter",
      G_CALLBACK (_get_recvonly_filter), NULL);

  if (!fs_rtp_session_add_transmitter_gst_sink (self, transmitter, error))
    goto error;

  g_object_get (transmitter, "gst-src", &src, NULL);

  if (!gst_bin_add (GST_BIN (self->priv->conference), src))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the transmitter src for %s to the conference",
      transmitter_name);
    goto error;
  }

  if (!_get_request_pad_and_link (self->priv->transmitter_rtp_funnel,
      "rtp funnel", src, "src1", GST_PAD_SRC, error))
    goto error;

  if (!_get_request_pad_and_link (self->priv->transmitter_rtcp_funnel,
      "rtcp funnel", src, "src2", GST_PAD_SRC, error))
    goto error;

  gst_element_sync_state_with_parent (src);

  FS_RTP_SESSION_LOCK (self);
  /* Check if two were added at the same time */
  if (g_hash_table_lookup (self->priv->transmitters, transmitter_name))
  {
    FS_RTP_SESSION_UNLOCK (self);

    gst_element_set_locked_state (src, TRUE);
    gst_element_set_state (src, GST_STATE_NULL);
    goto error;
  }

  g_object_ref (transmitter);

  g_hash_table_insert (self->priv->transmitters, g_strdup (transmitter_name),
      transmitter);
  FS_RTP_SESSION_UNLOCK (self);

  gst_object_unref (src);

  return transmitter;

  /*
   * TODO:
   * The transmitters sink/sources should be cleanly removed if there is
   * an error
  */

 error:
  if (src)
    gst_object_unref (src);
  if (transmitter)
    g_object_unref (transmitter);

  return NULL;
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
  FsStreamTransmitter *st = NULL;

  transmitter = fs_rtp_session_get_transmitter (self, transmitter_name, error);

  if (!transmitter)
    return NULL;

  st = fs_transmitter_new_stream_transmitter (transmitter, participant,
      n_parameters, parameters, error);

  g_object_unref (transmitter);

  return st;
}

/**
 * fs_rtp_session_get_stream_by_ssrc_locked
 * @self: The #FsRtpSession
 * @stream_ssrc: The stream ssrc
 *
 * Gets the #FsRtpStream for the SSRC or %NULL if it doesnt exist
 *
 * Return value: A #FsRtpStream (unref after use) or %NULL if it doesn't exist
 */
static FsRtpStream *
fs_rtp_session_get_stream_by_ssrc_locked (FsRtpSession *self,
    guint32 ssrc)
{
  FsRtpStream *stream = NULL;

  stream = g_hash_table_lookup (self->priv->ssrc_streams,
      GUINT_TO_POINTER (ssrc));

  if (stream)
    g_object_ref (stream);

  return stream;
}


/**
 * fs_rtp_session_verify_recv_codecs_locked
 * @session: A #FsRtpSession
 *
 * Verifies that the various substreams still have the right codec, otherwise
 * re-sets it.
 */

static void
fs_rtp_session_verify_recv_codecs_locked (FsRtpSession *session)
{
  GList *item, *item2;

  for (item = g_list_first (session->priv->free_substreams);
       item;
       item = g_list_next (item))
    fs_rtp_sub_stream_verify_codec_locked (item->data);

  for (item = g_list_first (session->priv->streams);
       item;
       item = g_list_next (item))
  {
    FsRtpStream *stream = item->data;

    for (item2 = g_list_first (stream->substreams);
         item2;
         item2 = g_list_next (item2))
      fs_rtp_sub_stream_verify_codec_locked (item2->data);

  }
}

/**
 * fs_rtp_session_distribute_recv_codecs_locked:
 * @session: a #FsRtpSession
 * @force_stream: The #FsRtpStream to which the new remote codecs belong
 * @forced_remote_codecs: The #GList of remote codecs to use for that stream
 *
 * This function distributes the codecs to the streams including their
 * own config data.
 *
 * If a stream is specified, it will use the specified remote codecs
 * instead of the ones currently in the stream.
 */


static void
fs_rtp_session_distribute_recv_codecs_locked (FsRtpSession *session,
    FsRtpStream *force_stream,
    GList *forced_remote_codecs)
{
  GList *item = NULL;
  guint cookie;

 restart:

  cookie = session->priv->streams_cookie;

  for (item = session->priv->streams;
       item;
       item = g_list_next (item))
  {
    FsRtpStream *stream = item->data;
    GList *remote_codecs = NULL;

    if (stream == force_stream)
      remote_codecs = forced_remote_codecs;
    else
      remote_codecs = stream->remote_codecs;

    if (remote_codecs)
    {
      GList *new_codecs = codec_associations_to_codecs (
          session->priv->codec_associations, FALSE);
      GList *item2 = NULL;

      for (item2 = new_codecs;
           item2;
           item2 = g_list_next (item2))
      {
        FsCodec *codec = item2->data;
        GList *item3 = NULL;
        FsCodec *remote_codec = NULL;

        for (item3 = remote_codecs; item3; item3 = g_list_next (item3))
        {
          FsCodec *tmpcodec = NULL;
          remote_codec = item3->data;

          tmpcodec = sdp_negotiate_codec (codec, FS_PARAM_TYPE_RECV,
              remote_codec, FS_PARAM_TYPE_RECV | FS_PARAM_TYPE_CONFIG);
          if (tmpcodec)
          {
            fs_codec_destroy (tmpcodec);
            break;
          }
        }

        if (item3 == NULL)
          remote_codec = NULL;

        GST_LOG ("Adding codec to stream %p " FS_CODEC_FORMAT, stream,
            FS_CODEC_ARGS (codec));

        if (remote_codec)
        {
          for (item3 = remote_codec->optional_params; item3;
               item3 = g_list_next (item3))
          {
            FsCodecParameter *param = item3->data;
            if (codec_has_config_data_named (codec, param->name))
            {
              GST_LOG ("Adding parameter to stream %p %s=%s", stream,
                  param->name, param->value);
              fs_codec_add_optional_parameter (codec, param->name,
                  param->value);
            }
          }
        }
      }

      /* This function unlocks the lock, so we have to check the cookie
       * when we come back */
      g_object_ref (stream);
      fs_rtp_stream_set_negotiated_codecs_unlock (stream, new_codecs);
      g_object_unref (stream);

      FS_RTP_SESSION_LOCK (session);

      if (cookie != session->priv->streams_cookie)
        goto restart;
    }
  }
}


/**
 * fs_rtp_session_negotiate_codecs_locked:
 * @session: a #FsRtpSession
 * @stream: The #FsRtpStream to which the new remote codecs belong
 * @remote_codecs: The #GList of remote codecs to use for that stream
 * @has_remotes: Set to %TRUE if at least one stream has remote codecs
 *  set to %FALSE otherwise
 * @is_new: Set to %TRUE if the codecs associations have changed
 *
 * Negotiates the codecs using the current (stored) codecs
 * and the remote codecs from each stream.
 * If a stream is specified, it will use the specified remote codecs
 * instead of the ones currently in the stream
 *
 * Returns: %TRUE if a new list could be negotiated, otherwise %FALSE and sets
 *  @error
 */

static gboolean
fs_rtp_session_negotiate_codecs_locked (FsRtpSession *session,
    FsRtpStream *stream,
    GList *remote_codecs,
    gboolean *has_remotes,
    gboolean *is_new,
    GError **error)
{
  gint streams_with_codecs = 0;
  gboolean has_many_streams = FALSE;
  GList *new_negotiated_codec_associations = NULL;
  GList *item;
  guint8 hdrext_used_ids[8];
  GList *new_hdrexts = NULL;

  *has_remotes = FALSE;

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
      session->priv->blueprints, session->priv->codec_preferences,
      session->priv->codec_associations);

  if (!new_negotiated_codec_associations)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_NO_CODECS_LEFT,
        "Codec config would leave no valid local codecs");
    goto error;
  }

  new_hdrexts = create_local_header_extensions (
    session->priv->hdrext_negotiated, session->priv->hdrext_preferences,
    hdrext_used_ids);

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

      new_hdrexts = negotiate_stream_header_extensions (new_hdrexts,
          mystream->hdrext, !has_many_streams, hdrext_used_ids);
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

  new_negotiated_codec_associations =
    fs_rtp_special_sources_negotiation_filter (
        new_negotiated_codec_associations);

  if (session->priv->codec_associations)
    *is_new = ! codec_associations_list_are_equal (
      session->priv->codec_associations, new_negotiated_codec_associations);

  codec_association_list_destroy (session->priv->codec_associations);
  session->priv->codec_associations = new_negotiated_codec_associations;

  new_hdrexts = finish_header_extensions_nego (new_hdrexts, hdrext_used_ids);

  fs_rtp_header_extension_list_destroy (session->priv->hdrext_negotiated);
  session->priv->hdrext_negotiated = new_hdrexts;

  return TRUE;

 error:

  fs_rtp_header_extension_list_destroy (new_hdrexts);

  return FALSE;
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
  gboolean is_new = TRUE;
  gboolean has_remotes = FALSE;

  FS_RTP_SESSION_LOCK (session);

  if (!fs_rtp_session_negotiate_codecs_locked (
        session, stream, remote_codecs, &has_remotes, &is_new, error))
  {
    FS_RTP_SESSION_UNLOCK (session);
    return FALSE;
  }

  fs_rtp_session_distribute_recv_codecs_locked (session, stream, remote_codecs);

  fs_rtp_session_verify_recv_codecs_locked (session);

  if (is_new)
    g_signal_emit_by_name (session->priv->conference->gstrtpbin,
        "clear-pt-map");

  fs_rtp_session_start_codec_param_gathering_locked (session);

  FS_RTP_SESSION_UNLOCK (session);

  if (has_remotes)
  {
    fs_rtp_session_verify_send_codec_bin (session);
  }

  if (is_new)
  {
    g_object_notify (G_OBJECT (session), "codecs");
    g_object_notify (G_OBJECT (session), "codecs-without-config");

    gst_element_post_message (GST_ELEMENT (session->priv->conference),
        gst_message_new_element (GST_OBJECT (session->priv->conference),
            gst_structure_new ("farsight-codecs-changed",
                "session", FS_TYPE_SESSION, session,
                NULL)));
  }

  return TRUE;
}

static gboolean
_stream_new_remote_codecs (FsRtpStream *stream,
    GList *codecs, GError **error, gpointer user_data)
{
  FsRtpSession *session = FS_RTP_SESSION_CAST (user_data);
  gboolean ret;

  if (fs_rtp_session_has_disposed_enter (session, error))
    return FALSE;

  ret = fs_rtp_session_update_codecs (session, stream, codecs, error);

  fs_rtp_session_has_disposed_exit (session);
  return ret;
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

static void
fs_rtp_session_update_minimum_rtcp_interval (FsRtpSession *self,
    FsRtpSubStream *skip_substream)
{
  guint min_interval = 5000;
  GList *item, *item2;

  FS_RTP_SESSION_LOCK (self);

  if (self->priv->current_send_codec)
    min_interval = MIN (min_interval,
        self->priv->current_send_codec->ABI.ABI.minimum_reporting_interval);

  for (item = self->priv->free_substreams; item; item = item->next)
  {
    FsRtpSubStream *substream = item->data;

    if (substream == skip_substream)
      continue;

    if (substream->codec)
      min_interval = MIN (min_interval,
          substream->codec->ABI.ABI.minimum_reporting_interval);
  }

  for (item2 = self->priv->streams; item2; item2 = item2->next)
  {
    FsRtpStream *stream = item2->data;

    for (item = stream->substreams; item; item = item->next)
    {
      FsRtpSubStream *substream = item->data;

      if (substream == skip_substream)
        continue;

      if (substream->codec)
        min_interval = MIN (min_interval,
            substream->codec->ABI.ABI.minimum_reporting_interval);
    }
  }

  FS_RTP_SESSION_UNLOCK (self);

  g_object_set (self->priv->rtpbin_internal_session,
      "rtcp-min-interval", (guint64) min_interval * GST_MSECOND, NULL);

}

static void
_substream_unlinked (FsRtpSubStream *substream, gpointer user_data)
{
  FsRtpSession *self = FS_RTP_SESSION (user_data);

  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return;

  fs_rtp_session_update_minimum_rtcp_interval (self, substream);

  FS_RTP_SESSION_LOCK (self);

  if (g_list_find (self->priv->free_substreams, substream))
  {
    self->priv->free_substreams = g_list_remove (self->priv->free_substreams,
        substream);
    FS_RTP_SESSION_UNLOCK (self);

    fs_rtp_sub_stream_stop (substream);
    g_object_unref (substream);
  }
  else
  {
    FS_RTP_SESSION_UNLOCK (self);
  }

  fs_rtp_session_has_disposed_exit (self);
}

static void
_substream_codec_changed (FsRtpSubStream *substream, FsRtpSession *self)
{
  if (fs_rtp_session_has_disposed_enter (self, NULL))
    return;

  fs_rtp_session_update_minimum_rtcp_interval (self, NULL);

  fs_rtp_session_has_disposed_exit (self);
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

  if (fs_rtp_session_has_disposed_enter (session, NULL))
    return;

  FS_RTP_SESSION_LOCK (session);
  no_rtcp_timeout = session->priv->no_rtcp_timeout;
  FS_RTP_SESSION_UNLOCK (session);

  substream = fs_rtp_sub_stream_new (session->priv->conference, session,
      new_pad, ssrc, pt, no_rtcp_timeout, &error);

  if (substream == NULL)
  {
    if (error && error->domain == FS_ERROR)
      fs_session_emit_error (FS_SESSION (session), error->code,
        "Could not create a substream for the new pad", error->message);
    else
      fs_session_emit_error (FS_SESSION (session), FS_ERROR_CONSTRUCTION,
        "Could not create a substream for the new pad",
        "No error details returned");

    g_clear_error (&error);
    fs_rtp_session_has_disposed_exit (session);
    return;
  }

  g_signal_connect_object (substream, "get-codec-bin",
      G_CALLBACK (_substream_get_codec_bin), session, 0);

  g_signal_connect_object (substream, "unlinked",
      G_CALLBACK (_substream_unlinked), session, 0);

  g_signal_connect_object (substream, "codec-changed",
      G_CALLBACK (_substream_codec_changed), session, 0);

  /* Lets find the FsRtpStream for this substream, if no Stream claims it
   * then we just store it
   */

  FS_RTP_SESSION_LOCK (session);
  stream = fs_rtp_session_get_stream_by_ssrc_locked (session, ssrc);

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

      g_signal_connect_object (substream, "error",
          G_CALLBACK (_substream_error), session, 0);

      if (no_rtcp_timeout > 0)
      {
        g_signal_connect_object (substream, "no-rtcp-timedout",
            G_CALLBACK (_substream_no_rtcp_timedout_cb), session, 0);
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


  if (stream)
  {
    if (!fs_rtp_stream_add_substream_unlock (stream, substream, &error))
      fs_session_emit_error (FS_SESSION (session), error->code,
          "Could not add the output ghostpad to the new substream",
          error->message);

    g_clear_error (&error);
  }
  else
  {
    fs_rtp_sub_stream_verify_codec_locked (substream);
    FS_RTP_SESSION_UNLOCK (session);
  }

  if (stream)
    g_object_unref (stream);

  fs_rtp_session_has_disposed_exit (session);
}

static gboolean
validate_src_pads (gpointer item, GValue *ret, gpointer user_data)
{
  GstPad *pad = item;
  GList *codecs = user_data;
  GstCaps *caps;
  GList *listitem = NULL;
  gboolean retval = FALSE;

  caps = gst_pad_get_caps_reffed (pad);

  if (gst_caps_is_empty (caps))
  {
    GST_WARNING_OBJECT (pad, "Caps on pad are empty");
    goto error;
  }

  for (listitem = codecs; listitem; listitem = g_list_next (listitem))
  {
    FsCodec *codec = listitem->data;
    GstCaps *tmpcaps = fs_codec_to_gst_caps (codec);

    if (gst_caps_can_intersect (tmpcaps, caps))
    {
      GST_LOG_OBJECT (pad, "Pad matches " FS_CODEC_FORMAT,
          FS_CODEC_ARGS (codec));
      retval = TRUE;
    }
    gst_caps_unref (tmpcaps);

    if (retval)
      break;
  }

 error:

  gst_object_unref (pad);
  gst_caps_unref (caps);
  if (!retval)
    g_value_set_boolean (ret, FALSE);
  return retval;
}


static GstElement *
_create_codec_bin (const CodecAssociation *ca, const FsCodec *codec,
    const gchar *name, gboolean is_send, GList *codecs,
    guint current_builder_hash, guint *new_builder_hash, GError **error)
{
  GstElement *codec_bin = NULL;
  gchar *direction_str = (is_send == TRUE) ? "send" : "receive";
  gchar *profile = NULL;

  if (is_send)
    profile = ca->send_profile;
  else
    profile = ca->recv_profile;

  if (profile)
  {
    GError *tmperror = NULL;
    guint src_pad_count = 0, sink_pad_count = 0;

    /* Return nothing if the builder hash is the same, it would just return
     * the same thing
     */
    if (new_builder_hash)
    {
      *new_builder_hash = g_str_hash (profile);
      if (*new_builder_hash == current_builder_hash)
      {
        GST_DEBUG ("profile builder hash is the same for "FS_CODEC_FORMAT,
            FS_CODEC_ARGS (ca->codec));
        return NULL;
      }
      GST_DEBUG ("profile builder hash is different (new: %u != old: %u)"
          " for " FS_CODEC_FORMAT,
          *new_builder_hash, current_builder_hash, FS_CODEC_ARGS (ca->codec));
    }

    codec_bin = parse_bin_from_description_all_linked (profile,
        &src_pad_count, &sink_pad_count, &tmperror);

    if (codec_bin)
    {
      if (sink_pad_count != 1 || src_pad_count == 0)
      {
        GST_ERROR ("Invalid pad count (src:%u sink:%u)"
            " from codec profile: %s", src_pad_count, sink_pad_count, profile);
        gst_object_unref (codec_bin);
        codec_bin = NULL;
        goto try_factory;
      }

      if (codecs && src_pad_count > 1)
      {
        GstIterator *iter;
        GValue valid = {0};
        GstIteratorResult res;

        iter = gst_element_iterate_src_pads (codec_bin);
        g_value_init (&valid, G_TYPE_BOOLEAN);
        g_value_set_boolean (&valid, TRUE);
        res = gst_iterator_fold (iter, validate_src_pads, &valid,
            codecs);
        gst_iterator_free (iter);

        if (!g_value_get_boolean (&valid) || res == GST_ITERATOR_ERROR)
        {
          gst_object_unref (codec_bin);
          codec_bin = NULL;
          goto try_factory;
        }
      }

      GST_DEBUG ("creating %s codec bin for id %d, profile: %s",
          direction_str, codec->id, profile);
      gst_element_set_name (codec_bin, name);
      return codec_bin;
    }
    else if (!codec_blueprint_has_factory (ca->blueprint, is_send))
    {
      g_propagate_error (error, tmperror);
      return NULL;
    }
  }

 try_factory:

  if (new_builder_hash)
  {
    /* If its the same blueprint, it will be the same result,
     * so return NULL without an error.
     */
    *new_builder_hash = g_direct_hash (ca->blueprint);
    if (ca->blueprint && current_builder_hash == *new_builder_hash)
    {
      GST_DEBUG ("blueprint builder hash is the same for "FS_CODEC_FORMAT,
          FS_CODEC_ARGS (ca->codec));
      return NULL;
    }
    GST_DEBUG ("blueprint builder hash is different (new: %u != old: %u)"
        " for " FS_CODEC_FORMAT,
        *new_builder_hash, current_builder_hash, FS_CODEC_ARGS (ca->codec));
  }

  return create_codec_bin_from_blueprint (codec, ca->blueprint, name,
      is_send, error);
}

/**
 * fs_rtp_session_get_recv_codec_locked:
 * @session: a #FsRtpSession
 * @pt: The payload type to find the codec for
 * @stream: an optional #FsRtpStream for which this data is received
 * @recv_codec: The codec one wants to receive one
 *
 * This function returns the #CodecAssociation that will be used to receive
 * data on a specific payload type, optionally from a specific stream.
 *
 * MUST be called with the FsRtpSession lock held
 *
 * Returns: a #CodecAssociation, the caller doesn't own it
 */

static CodecAssociation *
fs_rtp_session_get_recv_codec_locked (FsRtpSession *session,
    guint pt,
    FsRtpStream *stream,
    FsCodec **recv_codec,
    GError **error)
{
  FsCodec *recv_codec_tmp = NULL;
  CodecAssociation *ca = NULL;
  GList *item = NULL;

  if (!session->priv->codec_associations)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "No codecs yet");
    return NULL;
  }

  ca = lookup_codec_association_by_pt (session->priv->codec_associations, pt);

  if (!ca)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_UNKNOWN_CODEC,
      "There is no negotiated codec with pt %d", pt);
    return NULL;
  }

  if (stream)
  {
    for (item = stream->negotiated_codecs; item; item = g_list_next (item))
    {
      recv_codec_tmp = item->data;
      if (recv_codec_tmp->id == pt)
        break;
    }

    if (item)
    {
      GST_DEBUG ("Receiving on stream codec " FS_CODEC_FORMAT,
          FS_CODEC_ARGS (recv_codec_tmp));
    }
    else
    {
      GST_DEBUG ("Have stream, but it does not have negotiatied codec");
      recv_codec_tmp = NULL;
    }
  }

  if (recv_codec_tmp)
  {
    *recv_codec = fs_codec_copy (recv_codec_tmp);
  }
  else
  {
    *recv_codec = codec_copy_filtered (ca->codec, FS_PARAM_TYPE_CONFIG);
    GST_DEBUG ("Receiving on session codec " FS_CODEC_FORMAT,
        FS_CODEC_ARGS (ca->codec));
  }

  return ca;
}

/**
 * fs_rtp_session_select_send_codec_locked:
 * @session: the #FsRtpSession
 *
 * This function selects the codec to send using either the user preference
 * or the remote preference (from the negotiation).
 *
 * You MUST own the FsRtpSession mutex to call this function
 *
 * Returns: a #CodecAssociation, the caller doesn't own it
 */

static CodecAssociation *
fs_rtp_session_select_send_codec_locked (FsRtpSession *session, GError **error)
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

  if (session->priv->requested_send_codec)
  {
    ca = lookup_codec_association_by_codec_for_sending (
        session->priv->codec_associations,
        session->priv->requested_send_codec);
    if (ca)
      return ca;


    /* The requested send codec no longer exists */
    fs_codec_destroy (session->priv->requested_send_codec);
    session->priv->requested_send_codec = NULL;

    GST_WARNING_OBJECT (session->priv->conference,
        "The current requested codec no longer exists, resetting");
  }

  /*
   * We don't have a requested codec, or it was not valid, lets use the first
   * codec from the list
   */
  for (ca_e = g_list_first (session->priv->codec_associations);
       ca_e;
       ca_e = g_list_next (ca_e))
  {
    if (codec_association_is_valid_for_sending (ca_e->data, TRUE))
    {
      ca = ca_e->data;
      break;
    }
  }

  if (ca == NULL)
    g_set_error (error, FS_ERROR, FS_ERROR_NEGOTIATION_FAILED,
        "Could not get a valid send codec");

  return ca;
}

struct link_data {
  FsRtpSession *session;
  GstCaps *caps;
  FsCodec *codec;

  GList *all_codecs;

  GList *other_codecs;

  GError **error;
};

/*
 * This is a  GstIteratorFoldFunction
 * It returns FALSE when it wants to stop the iteration
 */

static gboolean
link_main_pad (gpointer item, GValue *ret, gpointer user_data)
{
  GstPad *pad = item;
  struct link_data *data = user_data;
  GstCaps *caps;
  GstPad *other_pad;

  caps = gst_pad_get_caps_reffed (pad);

  if (!gst_caps_can_intersect (caps, data->caps))
  {
    gst_caps_unref (caps);
    gst_object_unref (pad);
    return TRUE;
  }
  gst_caps_unref (caps);

  other_pad = gst_element_get_static_pad (data->session->priv->send_capsfilter,
      "sink");

  if (!other_pad)
  {
    g_set_error (data->error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not get the sink pad of the send capsfilter");
    goto error;
  }

  if (GST_PAD_LINK_SUCCESSFUL(gst_pad_link (pad, other_pad)))
    g_value_set_boolean (ret, TRUE);
  else
    g_set_error (data->error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the send codec bin for pt %d to the send capsfilter",
        data->codec->id);

 error:

  gst_object_unref (other_pad);
  gst_object_unref (pad);

  return FALSE;
}


/*
 * This is a  GstIteratorFoldFunction
 * It returns FALSE when it wants to stop the iteration
 */

static gboolean
link_other_pads (gpointer item, GValue *ret, gpointer user_data)
{
  GstPad *pad = item;
  struct link_data *data = user_data;
  GstCaps *caps;
  GstCaps *filter_caps = NULL;
  GList *listitem;
  GstElement *capsfilter;
  GstPad *otherpad;

  if (gst_pad_is_linked (pad))
  {
    gst_object_unref (pad);
    return TRUE;
  }

  caps = gst_pad_get_caps_reffed (pad);

  if (gst_caps_is_empty (caps))
  {
    GST_WARNING_OBJECT (pad, "Caps on pad are empty");
    return TRUE;
  }

  for (listitem = data->all_codecs; listitem; listitem = g_list_next (listitem))
  {
    FsCodec *codec = listitem->data;

    filter_caps = fs_codec_to_gst_caps (codec);

    if (gst_caps_can_intersect (filter_caps, caps))
    {
      GST_LOG_OBJECT (pad, "Pad matches " FS_CODEC_FORMAT,
          FS_CODEC_ARGS (codec));
      break;
    }

    gst_caps_unref (filter_caps);
  }

  gst_caps_unref (caps);

  if (!listitem)
  {
    g_set_error (data->error, FS_ERROR, FS_ERROR_INTERNAL,
        "Could not find codec that matches the src pad");
    g_value_set_boolean (ret, FALSE);
    gst_object_unref (pad);
    return FALSE;
  }

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  g_object_set (capsfilter, "caps", filter_caps, NULL);

  if (!gst_bin_add (GST_BIN (data->session->priv->conference), capsfilter))
  {
    g_set_error (data->error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add send capsfilter to the conference");
    gst_object_unref (capsfilter);
    goto error;
  }

  data->session->priv->extra_send_capsfilters =
    g_list_append (data->session->priv->extra_send_capsfilters,
        capsfilter);

  otherpad = gst_element_get_static_pad (capsfilter, "sink");

  if (!otherpad)
  {
    g_set_error (data->error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not get sink pad on capsfilter");
    goto error;
  }

  if (GST_PAD_LINK_FAILED (gst_pad_link (pad, otherpad)))
  {
    g_set_error (data->error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not get sink pad on capsfilter");
    gst_object_unref (otherpad);
    goto error;
  }
  gst_object_unref (otherpad);
  gst_object_unref (pad);
  pad = NULL;

  if (!gst_element_link_pads (capsfilter, NULL,
          data->session->priv->rtpmuxer, "sink_%d"))
  {
    g_set_error (data->error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not an extra capsfilter to the muxer");
    goto error;
  }


  if (!gst_element_sync_state_with_parent (capsfilter))
  {
    g_set_error (data->error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not sync the state of the extra send capsfilter"
        " with the state of the conference");
    goto error;
  }

  data->other_codecs = g_list_append (data->other_codecs, filter_caps);

  return TRUE;

 error:

  g_value_set_boolean (ret, FALSE);
  gst_bin_remove (GST_BIN (data->session->priv->conference), capsfilter);
  data->session->priv->extra_send_capsfilters =
    g_list_remove (data->session->priv->extra_send_capsfilters,
        capsfilter);
  gst_object_unref (pad);
  gst_caps_unref (filter_caps);

  return FALSE;
}

/*
 * @codec: The currently selected codec for sending (but not the send_codec)
 */

static gboolean
fs_rtp_session_remove_send_codec_bin (FsRtpSession *self,
    FsCodec *codec,
    GstElement *send_codecbin,
    gboolean error_emit)
{
  FS_RTP_SESSION_LOCK (self);

  if (self->priv->send_codecbin || send_codecbin)
  {
    GstElement *codecbin = self->priv->send_codecbin;
    self->priv->send_codecbin = NULL;

    FS_RTP_SESSION_UNLOCK (self);

    if (!codecbin)
      codecbin = send_codecbin;

    gst_element_set_locked_state (codecbin, TRUE);
    if (gst_element_set_state (codecbin, GST_STATE_NULL) !=
        GST_STATE_CHANGE_SUCCESS)
    {
      gst_element_set_locked_state (codecbin, FALSE);
      GST_ERROR ("Could not stop the codec bin, setting it to NULL did not"
          " succeed");
      if (error_emit)
        fs_session_emit_error (FS_SESSION (self), FS_ERROR_INTERNAL,
            "Could not stop the codec bin",
            "Setting the codec bin to NULL did not succeed" );
      return FALSE;
    }

    gst_bin_remove (GST_BIN (self->priv->conference), codecbin);
    FS_RTP_SESSION_LOCK (self);
  }

  fs_codec_destroy (self->priv->current_send_codec);
  self->priv->current_send_codec = NULL;
  FS_RTP_SESSION_UNLOCK (self);

  while (self->priv->extra_send_capsfilters)
  {
    GstElement *cf = self->priv->extra_send_capsfilters->data;
    GstPad *ourpad = gst_element_get_static_pad (cf, "src");
    GstPad *pad = NULL;

    if (ourpad)
    {
      pad = gst_pad_get_peer (ourpad);
      if (pad)
      {
        gst_pad_set_active (pad, FALSE);
        gst_element_release_request_pad (self->priv->rtpmuxer, pad);
        gst_object_unref (pad);
      }
      gst_object_unref (ourpad);
    }

    gst_element_set_locked_state (cf, TRUE);
    gst_element_set_state (cf, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self->priv->conference), cf);

    self->priv->extra_send_capsfilters = g_list_delete_link (
        self->priv->extra_send_capsfilters,
        self->priv->extra_send_capsfilters);
  }

  if (codec)
    fs_rtp_special_sources_remove (
        &self->priv->extra_sources,
        &self->priv->codec_associations,
        FS_RTP_SESSION_GET_LOCK (self),
        codec);

  return TRUE;
}


/**
 * fs_rtp_session_add_send_codec_bin_unlock:
 * @session: a #FsRtpSession
 * @ca: the #CodecAssociation to use
 *
 * This function creates, adds and links a codec bin for the current send remote
 * codec
 *
 * Needs the Session lock to be held. and releases it
 *
 * Returns: The new codec bin (or NULL if there is an error)
 */

static GstElement *
fs_rtp_session_add_send_codec_bin_unlock (FsRtpSession *session,
    const CodecAssociation *ca,
    GList **other_codecs,
    GError **error)
{
  GstElement *codecbin = NULL;
  gchar *name;
  GstCaps *sendcaps;
  GList *codecs;
  GstIterator *iter;
  GValue link_rv = {0};
  struct link_data data;
  FsCodec *send_codec_copy = fs_codec_copy (ca->send_codec);
  FsCodec *codec_copy = fs_codec_copy (ca->codec);

  GST_DEBUG ("Trying to add send codecbin for " FS_CODEC_FORMAT,
      FS_CODEC_ARGS (ca->send_codec));

  name = g_strdup_printf ("send_%d_%d", session->id, ca->send_codec->id);
  codecs = codec_associations_to_send_codecs (
      session->priv->codec_associations);
  codecbin = _create_codec_bin (ca, ca->send_codec, name, TRUE, codecs,
      0, NULL, error);
  g_free (name);

  sendcaps = fs_codec_to_gst_caps (ca->send_codec);

  if (codecbin)
    codecbin_set_bitrate (codecbin, session->priv->send_bitrate);

  FS_RTP_SESSION_UNLOCK (session);

  if (!codecbin)
  {
    fs_codec_destroy (send_codec_copy);
    fs_codec_destroy (codec_copy);
    fs_codec_list_destroy (codecs);
    return NULL;
  }

  gst_element_set_locked_state (codecbin, TRUE);

  if (!gst_bin_add (GST_BIN (session->priv->conference), codecbin))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not add the send codec bin for: " FS_CODEC_FORMAT,
        FS_CODEC_ARGS (send_codec_copy));
    gst_object_unref (codecbin);
    fs_codec_list_destroy (codecs);
    fs_codec_destroy (send_codec_copy);
    fs_codec_destroy (codec_copy);
    gst_caps_unref (sendcaps);
    return NULL;
  }

  if (!gst_element_link_pads (session->priv->media_sink_valve, "src",
          codecbin, "sink"))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not link the send codec bin sink pad");
    gst_bin_remove (GST_BIN (session->priv->conference), (codecbin));
    fs_codec_list_destroy (codecs);
    gst_caps_unref (sendcaps);
    fs_codec_destroy (codec_copy);
    fs_codec_destroy (send_codec_copy);
    return NULL;
  }


  g_object_set (G_OBJECT (session->priv->send_capsfilter),
      "caps", sendcaps, NULL);

  iter = gst_element_iterate_src_pads (codecbin);

  g_value_init (&link_rv, G_TYPE_BOOLEAN);
  g_value_set_boolean (&link_rv, FALSE);

  data.session = session;
  data.caps = sendcaps;
  data.all_codecs = codecs;
  data.error = error;
  data.other_codecs = NULL;

  if (gst_iterator_fold (iter, link_main_pad, &link_rv, &data) ==
      GST_ITERATOR_ERROR)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not iterate over the src pads of the send codec bin to link"
        " the main pad for: " FS_CODEC_FORMAT, FS_CODEC_ARGS (send_codec_copy));
    gst_iterator_free (iter);
   goto error;
  }

  gst_caps_unref (sendcaps);

  if (!g_value_get_boolean (&link_rv))
  {
    gst_iterator_free (iter);
    goto error;
  }

  gst_iterator_resync (iter);

  if (gst_iterator_fold (iter, link_other_pads, &link_rv, &data) ==
      GST_ITERATOR_ERROR)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not iterate over the src pads of the send codec bin to link"
        " the main pad for: " FS_CODEC_FORMAT,
        FS_CODEC_ARGS (send_codec_copy));
    gst_iterator_free (iter);
    goto error;
  }

  gst_iterator_free (iter);

  if (!g_value_get_boolean (&link_rv))
    goto error;

  gst_element_set_locked_state (codecbin, FALSE);

  if (!gst_element_sync_state_with_parent (codecbin))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not sync the state of the codec bin with parent for pt: "
        FS_CODEC_FORMAT, FS_CODEC_ARGS (send_codec_copy));
    goto error;
  }


  FS_RTP_SESSION_LOCK (session);

  /* Re-set it here in case in changed while we were unlocked */
  codecbin_set_bitrate (codecbin, session->priv->send_bitrate);

  if (session->priv->streams_sending)
    g_object_set (session->priv->media_sink_valve, "drop", FALSE, NULL);

  while (data.other_codecs)
  {
    FsCodec *other_send_codec = data.other_codecs->data;
    CodecAssociation *ca;

    data.other_codecs = g_list_remove (data.other_codecs, other_send_codec);

    ca = lookup_codec_association_by_pt (session->priv->codec_associations,
        other_send_codec->id);

    if (ca)
      *other_codecs = g_list_append (*other_codecs,
          fs_codec_copy (ca->codec));
  }

  session->priv->send_codecbin = codecbin;

  session->priv->current_send_codec = codec_copy;
  FS_RTP_SESSION_UNLOCK (session);

  fs_codec_list_destroy (codecs);
  fs_codec_destroy (send_codec_copy);

  return codecbin;

 error:
  g_list_free (data.other_codecs);
  fs_rtp_session_remove_send_codec_bin (session, NULL, codecbin, FALSE);
  fs_codec_list_destroy (codecs);
  fs_codec_destroy (codec_copy);
  fs_codec_destroy (send_codec_copy);
  return NULL;
}

static void
pad_block_do_nothing (GstPad *pad, gboolean blocked, gpointer user_data)
{
}

/**
 * _send_src_pad_blocked_callback:
 *
 * This is the callback for the pad blocking on the media src pad
 * It is used to replace the codec bin when the send codec has been changed.
 */

static void
_send_src_pad_blocked_callback (GstPad *pad, gboolean blocked,
    gpointer user_data)
{
  FsRtpSession *self = FS_RTP_SESSION (user_data);
  CodecAssociation *ca = NULL;
  FsCodec *send_codec_copy = NULL;
  FsCodec *codec_copy = NULL;
  GError *error = NULL;
  gboolean changed = FALSE;
  GList *other_codecs = NULL;

  if (fs_rtp_session_has_disposed_enter (self, NULL))
  {
    gst_pad_set_blocked_async (pad, FALSE, pad_block_do_nothing, NULL);
    return;
  }

  g_mutex_lock (self->priv->send_pad_blocked_mutex);

  FS_RTP_SESSION_LOCK (self);
  ca = fs_rtp_session_select_send_codec_locked (self, &error);

  if (!ca)
  {
    fs_session_emit_error (FS_SESSION (self), error->code,
        "Could not select a new send codec", error->message);
    goto done_locked;
  }

  g_clear_error (&error);

  send_codec_copy = fs_codec_copy (ca->send_codec);
  if (fs_codec_are_equal (ca->codec, self->priv->current_send_codec))
  {
    codec_copy = fs_codec_copy (ca->codec);
    FS_RTP_SESSION_UNLOCK (self);

    /* If the main codec has not changed, the special codecs could still
     * have changed, so lets try to see if it is necessary to do something
     * about it.
     */

    changed |= fs_rtp_special_sources_remove (
        &self->priv->extra_sources,
        &self->priv->codec_associations,
        FS_RTP_SESSION_GET_LOCK (self),
        codec_copy);
    goto skip_main_codec;
  }

  FS_RTP_SESSION_UNLOCK (self);

  g_object_set (self->priv->media_sink_valve, "drop", TRUE, NULL);

  if (!fs_rtp_session_remove_send_codec_bin (self, send_codec_copy, NULL, TRUE))
    goto done;


  FS_RTP_SESSION_LOCK (self);
  /* We have to re-fetch the ca because we lifted the lock */
  fs_codec_destroy (send_codec_copy);
  send_codec_copy = NULL;
  ca = fs_rtp_session_select_send_codec_locked (self, &error);

  if (!ca)
  {
    fs_session_emit_error (FS_SESSION (self), error->code,
        "Could not select a new send codec", error->message);
    goto done_locked;
  }

  g_clear_error (&error);

  send_codec_copy = fs_codec_copy (ca->send_codec);
  codec_copy = fs_codec_copy (ca->codec);

  if (!fs_rtp_session_add_send_codec_bin_unlock (self, ca, &other_codecs,
          &error))
  {
    fs_session_emit_error (FS_SESSION (self), error->code,
        "Could not build a new send codec bin", error->message);
  }

  changed = TRUE;

 skip_main_codec:

  changed |= fs_rtp_special_sources_create (
      &self->priv->extra_sources,
      &self->priv->codec_associations,
      FS_RTP_SESSION_GET_LOCK (self),
      codec_copy,
      GST_ELEMENT (self->priv->conference),
      self->priv->rtpmuxer);

  if (changed && !error)
  {
    GList *secondary_codecs;

    FS_RTP_SESSION_LOCK (self);
    secondary_codecs = fs_rtp_special_sources_get_codecs_locked (
        self->priv->extra_sources, self->priv->codec_associations,
        codec_copy);
    FS_RTP_SESSION_UNLOCK (self);

    secondary_codecs = g_list_concat (secondary_codecs, other_codecs);

    g_object_notify (G_OBJECT (self), "current-send-codec");
    gst_element_post_message (GST_ELEMENT (self->priv->conference),
        gst_message_new_element (GST_OBJECT (self->priv->conference),
            gst_structure_new ("farsight-send-codec-changed",
                "session", FS_TYPE_SESSION, self,
                "codec", FS_TYPE_CODEC, codec_copy,
                "secondary-codecs", FS_TYPE_CODEC_LIST, secondary_codecs,
                NULL)));

    fs_codec_list_destroy (secondary_codecs);
  }

 done:
  g_clear_error (&error);
  fs_codec_destroy (send_codec_copy);
  fs_codec_destroy (codec_copy);

  /* If we have a codec bin, the required/preferred caps may have changed,
   * in this case, we need to drop the current buffer and wait for a buffer
   * with the right caps to come in. Only then can we drop the pad block
   */

  gst_pad_set_blocked_async (pad, FALSE, pad_block_do_nothing, NULL);

  g_mutex_unlock (self->priv->send_pad_blocked_mutex);
  fs_rtp_session_has_disposed_exit (self);
  return;

 done_locked:
  FS_RTP_SESSION_UNLOCK (self);
  goto done;
}

/**
 * fs_rtp_session_verify_send_codec_bin:
 *
 * Verify that the current send codec is still valid and if it is not
 * do whats required to have the right one be used.
 *
 * Does not care about locks
 *
 */

static void
fs_rtp_session_verify_send_codec_bin (FsRtpSession *self)
{
  gst_pad_set_blocked_async (self->priv->send_tee_media_pad, TRUE,
      _send_src_pad_blocked_callback, self);
}

/*
 * This callback is called when the pad of a substream has been locked because
 * the codec needs to be changed.
 *
 * It will return a new codecbin if it needs changing. If there is an error,
 * the GError * will be set.
 */

static GstElement *
_substream_get_codec_bin (FsRtpSubStream *substream,
    FsRtpStream *stream, FsCodec *current_codec, FsCodec **new_codec,
    guint current_builder_hash, guint *new_builder_hash,
    GError **error, FsRtpSession *session)
{
  GstElement *codecbin = NULL;
  gchar *name;
  CodecAssociation *ca = NULL;

  if (fs_rtp_session_has_disposed_enter (session, NULL))
    return NULL;

  FS_RTP_SESSION_LOCK (session);

  ca = fs_rtp_session_get_recv_codec_locked (session, substream->pt, stream,
      new_codec, error);
  if (!ca)
    goto out;

  name = g_strdup_printf ("recv_%d_%u_%d", session->id, substream->ssrc,
      substream->pt);
  codecbin = _create_codec_bin (ca, *new_codec, name, FALSE, NULL,
      current_builder_hash, new_builder_hash, error);
  g_free (name);

  if (!codecbin)
  {
    fs_codec_destroy (*new_codec);
    *new_codec = NULL;
  }

 out:

  fs_rtp_session_has_disposed_exit (session);

  FS_RTP_SESSION_UNLOCK (session);

  return codecbin;
}

static void
fs_rtp_session_associate_free_substreams (FsRtpSession *session,
    FsRtpStream *stream, guint32 ssrc)
{
  gboolean added = FALSE;

  FS_RTP_SESSION_LOCK (session);

  for (;;)
  {
    FsRtpSubStream *substream = NULL;
    GList *item = NULL;
    GError *error = NULL;

    for (item = g_list_first (session->priv->free_substreams);
         item;
         item = g_list_next (item))
    {
      FsRtpSubStream *localsubstream = item->data;

      GST_LOG ("Have substream with ssrc %x, looking for %x",
          localsubstream->ssrc, ssrc);
      if (ssrc == localsubstream->ssrc)
      {
        substream = localsubstream;
        session->priv->free_substreams = g_list_delete_link (
            session->priv->free_substreams, item);
        break;
      }
    }

    if (!substream)
      break;

    added = TRUE;

    while (
        g_signal_handlers_disconnect_by_func (substream, "error", session) > 0);
    while (
        g_signal_handlers_disconnect_by_func (substream, "no-rtcp-timedout",
            session) > 0);

    if (fs_rtp_stream_add_substream_unlock (stream, substream, &error))
    {
      GST_DEBUG ("Associated SSRC %x in session %u", ssrc, session->id);
    }
    else
    {
      GST_ERROR ("Could not associate a substream with its stream : %s",
          error->message);
      fs_session_emit_error (FS_SESSION (session), error->code,
          "Could not associate a substream with its stream",
          error->message);
    }
    g_clear_error (&error);
    FS_RTP_SESSION_LOCK (session);
  }
  FS_RTP_SESSION_UNLOCK (session);

  if (added == FALSE)
    GST_DEBUG ("No free substream with SSRC %x in session %u",
        ssrc, session->id);
}

void
fs_rtp_session_associate_ssrc_cname (FsRtpSession *session,
    guint32 ssrc,
    const gchar *cname)
{
  FsRtpStream *stream = NULL;
  GList *item = NULL;

  if (fs_rtp_session_has_disposed_enter (session, NULL))
    return;

  FS_RTP_SESSION_LOCK (session);

  if (!session->priv->free_substreams)
  {
    FS_RTP_SESSION_UNLOCK (session);
    fs_rtp_session_has_disposed_exit (session);
    return;
  }

  for (item = g_list_first (session->priv->streams);
       item;
       item = g_list_next (item))
  {
    FsRtpStream *localstream = item->data;
    gchar *localcname = NULL;

    g_object_get (localstream->participant, "cname", &localcname, NULL);

    if (localcname && !strcmp (localcname, cname))
    {
      stream = localstream;
      g_free (localcname);
      break;
    }
    g_free (localcname);
  }

  if (!stream)
  {
    GST_LOG ("There is no participant with cname %s, but"
        " we have streams of unknown origin", cname);
    FS_RTP_SESSION_UNLOCK (session);
    fs_rtp_session_has_disposed_exit (session);
    return;
  }

  if (!g_hash_table_lookup (session->priv->ssrc_streams,
          GUINT_TO_POINTER (ssrc)))
    g_hash_table_insert (session->priv->ssrc_streams, GUINT_TO_POINTER (ssrc),
        stream);

  g_object_ref (stream);
  FS_RTP_SESSION_UNLOCK (session);

  fs_rtp_session_associate_free_substreams (session, stream, ssrc);
  g_object_unref (stream);

  fs_rtp_session_has_disposed_exit (session);
}

static void
_substream_no_rtcp_timedout_cb (FsRtpSubStream *substream,
    FsRtpSession *session)
{
  GError *error = NULL;
  FsRtpStream *first_stream = NULL;

  if (fs_rtp_session_has_disposed_enter (session, NULL))
    return;

  FS_RTP_SESSION_LOCK (session);

  if (g_list_length (session->priv->streams) != 1)
  {
    GST_WARNING ("The substream for SSRC %x and pt %u did not receive RTCP"
        " for %d milliseconds, but we have more than one stream so we can"
        " not associate it.", substream->ssrc, substream->pt,
        substream->no_rtcp_timeout);
    FS_RTP_SESSION_UNLOCK (session);
    fs_rtp_session_has_disposed_exit (session);
    return;
  }

  if (!g_list_find (session->priv->free_substreams, substream))
  {
    GST_WARNING ("Could not find substream %p in the list of free substreams",
        substream);
    FS_RTP_SESSION_UNLOCK (session);
    fs_rtp_session_has_disposed_exit (session);
    return;
  }

  session->priv->free_substreams =
    g_list_remove (session->priv->free_substreams,
        substream);

  while (
      g_signal_handlers_disconnect_by_func (substream, "error", session) > 0);
  while (
      g_signal_handlers_disconnect_by_func (substream, "no-rtcp-timedout",
          session) > 0);

  first_stream = g_list_first (session->priv->streams)->data;
  g_object_ref (first_stream);
  if (!fs_rtp_stream_add_substream_unlock (first_stream, substream, &error))
    fs_session_emit_error (FS_SESSION (session),
        error ? error->code : FS_ERROR_INTERNAL,
        "Could not link the substream to a stream",
        error ? error->message : "No error message");
  g_clear_error (&error);
  g_object_unref (first_stream);

  fs_rtp_session_has_disposed_exit (session);
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
  if (fs_rtp_session_has_disposed_enter (session, NULL))
    return;

  /* First remove it from the known SSRCs */

  FS_RTP_SESSION_LOCK (session);
  if (!g_hash_table_lookup (session->priv->ssrc_streams_manual,
          GUINT_TO_POINTER (ssrc)))
    g_hash_table_remove (session->priv->ssrc_streams, GUINT_TO_POINTER (ssrc));
  FS_RTP_SESSION_UNLOCK (session);

  /*
   * TODO:
   *
   * Remove running substreams with that SSRC .. lets also check if they
   * come from the right ip/port/etc ??
   */

  fs_rtp_session_has_disposed_exit (session);
}


static gboolean
gather_caps_parameters (CodecAssociation *ca, GstCaps *caps)
{
  GstStructure *s = NULL;
  int i;
  gboolean old_need_config = FALSE;

  s = gst_caps_get_structure (caps, 0);

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

              GST_DEBUG ("%d/%s: replacing param %s=%s with %s",
                  ca->codec->id, ca->codec->encoding_name, name, param->value, value);

              /* replace the value if its different */
              fs_codec_remove_optional_parameter (ca->codec, param);
              fs_codec_add_optional_parameter (ca->codec, name, value);
              break;
            }
          }

          /* Add it if it wasn't there */
          if (item == NULL)
          {
            GST_DEBUG ("%d/%s: adding param %s=%s",
                ca->codec->id, ca->codec->encoding_name, name, value);

            fs_codec_add_optional_parameter (ca->codec, name, value);
          }
        }
      }
    }
  }

  old_need_config = ca->need_config;
  ca->need_config = FALSE;

  return old_need_config;
}

static void
_send_caps_changed (GstPad *pad, GParamSpec *pspec, FsRtpSession *session)
{
  GstCaps *caps = NULL;
  CodecAssociation *ca = NULL;

  g_object_get (pad, "caps", &caps, NULL);

  if (!caps)
    return;

  g_return_if_fail (GST_CAPS_IS_SIMPLE(caps));

  if (fs_rtp_session_has_disposed_enter (session, NULL))
  {
    gst_caps_unref (caps);
    return;
  }

  FS_RTP_SESSION_LOCK (session);

  if (!session->priv->current_send_codec)
    goto out;

  ca = lookup_codec_association_by_codec (session->priv->codec_associations,
      session->priv->current_send_codec);

  if (!ca)
    goto out;

  /*
   * Emit farsight-codecs-changed if the sending thread finds the config
   * for the last codec that needed it
   */
  if (gather_caps_parameters (ca, caps))
  {
    GList *item = NULL;

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
      FS_RTP_SESSION_UNLOCK (session);
      g_object_notify (G_OBJECT (session), "codecs-ready");
      g_object_notify (G_OBJECT (session), "codecs");
      gst_element_post_message (GST_ELEMENT (session->priv->conference),
          gst_message_new_element (GST_OBJECT (session->priv->conference),
              gst_structure_new ("farsight-codecs-changed",
                  "session", FS_TYPE_SESSION, session,
                  NULL)));

      goto out_unlocked;
    }

  }

 out:

  FS_RTP_SESSION_UNLOCK (session);

 out_unlocked:

  gst_caps_unref (caps);

  fs_rtp_session_has_disposed_exit (session);
}

static void
_discovery_caps_changed (GstPad *pad, GParamSpec *pspec, FsRtpSession *session)
{
  CodecAssociation *ca = NULL;
  GstCaps *caps = NULL;
  gboolean block = TRUE;

  g_object_get (pad, "caps", &caps, NULL);

  if (!caps)
    return;

  g_return_if_fail (GST_CAPS_IS_SIMPLE(caps));

  if (fs_rtp_session_has_disposed_enter (session, NULL))
  {
    gst_caps_unref (caps);
    return;
  }

  FS_RTP_SESSION_LOCK (session);

  /* If there is no codec, its because we're shutting down */
  if (!session->priv->discovery_codec)
  {
    GST_DEBUG ("Got caps while discovery is stopping");
    goto out;
  }

  ca = lookup_codec_association_by_codec_for_sending (
      session->priv->codec_associations,
      session->priv->discovery_codec);

  if (ca && ca->need_config)
  {
    gather_caps_parameters (ca, caps);
    fs_codec_destroy (session->priv->discovery_codec);
    session->priv->discovery_codec = fs_codec_copy (ca->codec);
    block = !ca->need_config;
  }

 out:

  FS_RTP_SESSION_UNLOCK (session);

  gst_caps_unref (caps);

  if (block)
    gst_pad_set_blocked_async (session->priv->send_tee_discovery_pad, TRUE,
        _discovery_pad_blocked_callback, session);
  fs_rtp_session_has_disposed_exit (session);
}

/**
 * fs_rtp_session_get_codec_params_unlock:
 * @session: a #FsRtpSession
 * @ca: the #CodecAssociaton to get params for
 *
 * Gets the parameters for the specified #CodecAssociation
 *
 * Returns: %TRUE on success, %FALSE on error
 */

static gboolean
fs_rtp_session_get_codec_params_unlock (FsRtpSession *session,
    CodecAssociation *ca, GError **error)
{
  GstPad *pad = NULL;
  gchar *tmp;
  GstCaps *caps;
  FsCodec *discovery_codec = fs_codec_copy (ca->codec);
  FsCodec *send_codec = fs_codec_copy (ca->send_codec);
  GstElement *codecbin = NULL;

  GST_LOG ("Gathering params for codec " FS_CODEC_FORMAT,
      FS_CODEC_ARGS (ca->send_codec));

  fs_codec_destroy (session->priv->discovery_codec);
  session->priv->discovery_codec = NULL;

  tmp = g_strdup_printf ("discover_%d_%d", session->id, ca->send_codec->id);
  codecbin = _create_codec_bin (ca, ca->send_codec, tmp, TRUE, NULL,
      0, NULL, error);
  g_free (tmp);

  FS_RTP_SESSION_UNLOCK (session);
  /* Invalidate CA because we've just unlocked */
  ca = NULL;

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

    if (!gst_bin_add (GST_BIN (session->priv->conference),
            session->priv->discovery_capsfilter))
    {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not add the discovery capsfilter to the bin");
      goto error;
    }

    if (!gst_element_sync_state_with_parent (
            session->priv->discovery_capsfilter))
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
    g_signal_connect_object (pad, "notify::caps",
        G_CALLBACK (_discovery_caps_changed), session, 0);
    gst_object_unref (pad);
  }

  if (!codecbin)
    goto error;

  session->priv->discovery_codecbin = codecbin;
  codecbin = NULL;

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

  caps = fs_codec_to_gst_caps (send_codec);
  g_object_set (session->priv->discovery_capsfilter,
      "caps", caps,
      NULL);
  gst_caps_unref (caps);


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

  fs_codec_destroy (send_codec);
  session->priv->discovery_codec = discovery_codec;

  return TRUE;

 error:

  fs_codec_destroy (send_codec);
  fs_codec_destroy (discovery_codec);

  if (codecbin)
    gst_object_unref (codecbin);

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

  return FALSE;
}

/**
 * _discovery_pad_blocked_callback:
 *
 * This is the callback to change the discovery codecbin
 */

static void
_discovery_pad_blocked_callback (GstPad *pad, gboolean blocked,
    gpointer user_data)
{
  FsRtpSession *session = user_data;
  GError *error = NULL;
  GList *item = NULL;
  CodecAssociation *ca = NULL;

  if (fs_rtp_session_has_disposed_enter (session, NULL))
  {
    gst_pad_set_blocked_async (pad, FALSE, pad_block_do_nothing, NULL);
    return;
  }

  g_mutex_lock (session->priv->discovery_pad_blocked_mutex);

  FS_RTP_SESSION_LOCK (session);

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
    fs_rtp_session_stop_codec_param_gathering_unlock (session);

    g_object_notify (G_OBJECT (session), "codecs-ready");
    gst_element_post_message (GST_ELEMENT (session->priv->conference),
        gst_message_new_element (GST_OBJECT (session->priv->conference),
            gst_structure_new ("farsight-codecs-changed",
                "session", FS_TYPE_SESSION, session,
                NULL)));

    goto out_unlocked;
  }

  if (fs_codec_are_equal (ca->codec, session->priv->discovery_codec))
    goto out_locked;

  if (!fs_rtp_session_get_codec_params_unlock (session, ca, &error))
  {
    FS_RTP_SESSION_LOCK (session);
    fs_rtp_session_stop_codec_param_gathering_unlock (session);
    fs_session_emit_error (FS_SESSION (session), error->code,
        "Error while discovering codec data, discovery cancelled",
        error->message);
  }

  g_clear_error (&error);

 out_unlocked:
  gst_pad_set_blocked_async (pad, FALSE, pad_block_do_nothing, NULL);
  g_mutex_unlock (session->priv->discovery_pad_blocked_mutex);
  fs_rtp_session_has_disposed_exit (session);
  return;

 out_locked:
  FS_RTP_SESSION_UNLOCK (session);
  goto out_unlocked;
}

/**
 * fs_rtp_session_start_codec_param_gathering_locked
 * @session: a #FsRtpSession
 *
 * Check if there is any codec associations that requires codec discovery and
 * if there is, starts the gathering process by adding a pad block to the
 * tee's discovery src pad
 */

static void
fs_rtp_session_start_codec_param_gathering_locked (FsRtpSession *session)
{
  GList *item = NULL;

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
    return;

  GST_DEBUG ("Starting Codec Param discovery for session %d", session->id);

  gst_pad_set_blocked_async (session->priv->send_tee_discovery_pad, TRUE,
      _discovery_pad_blocked_callback, session);
}


/**
 * fs_rtp_session_stop_codec_param_gathering_unlock:
 * @session: a #FsRtpSession
 *
 * Stop the codec config gathering and remove the elements used for that
 */

static void
fs_rtp_session_stop_codec_param_gathering_unlock (FsRtpSession *session)
{

  GST_DEBUG ("Stopping Codec Param discovery for session %d", session->id);

  if (session->priv->discovery_codec)
  {
    fs_codec_destroy (session->priv->discovery_codec);
    session->priv->discovery_codec = NULL;
  }

  FS_RTP_SESSION_UNLOCK (session);

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
}

static gchar **
fs_rtp_session_list_transmitters (FsSession *session)
{
  gchar **rv;

  g_return_val_if_fail (FS_IS_RTP_SESSION (session), NULL);

  rv = fs_transmitter_list_available ();

  if (!rv)
    rv = g_malloc0 (1);

  return rv;
}


static GType
fs_rtp_session_get_stream_transmitter_type (FsSession *session,
    const gchar *transmitter)
{
  FsRtpSession *self = FS_RTP_SESSION (session);
  GType st_type = 0;
  FsTransmitter *trans;

  trans = fs_rtp_session_get_transmitter (self, transmitter, NULL);

  if (transmitter)
    st_type = fs_transmitter_get_stream_transmitter_type (trans);

  g_object_unref (trans);

  return st_type;
}


void
fs_rtp_session_ssrc_validated (FsRtpSession *session,
    guint32 ssrc)
{
  if (fs_rtp_session_has_disposed_enter (session, NULL))
    return;

  gst_element_send_event (session->priv->rtpmuxer,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("GstForceKeyUnit",
              "all-headers", G_TYPE_BOOLEAN, TRUE,
              NULL)));

  fs_rtp_session_has_disposed_exit (session);
}

struct CodecBinSetBitrateData
{
  guint bitrate;
  gboolean ret;
};

static void
codecbin_set_bitrate_func (gpointer e, gpointer user_data)
{
  GstElement *elem = e;
  struct CodecBinSetBitrateData *data = user_data;

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (elem), "bitrate"))
  {
    fs_utils_set_bitrate (elem, data->bitrate);
    data->ret = TRUE;
  }

  gst_object_unref (elem);
}

static gboolean
codecbin_set_bitrate (GstElement *codecbin, guint bitrate)
{
  GstIterator *it;
  struct CodecBinSetBitrateData data;

  if (bitrate == 0)
    return FALSE;

  data.bitrate = bitrate;
  data.ret = FALSE;

  it = gst_bin_iterate_recurse (GST_BIN (codecbin));
  gst_iterator_foreach (it, codecbin_set_bitrate_func, &data);
  gst_iterator_free (it);

  return data.ret;
}

static void
fs_rtp_session_set_send_bitrate (FsRtpSession *self, guint bitrate)
{
  FS_RTP_SESSION_LOCK (self);

  if (bitrate)
    self->priv->send_bitrate = bitrate;

  if (self->priv->send_codecbin)
    codecbin_set_bitrate (self->priv->send_codecbin, bitrate);

  FS_RTP_SESSION_UNLOCK (self);
}


static GList *
fs_rtp_session_get_codecs_need_resend (FsSession *session,
    GList *old_codecs, GList *new_codecs)
{
  g_return_val_if_fail (FS_IS_RTP_SESSION (session), FALSE);

  return codecs_list_has_codec_config_changed (old_codecs, new_codecs);
}

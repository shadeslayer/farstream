/*
 * Farsight2 - Farsight RTP Sub Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-substream.c - A Farsight RTP Substream gobject
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

#include "fs-rtp-substream.h"
#include "fs-rtp-stream.h"

#include <gst/farsight/fs-stream.h>
#include <gst/farsight/fs-session.h>

#define GST_CAT_DEFAULT fsrtpconference_debug

/**
 * SECTION:fs-rtp-sub-stream
 * @short_description: The receive codec bin for a ssrc and a pt
 *
 * This object controls a part of the receive pipeline, with the following shape
 *
 * rtpbin_pad -> codecbin -> valve  -> output_ghostad
 *
 */

/* signals */
enum
{
  NO_RTCP_TIMEDOUT,
  SRC_PAD_ADDED,
  ERROR,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_CONFERENCE,
  PROP_SESSION,
  PROP_STREAM,
  PROP_RTPBIN_PAD,
  PROP_SSRC,
  PROP_PT,
  PROP_CODEC,
  PROP_RECEIVING,
  PROP_OUTPUT_GHOSTPAD,
  PROP_NO_RTCP_TIMEOUT
};

#define DEFAULT_NO_RTCP_TIMEOUT (7000)

struct _FsRtpSubStreamPrivate {
  gboolean disposed;

  /* These are only pointers, we don't own references */
  FsRtpConference *conference;
  FsRtpSession *session;
  FsRtpStream *stream; /* only set once, protected by session lock */

  guint32 ssrc;
  guint pt;

  GstPad *rtpbin_pad;

  GstElement *valve;

  /* This only exists if the codec is valid,
   * otherwise the rtpbin_pad is blocked */
  /* Protected by the session mutex */
  GstElement *codecbin;
  FsCodec *codec;

  /* This is only created when the substream is associated with a FsRtpStream */
  GstPad *output_ghostpad;

  /* The id of the pad probe used to block the stream while the recv codec
   * is changed
   * Protected by the session mutex
   */
  gulong blocking_id;

  gboolean receiving;

  gint no_rtcp_timeout;

  /* Protected by the session mutex */
  gint no_rtcp_timeout_id;

  GError *construction_error;
};

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(FsRtpSubStream, fs_rtp_sub_stream, G_TYPE_OBJECT);

#define FS_RTP_SUB_STREAM_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_SUB_STREAM,             \
   FsRtpSubStreamPrivate))

static void fs_rtp_sub_stream_dispose (GObject *object);
static void fs_rtp_sub_stream_finalize (GObject *object);
static void fs_rtp_sub_stream_constructed (GObject *object);

static void fs_rtp_sub_stream_get_property (GObject *object, guint prop_id,
  GValue *value, GParamSpec *pspec);
static void fs_rtp_sub_stream_set_property (GObject *object, guint prop_id,
  const GValue *value, GParamSpec *pspec);

static void
fs_rtp_sub_stream_emit_error (FsRtpSubStream *substream,
    gint error_no,
    gchar *error_msg,
    gchar *debug_msg);

static void
fs_rtp_sub_stream_class_init (FsRtpSubStreamClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = fs_rtp_sub_stream_parent_class;

  gobject_class->constructed = fs_rtp_sub_stream_constructed;
  gobject_class->dispose = fs_rtp_sub_stream_dispose;
  gobject_class->finalize = fs_rtp_sub_stream_finalize;
  gobject_class->set_property = fs_rtp_sub_stream_set_property;
  gobject_class->get_property = fs_rtp_sub_stream_get_property;

  g_object_class_install_property (gobject_class,
    PROP_CONFERENCE,
    g_param_spec_object ("conference",
      "The FsRtpConference this substream stream refers to",
      "This is a convience pointer for the Conference",
      FS_TYPE_RTP_CONFERENCE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
    PROP_SESSION,
    g_param_spec_object ("session",
      "The FsRtpSession this substream stream refers to",
      "This is a convience pointer for the parent FsRtpSession",
      FS_TYPE_RTP_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));


  g_object_class_install_property (gobject_class,
    PROP_STREAM,
    g_param_spec_object ("stream",
      "The FsRtpStream this substream stream refers to",
      "This is a convience pointer for the parent FsRtpStream",
      FS_TYPE_RTP_STREAM,
      G_PARAM_READWRITE));


  g_object_class_install_property (gobject_class,
    PROP_RTPBIN_PAD,
    g_param_spec_object ("rtpbin-pad",
      "The GstPad this substrea is linked to",
      "This is the pad on which this substream will attach itself",
      GST_TYPE_PAD,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));


  g_object_class_install_property (gobject_class,
    PROP_SSRC,
    g_param_spec_uint ("ssrc",
      "The ssrc this stream is used for",
      "This is the SSRC from the pad",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
    PROP_PT,
    g_param_spec_uint ("pt",
      "The payload type this stream is used for",
      "This is the payload type from the pad",
      0, 128, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
    PROP_CODEC,
    g_param_spec_boxed ("codec",
      "The FsCodec this substream is received",
      "The FsCodec currently received from this substream",
      FS_TYPE_CODEC,
      G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
      PROP_RECEIVING,
      g_param_spec_boolean ("receiving",
          "Whether this substream will receive any data",
          "A toggle that prevents the substream from outputting any data",
          TRUE,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_OUTPUT_GHOSTPAD,
      g_param_spec_object ("output-ghostpad",
          "The output ghostpad for this substream",
          "The GstPad which is on the outside of the fsrtpconference element"
          " for this substream",
          GST_TYPE_PAD,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
      PROP_NO_RTCP_TIMEOUT,
      g_param_spec_int ("no-rtcp-timeout",
          "The timeout (in ms) before no RTCP is assumed",
          "This is the time (in ms) after which data received without RTCP"
          " is attached the FsStream, this only works if there is only one"
          " FsStream. <=0 will do nothing",
          -1, G_MAXINT, DEFAULT_NO_RTCP_TIMEOUT,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));


  /**
   * FsRtpSubStream::no-rtcp-timedout:
   * @self: #FsSubStream that emitted the signal
   *
   * This signal is emitted after the timeout specified by
   * #FsRtpSubStream:no-rtcp-timeout if this sub-stream has not been attached
   * to a stream.
   *
   */
  signals[NO_RTCP_TIMEDOUT] = g_signal_new ("no-rtcp-timedout",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  /**
   * FsRtpSubStream::src-pad-added:
   * @self: #FsRtpSubStream that emitted the signal
   * @pad: #GstPad of the new source pad
   * @codec: #FsCodec of the codec being received on the new source pad
   *
   * This signal is emitted when a new gst source pad has been created for a
   * specific codec being received. There will be a different source pad for
   * each codec that is received. The user must ref the #GstPad if he wants to
   * keep it. The user should not modify the #FsCodec and must copy it if he
   * wants to use it outside the callback scope.
   *
   * This signal is not emitted on the main thread, but on GStreamer's streaming
   * thread!
   *
   * This is probably re-emited by the FsStream
   *
   */
  signals[SRC_PAD_ADDED] = g_signal_new ("src-pad-added",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_rtp_marshal_VOID__BOXED_BOXED,
      G_TYPE_NONE, 2, GST_TYPE_PAD, FS_TYPE_CODEC);

  /**
   * FsRtpSubStream::error:
   * @self: #FsRtpSubStream that emitted the signal
   * @errorno: The number of the error
   * @error_msg: Error message to be displayed to user
   * @debug_msg: Debugging error message
   *
   * This signal is emitted in any error condition
   *
   */
  signals[ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_rtp_marshal_VOID__INT_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);


  g_type_class_add_private (klass, sizeof (FsRtpSubStreamPrivate));
}


static void
fs_rtp_sub_stream_init (FsRtpSubStream *self)
{
  self->priv = FS_RTP_SUB_STREAM_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
  self->priv->receiving = TRUE;
}

static gboolean
_no_rtcp_timeout (gpointer user_data)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (user_data);

  FS_RTP_SESSION_LOCK (self->priv->session);

  if (self->priv->no_rtcp_timeout_id)
    self->priv->no_rtcp_timeout_id = 0;

  FS_RTP_SESSION_UNLOCK (self->priv->session);

  g_signal_emit (self, signals[NO_RTCP_TIMEDOUT], 0);

  return FALSE;
}

static void
fs_rtp_sub_stream_constructed (GObject *object)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  GST_DEBUG ("New substream in session %u for ssrc %x and pt %u",
      self->priv->session->id, self->priv->ssrc, self->priv->pt);

  if (!self->priv->conference) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_INVALID_ARGUMENTS, "A Substream needs a conference object");
    return;
  }

  self->priv->valve = gst_element_factory_make ("fsvalve", NULL);

  if (!self->priv->valve) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not create a fsvalve element for"
      " session substream with ssrc: %u and pt:%d", self->priv->ssrc,
      self->priv->pt);
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->valve)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not add the fsvalve element for session"
      " substream with ssrc: %u and pt:%d to the conference bin",
      self->priv->ssrc, self->priv->pt);
    return;
  }

  /* We set the valve to dropping, the stream will unblock it when its linked */
  g_object_set (self->priv->valve,
      "drop", TRUE,
      NULL);


  if (gst_element_set_state (self->priv->valve, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not set the fsvalve element for session"
      " substream with ssrc: %u and pt:%d to the playing state",
      self->priv->ssrc, self->priv->pt);
    return;
  }

  if (self->priv->no_rtcp_timeout > 0)
    self->priv->no_rtcp_timeout_id = g_timeout_add (self->priv->no_rtcp_timeout,
        _no_rtcp_timeout, self);
}


static void
fs_rtp_sub_stream_dispose (GObject *object)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  if (self->priv->disposed)
    return;


  FS_RTP_SESSION_LOCK (self->priv->session);
  if (self->priv->no_rtcp_timeout_id)
  {
    g_source_remove (self->priv->no_rtcp_timeout_id);
    self->priv->no_rtcp_timeout_id = 0;
  }
  FS_RTP_SESSION_UNLOCK (self->priv->session);


  if (self->priv->output_ghostpad) {
    gst_element_remove_pad (GST_ELEMENT (self->priv->conference),
      self->priv->output_ghostpad);
    self->priv->output_ghostpad = NULL;
  }

  if (self->priv->valve) {
    gst_object_ref (self->priv->valve);
    gst_element_set_state (self->priv->valve, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self->priv->conference), self->priv->valve);
    gst_element_set_state (self->priv->valve, GST_STATE_NULL);
    gst_object_unref (self->priv->valve);
    self->priv->valve = NULL;
  }


  FS_RTP_SESSION_LOCK (self->priv->session);

  if (self->priv->blocking_id)
  {
    gst_pad_remove_data_probe (self->priv->rtpbin_pad,
        self->priv->blocking_id);
    self->priv->blocking_id = 0;
  }

  if (self->priv->codecbin) {
    gst_object_ref (self->priv->codecbin);
    gst_element_set_state (self->priv->codecbin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self->priv->conference), self->priv->codecbin);
    gst_element_set_state (self->priv->codecbin, GST_STATE_NULL);
    gst_object_unref (self->priv->codecbin);
    self->priv->codecbin = NULL;
  }

  FS_RTP_SESSION_UNLOCK (self->priv->session);


  if (self->priv->rtpbin_pad) {
    gst_object_unref (self->priv->rtpbin_pad);
    self->priv->rtpbin_pad = NULL;
  }

  self->priv->disposed = TRUE;
  G_OBJECT_CLASS (fs_rtp_sub_stream_parent_class)->dispose (object);
}

static void
fs_rtp_sub_stream_finalize (GObject *object)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  if (self->priv->codec)
    fs_codec_destroy (self->priv->codec);

  G_OBJECT_CLASS (fs_rtp_sub_stream_parent_class)->finalize (object);
}



static void
fs_rtp_sub_stream_set_property (GObject *object,
                                guint prop_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  switch (prop_id) {
    case PROP_CONFERENCE:
      self->priv->conference = g_value_get_object (value);
      break;
    case PROP_SESSION:
      self->priv->session = g_value_get_object (value);
      break;
    case PROP_STREAM:
      FS_RTP_SESSION_LOCK (self->priv->session);
      if (self->priv->stream)
        GST_WARNING ("Stream already set, not re-setting");
      else
        self->priv->stream = g_value_get_object (value);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    case PROP_RTPBIN_PAD:
      self->priv->rtpbin_pad = GST_PAD (g_value_dup_object (value));
      break;
    case PROP_SSRC:
      self->priv->ssrc = g_value_get_uint (value);
      break;
    case PROP_PT:
      self->priv->pt = g_value_get_uint (value);
      break;
    case PROP_RECEIVING:
      FS_RTP_SESSION_LOCK (self->priv->session);
      self->priv->receiving = g_value_get_boolean (value);
      if (self->priv->output_ghostpad && self->priv->valve)
        g_object_set (G_OBJECT (self->priv->valve),
            "drop", !self->priv->receiving,
            NULL);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    case PROP_NO_RTCP_TIMEOUT:
      FS_RTP_SESSION_LOCK (self->priv->session);
      self->priv->no_rtcp_timeout = g_value_get_int (value);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
fs_rtp_sub_stream_get_property (GObject *object,
                                guint prop_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  switch (prop_id) {
    case PROP_CONFERENCE:
      g_value_set_object (value, self->priv->conference);
      break;
    case PROP_SESSION:
      g_value_set_object (value, self->priv->session);
      break;
    case PROP_STREAM:
      FS_RTP_SESSION_LOCK (self->priv->session);
      g_value_set_object (value, self->priv->stream);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    case PROP_RTPBIN_PAD:
      g_value_set_object (value, self->priv->rtpbin_pad);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, self->priv->ssrc);
      break;
    case PROP_PT:
      g_value_set_uint (value, self->priv->pt);
      break;
    case PROP_CODEC:
      FS_RTP_SESSION_LOCK (self->priv->session);
      g_value_set_boxed (value, self->priv->codec);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    case PROP_RECEIVING:
      FS_RTP_SESSION_LOCK (self->priv->session);
      g_value_set_boolean (value, self->priv->receiving);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
    case PROP_OUTPUT_GHOSTPAD:
      FS_RTP_SESSION_LOCK (self->priv->session);
      g_value_set_object (value, self->priv->output_ghostpad);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    case PROP_NO_RTCP_TIMEOUT:
      FS_RTP_SESSION_LOCK (self->priv->session);
      g_value_set_int (value, self->priv->no_rtcp_timeout);
      FS_RTP_SESSION_UNLOCK (self->priv->session);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/**
 * fs_rtp_session_add_codecbin_locked:
 * @substream: a #FsRtpSubStream
 *
 * Add and links the rtpbin for a given substream.
 *
 * The caller MUST hold the session lock
 *
 * Returns: TRUE on success
 */

static gboolean
fs_rtp_sub_stream_add_codecbin_locked (FsRtpSubStream *substream,
    GError **error)
{
  GstPad *codec_bin_sink_pad;
  GstPadLinkReturn linkret;
  FsCodec *codec = NULL;
  GstElement *codecbin;

  if (substream->priv->codecbin)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "There already is a codec bin for this substream");
    goto error_no_remove;
  }

  codecbin = fs_rtp_session_new_recv_codec_bin_locked (
      substream->priv->session,
      substream->priv->ssrc, substream->priv->pt, &codec, error);

  if (!codecbin) {
    goto error_no_remove;
  }

  if (!gst_bin_add (GST_BIN (substream->priv->conference), codecbin))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the codec bin to the conference");
    goto error_no_remove;
  }

  if (gst_element_set_state (codecbin, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not set the codec bin to the playing state");
    goto error;
  }

  if (!gst_element_link_pads (codecbin, "src",
      substream->priv->valve, "sink"))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not link the codec bin to the valve");
    goto error;
  }

  codec_bin_sink_pad = gst_element_get_static_pad (codecbin, "sink");
  if (!codec_bin_sink_pad)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not get the codecbin's sink pad");
    goto error;
  }

  linkret = gst_pad_link (substream->priv->rtpbin_pad, codec_bin_sink_pad);

  gst_object_unref (codec_bin_sink_pad);

  if (GST_PAD_LINK_FAILED (linkret))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not link the rtpbin to the codec bin (%d)", linkret);
    goto error;
  }

  substream->priv->codecbin = codecbin;
  substream->priv->codec = codec;

  if (substream->priv->stream)
  {
    gboolean ret = TRUE;

    if (!substream->priv->output_ghostpad)
      ret =  fs_rtp_sub_stream_add_output_ghostpad_locked (substream, error);

    fs_rtp_stream_maybe_emit_codecs_changed (substream->priv->stream,
        substream);

    return ret;
  }
  else
  {
    return TRUE;
  }

  /* Announce the pad if it wasnt there already and this substream
   * has a stream
   */
  if (!substream->priv->output_ghostpad &&
      substream->priv->stream)
    return fs_rtp_sub_stream_add_output_ghostpad_locked (substream, error);
  else
    return TRUE;

 error:
    gst_element_set_state (codecbin, GST_STATE_NULL);
    gst_object_ref (codecbin);
    gst_bin_remove (GST_BIN (substream->priv->conference), codecbin);

 error_no_remove:

    fs_rtp_sub_stream_invalidate_codec_locked (substream, substream->priv->pt,
        NULL);

    return FALSE;
}

/**
 * fs_rtp_sub_stream_add_codecbin:
 * @substream: a #FsRtpSubStream
 *
 * Add and links the rtpbin for a given substream.
 *
 * MT safe.
 *
 * Returns: TRUE on success
 */

gboolean
fs_rtp_sub_stream_add_codecbin (FsRtpSubStream *substream,
    GError **error)
{
  gboolean ret;

  FS_RTP_SESSION_LOCK (substream->priv->session);
  ret = fs_rtp_sub_stream_add_codecbin_locked (substream, error);
  FS_RTP_SESSION_UNLOCK (substream->priv->session);

  return ret;
}

FsRtpSubStream *
fs_rtp_sub_stream_new (FsRtpConference *conference,
    FsRtpSession *session,
    GstPad *rtpbin_pad,
    guint32 ssrc,
    guint pt,
    gint no_rtcp_timeout,
    GError **error)
{
  FsRtpSubStream *substream = g_object_new (FS_TYPE_RTP_SUB_STREAM,
    "conference", conference,
    "session", session,
    "rtpbin-pad", rtpbin_pad,
    "ssrc", ssrc,
    "pt", pt,
    "no-rtcp-timeout", no_rtcp_timeout,
    NULL);

  if (substream->priv->construction_error) {
    g_propagate_error (error, substream->priv->construction_error);
    g_object_unref (substream);
    return NULL;
  }

  return substream;
}

/**
 * fs_rtp_sub_stream_stop:
 *
 * Stops all of the elements on a #FsRtpSubstream
 */

void
fs_rtp_sub_stream_stop (FsRtpSubStream *substream)
{
  if (substream->priv->output_ghostpad)
    gst_pad_set_active (substream->priv->output_ghostpad, FALSE);

  if (substream->priv->valve)
    gst_element_set_state (substream->priv->valve, GST_STATE_NULL);

  FS_RTP_SESSION_LOCK (substream->priv->session);
  if (substream->priv->codecbin)
    gst_element_set_state (substream->priv->codecbin, GST_STATE_NULL);
  FS_RTP_SESSION_UNLOCK (substream->priv->session);
}


/**
 * fs_rtp_sub_stream_get_output_ghostpad:
 *
 * Creates and adds an output ghostpad for this substreams
 *
 * The caller MUST hold the session lock
 *
 * Returns: TRUE on Success, FALSE on error
 */

gboolean
fs_rtp_sub_stream_add_output_ghostpad_locked (FsRtpSubStream *substream,
    GError **error)
{
  GstPad *valve_srcpad;
  gchar *padname = NULL;
  guint session_id;
  GstPad *ghostpad = NULL;

  g_assert (substream->priv->output_ghostpad == NULL);

  g_object_get (substream->priv->session, "id", &session_id, NULL);

  padname = g_strdup_printf ("src_%u_%u_%d", session_id,
      substream->priv->ssrc,
      substream->priv->pt);

  valve_srcpad = gst_element_get_static_pad (substream->priv->valve,
      "src");
  g_assert (valve_srcpad);

  ghostpad = gst_ghost_pad_new_from_template (padname, valve_srcpad,
      gst_element_class_get_pad_template (
          GST_ELEMENT_GET_CLASS (substream->priv->conference),
          "src_%d_%d_%d"));

  gst_object_unref (valve_srcpad);
  g_free (padname);

  if (!ghostpad)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not build ghostpad src_%u_%u_%d", session_id,
        substream->priv->ssrc, substream->priv->pt);
    return FALSE;
  }

  if (!gst_pad_set_active (ghostpad, TRUE))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not activate the src_%u_%u_%d", session_id,
        substream->priv->ssrc, substream->priv->pt);
    gst_object_unref (ghostpad);
    return FALSE;
  }

  if (!gst_element_add_pad (GST_ELEMENT (substream->priv->conference),
          ghostpad))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could add build ghostpad src_%u_%u_%d to the conference",
        session_id, substream->priv->ssrc, substream->priv->pt);
    gst_object_unref (ghostpad);
    return FALSE;
  }

  substream->priv->output_ghostpad = ghostpad;

  g_signal_emit (substream, signals[SRC_PAD_ADDED], 0,
                 ghostpad, substream->priv->codec);

  if (substream->priv->receiving)
    g_object_set (substream->priv->valve, "drop", FALSE, NULL);

  return TRUE;
}

/**
 *_rtpbin_pad_have_data_callback:
 *
 * This is the pad probe callback on the sink pad of the rtpbin.
 * It is used to replace the codec bin when the recv codec has been changed.
 *
 * Its a callback, it returns TRUE to let the data through and FALSE to drop it
 * (See the "have-data" signal documentation of #GstPad).
 */

static gboolean
_rtpbin_pad_have_data_callback (GstPad *pad, GstMiniObject *miniobj,
    gpointer user_data)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (user_data);
  FsCodec *codec = NULL;
  gboolean ret = TRUE;
  GError *error = NULL;
  gboolean success = FALSE;

  FS_RTP_SESSION_LOCK (self->priv->session);

  codec = fs_rtp_session_get_recv_codec_for_pt (self->priv->session,
      self->priv->pt);

  if (!codec)
  {
    gchar *str = g_strdup_printf ("Could not get the new recv codec for"
        " pt %d", self->priv->pt);
    fs_rtp_sub_stream_emit_error (self, FS_ERROR_UNKNOWN_CODEC, str,
        str);
    goto done;
  }

  g_clear_error (&error);

  if (fs_codec_are_equal (codec, self->priv->codec))
  {
    success = TRUE;
    goto done;
  }


  if (gst_element_set_state (self->priv->codecbin, GST_STATE_NULL) !=
      GST_STATE_CHANGE_SUCCESS)
  {
    gchar *str = g_strdup_printf ("Could not set the codec bin for ssrc %u"
        " and payload type %d to the state NULL", self->priv->ssrc,
        self->priv->pt);

    fs_rtp_sub_stream_emit_error (self, FS_ERROR_INTERNAL,
        "Could not set the codec bin to NULL", str);
    g_free (str);
    goto done;
  }

  gst_bin_remove (GST_BIN (self->priv->conference), self->priv->codecbin);
  self->priv->codecbin = NULL;

  fs_codec_destroy (self->priv->codec);
  self->priv->codec = NULL;


  if (!fs_rtp_sub_stream_add_codecbin_locked (self, &error))
  {
    gchar *str = g_strdup_printf ("Could not add the new recv codec bin for"
        " ssrc %u and payload type %d to the state NULL", self->priv->ssrc,
        self->priv->pt);

    fs_rtp_sub_stream_emit_error (self, FS_ERROR_CONSTRUCTION,
        "Could not add the new recv codec bin", str);
    g_free (str);
    goto done;
  }

  g_clear_error (&error);

  success = TRUE;

 done:
  if (success && GST_IS_BUFFER (miniobj))
  {
    GstCaps *caps = fs_codec_to_gst_caps (codec);
    GstCaps *intersection = gst_caps_intersect (GST_BUFFER_CAPS (miniobj),
        caps);

    if (gst_caps_is_empty (intersection))
      ret = FALSE;
    gst_caps_unref (intersection);
    gst_caps_unref (caps);
  }

  if (ret && self->priv->blocking_id)
  {
    gst_pad_remove_data_probe (pad, self->priv->blocking_id);
    self->priv->blocking_id = 0;
  }

  FS_RTP_SESSION_UNLOCK (self->priv->session);

  return ret;
}

/**
 * fs_rtp_sub_stream_invalidate_codec_locked:
 * @substream: A #FsRtpSubStream
 * @pt: The payload type to invalidate (does nothing if it does not match)
 * @codec: The new fscodec (the substream is invalidated if it not using this
 *  codec). You can pass NULL to match any codec.
 *
 * This function will start the process that invalidates the codec
 * for this rtpbin, if it doesnt match the passed codec
 *
 * You must hold the session lock to call it.
 */

void
fs_rtp_sub_stream_invalidate_codec_locked (FsRtpSubStream *substream, gint pt,
    const FsCodec *codec)
{
  if (substream->priv->pt == pt &&
      substream->priv->codec &&
      !substream->priv->blocking_id &&
      (!codec || !fs_codec_are_equal (substream->priv->codec, codec)))
    substream->priv->blocking_id = gst_pad_add_data_probe (
        substream->priv->rtpbin_pad,
        G_CALLBACK (_rtpbin_pad_have_data_callback), substream);
}


static void
fs_rtp_sub_stream_emit_error (FsRtpSubStream *substream,
    gint error_no,
    gchar *error_msg,
    gchar *debug_msg)
{
  g_signal_emit (substream, signals[ERROR], 0, error_no, error_msg, debug_msg);
}

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
 * rtpbin_pad -> capsfilter -> codecbin -> valve  -> output_ghostad
 *
 */

/* signals */
enum
{
  NO_RTCP_TIMEDOUT,
  SRC_PAD_ADDED,
  CODEC_CHANGED,
  ERROR_SIGNAL,
  BLOCKED,
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

  GstElement *capsfilter;

  /* This only exists if the codec is valid,
   * otherwise the rtpbin_pad is blocked */
  /* Protected by the session mutex */
  GstElement *codecbin;
  FsCodec *codec;
  GstCaps *caps;

  /* This is only created when the substream is associated with a FsRtpStream */
  GstPad *output_ghostpad;

  /* The id of the pad probe used to block the stream while the recv codec
   * is changed
   * Protected by the session mutex
   */
  gulong blocking_id;

  gboolean receiving;

  /* Protected by the this mutex */
  GMutex *mutex;
  GstClockID no_rtcp_timeout_id;
  GstClockTime next_no_rtcp_timeout;
  GThread *no_rtcp_timeout_thread;

  /* Protected by the session mutex*/
  gint no_rtcp_timeout;

  GError *construction_error;
};

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(FsRtpSubStream, fs_rtp_sub_stream, G_TYPE_OBJECT);

#define FS_RTP_SUB_STREAM_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_SUB_STREAM,             \
   FsRtpSubStreamPrivate))


#define FS_RTP_SUB_STREAM_LOCK(substream) \
  g_mutex_lock (substream->priv->mutex)
#define FS_RTP_SUB_STREAM_UNLOCK(substream) \
  g_mutex_unlock (substream->priv->mutex)


static void fs_rtp_sub_stream_dispose (GObject *object);
static void fs_rtp_sub_stream_finalize (GObject *object);
static void fs_rtp_sub_stream_constructed (GObject *object);

static void fs_rtp_sub_stream_get_property (GObject *object, guint prop_id,
  GValue *value, GParamSpec *pspec);
static void fs_rtp_sub_stream_set_property (GObject *object, guint prop_id,
  const GValue *value, GParamSpec *pspec);

static void
fs_rtp_sub_stream_add_probe_locked (FsRtpSubStream *substream);

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
  signals[ERROR_SIGNAL] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_rtp_marshal_VOID__INT_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

 /**
   * FsRtpSubStream:codec-changed
   * @self: #FsRtpSubStream that emitted the signal
   *
   * This signal is emitted when the code for this substream has
   * changed. It can be fetvched from the #FsRtpSubStream:codec property
   * This is useful for displaying the current active reception codecs.
   */
  signals[CODEC_CHANGED] = g_signal_new ("codec-changed",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

 /**
   * FsRtpSubStream:blocked
   * @self: #FsRtpSubStream that emitted the signal
   * @stream: the #FsRtpStream this substream is attached to if any (or %NULL)
   *
   * This signal is emitted after the substream has been blocked because its
   * codec has been invalidated OR because no codecbin was set on its creation.
   */
  signals[BLOCKED] = g_signal_new ("blocked",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__POINTER,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  g_type_class_add_private (klass, sizeof (FsRtpSubStreamPrivate));
}


static void
fs_rtp_sub_stream_init (FsRtpSubStream *self)
{
  self->priv = FS_RTP_SUB_STREAM_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
  self->priv->receiving = TRUE;
  self->priv->mutex = g_mutex_new ();
}


static gpointer
no_rtcp_timeout_func (gpointer user_data)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (user_data);
  GstClock *sysclock = NULL;
  GstClockID id;
  gboolean emit = TRUE;

  sysclock = gst_system_clock_obtain ();
  if (sysclock == NULL)
    goto no_sysclock;

  FS_RTP_SUB_STREAM_LOCK(self);
  id = self->priv->no_rtcp_timeout_id = gst_clock_new_single_shot_id (sysclock,
      self->priv->next_no_rtcp_timeout);

  FS_RTP_SUB_STREAM_UNLOCK(self);
  gst_clock_id_wait (id, NULL);
  FS_RTP_SUB_STREAM_LOCK(self);

  gst_clock_id_unref (id);
  self->priv->no_rtcp_timeout_id = NULL;

  if (self->priv->next_no_rtcp_timeout == 0)
    emit = FALSE;

  FS_RTP_SUB_STREAM_UNLOCK(self);

  gst_object_unref (sysclock);

  if (emit)
    g_signal_emit (self, signals[NO_RTCP_TIMEDOUT], 0);

  return NULL;

 no_sysclock:
  {
    fs_rtp_sub_stream_emit_error (self, FS_ERROR_INTERNAL,
        "Could not get system clock",
        "Could not get system clock");
    return NULL;
  }
}

static gboolean
fs_rtp_sub_stream_start_no_rtcp_timeout_thread (FsRtpSubStream *self,
    GError **error)
{
  gboolean res = TRUE;
  GstClock *sysclock = NULL;

  sysclock = gst_system_clock_obtain ();
  if (sysclock == NULL)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Could not obtain gst system clock");
    return FALSE;
  }

  FS_RTP_SESSION_LOCK (self->priv->session);
  FS_RTP_SUB_STREAM_LOCK(self);

  self->priv->next_no_rtcp_timeout = gst_clock_get_time (sysclock) +
    (self->priv->no_rtcp_timeout * GST_MSECOND);

  gst_object_unref (sysclock);

  if (self->priv->no_rtcp_timeout_thread == NULL) {
    /* only create a new thread if the old one was stopped. Otherwise we can
     * just reuse the currently running one. */
    self->priv->no_rtcp_timeout_thread =
      g_thread_create (no_rtcp_timeout_func, self, TRUE, error);
  }

  res = (self->priv->no_rtcp_timeout_thread != NULL);

  if (res == FALSE && error && *error == NULL)
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL, "Unknown error creating"
        " thread");

  FS_RTP_SUB_STREAM_UNLOCK(self);
  FS_RTP_SESSION_UNLOCK (self->priv->session);

  return res;
}

static void
fs_rtp_sub_stream_stop_no_rtcp_timeout_thread (FsRtpSubStream *self)
{
  FS_RTP_SUB_STREAM_LOCK(self);
  self->priv->next_no_rtcp_timeout = 0;
  if (self->priv->no_rtcp_timeout_id)
    gst_clock_id_unschedule (self->priv->no_rtcp_timeout_id);

  if (self->priv->no_rtcp_timeout_thread == NULL)
  {
    FS_RTP_SUB_STREAM_UNLOCK(self);
    return;
  }
  else
  {
    FS_RTP_SUB_STREAM_UNLOCK(self);
  }

  g_thread_join (self->priv->no_rtcp_timeout_thread);

  FS_RTP_SUB_STREAM_LOCK(self);
  self->priv->no_rtcp_timeout_thread = NULL;
  FS_RTP_SUB_STREAM_UNLOCK(self);
}

static void
fs_rtp_sub_stream_constructed (GObject *object)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);
  GstPad *capsfilter_sink_pad = NULL;
  GstPadLinkReturn linkret;
  gchar *tmp;

  GST_DEBUG ("New substream in session %u for ssrc %x and pt %u",
      self->priv->session->id, self->priv->ssrc, self->priv->pt);

  if (!self->priv->conference) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_INVALID_ARGUMENTS, "A Substream needs a conference object");
    return;
  }

  tmp = g_strdup_printf ("recv_valve_%d_%d_%d", self->priv->session->id,
      self->priv->ssrc, self->priv->pt);
  self->priv->valve = gst_element_factory_make ("fsvalve", tmp);
  g_free (tmp);

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

  tmp = g_strdup_printf ("recv_capsfilter_%d_%d_%d", self->priv->session->id,
      self->priv->ssrc, self->priv->pt);
  self->priv->capsfilter = gst_element_factory_make ("capsfilter", tmp);
  g_free (tmp);

  if (!self->priv->capsfilter) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not create a capsfilter element for"
      " session substream with ssrc: %u and pt:%d", self->priv->ssrc,
      self->priv->pt);
    return;
  }

  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->capsfilter)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not add the capsfilter element for session"
      " substream with ssrc: %u and pt:%d to the conference bin",
      self->priv->ssrc, self->priv->pt);
    return;
  }

  if (gst_element_set_state (self->priv->capsfilter, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not set the capsfilter element for session"
      " substream with ssrc: %u and pt:%d to the playing state",
      self->priv->ssrc, self->priv->pt);
    return;
  }

  capsfilter_sink_pad = gst_element_get_static_pad (self->priv->capsfilter,
      "sink");
  if (!capsfilter_sink_pad)
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not get the capsfilter's sink pad");
    return;
  }

  linkret = gst_pad_link (self->priv->rtpbin_pad, capsfilter_sink_pad);

  gst_object_unref (capsfilter_sink_pad);

  if (GST_PAD_LINK_FAILED (linkret))
  {
    self->priv->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not link the rtpbin to the codec bin (%d)", linkret);
    return;
  }


  if (self->priv->no_rtcp_timeout > 0)
    if (!fs_rtp_sub_stream_start_no_rtcp_timeout_thread (self,
            &self->priv->construction_error))
      return;

  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
}


static void
fs_rtp_sub_stream_dispose (GObject *object)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  if (self->priv->disposed)
    return;

  fs_rtp_sub_stream_stop_no_rtcp_timeout_thread (self);

  if (self->priv->output_ghostpad) {
    gst_element_remove_pad (GST_ELEMENT (self->priv->conference),
      self->priv->output_ghostpad);
    self->priv->output_ghostpad = NULL;
  }

  if (self->priv->capsfilter) {
    gst_object_ref (self->priv->capsfilter);
    gst_element_set_state (self->priv->capsfilter, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self->priv->conference), self->priv->capsfilter);
    gst_element_set_state (self->priv->capsfilter, GST_STATE_NULL);
    gst_object_unref (self->priv->capsfilter);
    self->priv->capsfilter = NULL;
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

  if (self->priv->caps)
    gst_caps_unref (self->priv->caps);

  if (self->priv->mutex)
    g_mutex_free (self->priv->mutex);

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
 * fs_rtp_sub_stream_set_codecbin:
 * @substream: a #FsRtpSubStream
 * @codec: The codec to set
 * @codecbin: the codecbin to set
 *
 * Add and links the rtpbin for a given substream.
 * Removes any codecbin that was previously there.
 *
 * This function will swallow one ref to the codecbin
 *
 * Returns: TRUE on success
 */

gboolean
fs_rtp_sub_stream_set_codecbin (FsRtpSubStream *substream,
    FsCodec *codec,
    GstElement *codecbin,
    GError **error)
{
  GstCaps *caps = NULL;
  gchar *tmp;
  gboolean ret = FALSE;
  GstPad *pad;
  gboolean codec_changed = TRUE;

  FS_RTP_SESSION_LOCK (substream->priv->session);

  if (substream->priv->codec)
  {
    if (!fs_codec_are_equal (codec, substream->priv->codec))
      codec_changed = FALSE;
  }

  if (substream->priv->codecbin)
  {
    if (gst_element_set_state (substream->priv->codecbin, GST_STATE_NULL) !=
        GST_STATE_CHANGE_SUCCESS)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
          "Could not set the codec bin for ssrc %u"
          " and payload type %d to the state NULL", substream->priv->ssrc,
          substream->priv->pt);
      goto error_no_remove;
    }

    gst_bin_remove (GST_BIN (substream->priv->conference),
        substream->priv->codecbin);
    substream->priv->codecbin = NULL;

    fs_codec_destroy (substream->priv->codec);
    substream->priv->codec = NULL;

    if (substream->priv->caps)
      gst_caps_unref (substream->priv->caps);
    substream->priv->caps = NULL;
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

  if (!gst_element_link_pads (substream->priv->capsfilter, "src",
          codecbin, "sink"))
  {
     g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
         "Could not link the receive capsfilter and the codecbin for pt %d",
         substream->priv->pt);
    goto error;
  }

  caps = fs_codec_to_gst_caps (codec);
  tmp = gst_caps_to_string (caps);
  GST_DEBUG ("Setting caps %s on recv substream", tmp);
  g_free (tmp);
  g_object_set (substream->priv->capsfilter, "caps", caps, NULL);

  pad = gst_element_get_static_pad (codecbin, "sink");
  if (!pad)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL, "Could not get sink pad"
        " from codecbin");
    goto error;
  }

  /* This is a non-error error
   * Some codecs require config data to start.. so we should just ignore them
   */
  if (!gst_pad_set_caps (pad, caps))
  {
    ret = TRUE;
    gst_object_unref (pad);
    gst_caps_unref (caps);

    GST_DEBUG ("Could not set the caps on the codecbin, waiting on config-data"
        " for SSRC:%x pt:%d", substream->priv->ssrc, substream->priv->pt);

    /* We call this to drop all buffers until something comes up */
    fs_rtp_sub_stream_add_probe_locked (substream);
    goto error;
  }

  gst_object_unref (pad);

  substream->priv->caps = caps;
  substream->priv->codecbin = codecbin;
  substream->priv->codec = fs_codec_copy (codec);

  if (substream->priv->stream && !substream->priv->output_ghostpad)
  {
    if (!fs_rtp_sub_stream_add_output_ghostpad_locked (substream, error))
      goto error;
  }
  else
  {
    if (codec_changed)
      g_signal_emit (substream, signals[CODEC_CHANGED], 0);
  }

  FS_RTP_SESSION_UNLOCK (substream->priv->session);
  return TRUE;

 error:

  gst_element_set_state (codecbin, GST_STATE_NULL);
  gst_object_ref (codecbin);
  gst_bin_remove (GST_BIN (substream->priv->conference), codecbin);

 error_no_remove:

  FS_RTP_SESSION_UNLOCK (substream->priv->session);

  return ret;;
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

  if (substream->priv->capsfilter)
    gst_element_set_state (substream->priv->capsfilter, GST_STATE_NULL);
}


/**
 * fs_rtp_sub_stream_add_output_ghostpad:
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

  GST_DEBUG ("Src pad added on substream for ssrc:%X pt:%u " FS_CODEC_FORMAT,
      substream->priv->ssrc, substream->priv->pt,
      FS_CODEC_ARGS (substream->priv->codec));

  g_signal_emit (substream, signals[SRC_PAD_ADDED], 0,
                 ghostpad, substream->priv->codec);

  g_signal_emit (substream, signals[CODEC_CHANGED], 0);

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
  gboolean ret = TRUE;
  gboolean remove = FALSE;

  FS_RTP_SESSION_LOCK (self->priv->session);

  if (!self->priv->codecbin || !self->priv->codec || !self->priv->caps)
  {
    ret = FALSE;
  }
  else if (GST_IS_BUFFER (miniobj))
  {
    if (!gst_caps_is_equal_fixed (GST_BUFFER_CAPS (miniobj), self->priv->caps))
    {
      GstCaps *intersect = gst_caps_intersect (GST_BUFFER_CAPS (miniobj),
          self->priv->caps);

      if (intersect)
      {
        gst_buffer_set_caps (GST_BUFFER (miniobj), self->priv->caps);

        gst_caps_unref (intersect);
      }
      else
        ret = FALSE;
    }
    else
    {
      remove = TRUE;
    }
  }

  if (remove && self->priv->blocking_id)
  {
    gst_pad_remove_data_probe (pad, self->priv->blocking_id);
    self->priv->blocking_id = 0;
  }

  FS_RTP_SESSION_UNLOCK (self->priv->session);

  return ret;
}

static void
do_nothing_blocked_callback (GstPad *pad, gboolean blocked, gpointer user_data)
{
}

static void
_rtpbin_pad_blocked_callback (GstPad *pad, gboolean blocked, gpointer user_data)
{
  FsRtpSubStream *substream = user_data;

  g_signal_emit (substream, signals[BLOCKED], 0, substream->priv->stream);

  gst_pad_set_blocked_async (substream->priv->rtpbin_pad, FALSE,
      do_nothing_blocked_callback, NULL);
}

static void
fs_rtp_sub_stream_add_probe_locked (FsRtpSubStream *substream)
{
  if (!substream->priv->blocking_id)
    substream->priv->blocking_id = gst_pad_add_data_probe (
        substream->priv->rtpbin_pad,
        G_CALLBACK (_rtpbin_pad_have_data_callback), substream);
}

/**
 * fs_rtp_sub_stream_verify_codec_locked:
 * @substream: A #FsRtpSubStream
 *
 * This function will start the process that invalidates the codec
 * for this rtpbin.
 *
 * You must hold the session lock to call it.
 */

void
fs_rtp_sub_stream_verify_codec_locked (FsRtpSubStream *substream)
{
  GST_LOG ("Starting codec verification process for substream with"
      " SSRC:%x pt:%d", substream->priv->ssrc, substream->priv->pt);


  fs_rtp_sub_stream_add_probe_locked (substream);

  gst_pad_set_blocked_async (substream->priv->rtpbin_pad, TRUE,
      _rtpbin_pad_blocked_callback, substream);
}


static void
fs_rtp_sub_stream_emit_error (FsRtpSubStream *substream,
    gint error_no,
    gchar *error_msg,
    gchar *debug_msg)
{
  g_signal_emit (substream, signals[ERROR_SIGNAL], 0, error_no, error_msg,
      debug_msg);
}

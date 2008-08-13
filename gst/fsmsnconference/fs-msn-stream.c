/*
 * Farsight2 - Farsight MSN Stream
 *
 *  @author: Richard Spiers <richard.spiers@gmail.com>
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @Rob Taylor, Philippe Khalaf   ?  
 *
 * fs-msn-stream.c - A Farsight MSN Stream gobject
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
 * SECTION:fs-msn-stream
 * @short_description: A MSN stream in a #FsMsnSession in a #FsMsnConference
 *

 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-msn-stream.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#include <gst/gst.h>

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_DIRECTION,
  PROP_PARTICIPANT,
  PROP_SESSION,
  PROP_SINK_PAD,
  PROP_SRC_PAD,
  PROP_CONFERENCE,
  PROP_L_RID,
  PROP_L_SID,
  PROP_R_RID,
  PROP_R_SID,
  
};

struct _FsMsnStreamPrivate
{
  FsMsnSession *session;
  FsMsnParticipant *participant;
  FsStreamDirection direction;
  GArray *fdlist;
  FsMsnConference *conference;
  GstElement *media_fd_src;
  GstPad *sink_pad,*src_pad;
  guint in_watch, out_watch, main_watch;
  gint local_recipientid, local_sessionid;
  gint remote_recipientid, remote_sessionid;  
  GIOChannel *connection; 
  


  /* Protected by the session mutex */

  GError *construction_error;

  gboolean disposed;
};


G_DEFINE_TYPE(FsMsnStream, fs_msn_stream, FS_TYPE_STREAM);

#define FS_MSN_STREAM_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_MSN_STREAM, FsMsnStreamPrivate))

static void fs_msn_stream_dispose (GObject *object);
static void fs_msn_stream_finalize (GObject *object);

static void fs_msn_stream_get_property (GObject *object,
                                    guint prop_id,
                                    GValue *value,
                                    GParamSpec *pspec);
static void fs_msn_stream_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec);

static void fs_msn_stream_constructed (GObject *object);

static gboolean fs_msn_stream_set_remote_candidates (FsMsnStream *stream,
                                                     GList *candidates,
                                                     GError **error);
                                                     
static gboolean fs_msn_stream_set_remote_candidate  (FsMsnStream *stream,
                                                     FsCandidate *candidate,
                                                     GError **error);

static gboolean main_fd_closed_cb (GIOChannel *ch,
                                   GIOCondition cond,
                                   gpointer data);

static gboolean successfull_connection_cb (GIOChannel *ch,
                                           GIOCondition cond,
                                           gpointer data);

static gboolean fs_msn_stream_attempt_connection (FsMsnStream *stream,
                                                  gchar *ip,
                                                  guint16 port);

static gboolean fs_msn_authenticate_outgoing (FsMsnStream *stream,
                                              gint fd);

static void fs_msn_open_listening_port (FsMsnStream *stream,
                                        guint16 port);                                                                                                                                                                                                                          


/* Needed ?   
static void _local_candidates_prepared (
    FsStreamTransmitter *stream_transmitter,
    gpointer user_data);
    
static void _new_active_candidate_pair (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *candidate1,
    FsCandidate *candidate2,
    gpointer user_data);
    
static void _new_local_candidate (
    FsStreamTransmitter *stream_transmitter,
    FsCandidate *candidate,
    gpointer user_data);
    
*/

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_msn_stream_class_init (FsMsnStreamClass *klass)
{
  GObjectClass *gobject_class;
  FsStreamClass *stream_class = FS_STREAM_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_msn_stream_set_property;
  gobject_class->get_property = fs_msn_stream_get_property;
  gobject_class->constructed = fs_msn_stream_constructed;
  gobject_class->dispose = fs_msn_stream_dispose;
  gobject_class->finalize = fs_msn_stream_finalize;


  stream_class->set_remote_candidates = fs_msn_stream_set_remote_candidates;


  g_type_class_add_private (klass, sizeof (FsMsnStreamPrivate));

  g_object_class_override_property (gobject_class,
      PROP_DIRECTION,
      "direction");
  g_object_class_override_property (gobject_class,
      PROP_PARTICIPANT,
      "participant");
  g_object_class_override_property (gobject_class,
      PROP_SESSION,
      "session");

  g_object_class_install_property (gobject_class,
      PROP_SINK_PAD,
      g_param_spec_object ("sink-pad",
          "A gstreamer sink pad for this stream",
          "A pad used for sending data on this stream",
          GST_TYPE_PAD,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
      PROP_SRC_PAD,
      g_param_spec_object ("src-pad",
          "A gstreamer src pad for this stream",
          "A pad used for reading data from this stream",
          GST_TYPE_PAD,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
      PROP_CONFERENCE,
      g_param_spec_object ("conference",
          "The Conference this stream refers to",
          "This is a conveniance pointer for the Conference",
          FS_TYPE_MSN_CONFERENCE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
          
  g_object_class_install_property (gobject_class,
      PROP_L_RID,
      g_param_spec_uint ("local-recipientid",
          "The local recipientid used for this stream",
          "The session ID used for this stream",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class,
      PROP_L_SID,
      g_param_spec_uint ("local-sessionid",
          "The local sessionid used for this stream",
          "The session ID used for this stream",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE));
          
  g_object_class_install_property (gobject_class,
      PROP_R_RID,
      g_param_spec_uint ("remote-recipientid",
          "The remote recipientid used for this stream",
          "The session ID used for this stream",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class,
      PROP_R_SID,
      g_param_spec_uint ("remote-sessionid",
          "The remote sessionid used for this stream",
          "The session ID used for this stream",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE));                        


}

static void
fs_msn_stream_init (FsMsnStream *self)
{
  /* member init */
  self->priv = FS_MSN_STREAM_GET_PRIVATE (self);

  self->priv->disposed = FALSE;
  self->priv->session = NULL;
  self->priv->participant = NULL;

  self->priv->direction = FS_DIRECTION_NONE;
  self->priv->fdlist = g_array_new (FALSE, FALSE, sizeof(GIOChannel *));
}

static void
fs_msn_stream_dispose (GObject *object)
{
  FsMsnStream *self = FS_MSN_STREAM (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  if (self->priv->participant) {
    g_object_unref (self->priv->participant);
    self->priv->participant = NULL;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  if (self->priv->session)
  {
    g_object_unref (self->priv->session);
    self->priv->session = NULL;
  }

  parent_class->dispose (object);
}

static void
fs_msn_stream_finalize (GObject *object)
{
  FsMsnStream *self = FS_MSN_STREAM (object);

  parent_class->finalize (object);
}


static void
fs_msn_stream_get_property (GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
  FsMsnStream *self = FS_MSN_STREAM (object);

  switch (prop_id) {
    case PROP_SESSION:
      g_value_set_object (value, self->priv->session);
      break;
    case PROP_PARTICIPANT:
      g_value_set_object (value, self->priv->participant);
      break;
    case PROP_DIRECTION:
      g_value_set_flags (value, self->priv->direction);
      break;
    case PROP_SINK_PAD:
      g_value_set_object (value, self->priv->sink_pad);
      break;
    case PROP_SRC_PAD:
      g_value_set_object (value, self->priv->src_pad);
      break;
    case PROP_CONFERENCE:
      g_value_set_flags (value, self->priv->conference);
      break;
    case PROP_L_RID:
      g_value_set_uint (value, self->priv->local_recipientid);
      break;
    case PROP_L_SID:
      g_value_set_uint (value, self->priv->local_sessionid);
      break;
    case PROP_R_RID:
      g_value_set_uint (value, self->priv->remote_recipientid);
      break;
    case PROP_R_SID:
      g_value_set_uint (value, self->priv->remote_sessionid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_msn_stream_set_property (GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
  FsMsnStream *self = FS_MSN_STREAM (object);
  GList *item;

  switch (prop_id) {
    case PROP_SESSION:
      self->priv->session = FS_MSN_SESSION (g_value_dup_object (value));
      break;
    case PROP_PARTICIPANT:
      self->priv->participant = FS_MSN_PARTICIPANT (g_value_dup_object (value));
      break;
    case PROP_DIRECTION:
      self->priv->direction = g_value_get_flags (value);
      break;
    case PROP_CONFERENCE:
      self->priv->conference = FS_MSN_CONFERENCE (g_value_dup_object (value));
      break;
    case PROP_L_RID:
      self->priv->local_recipientid = g_value_get_uint (value);
      break;
    case PROP_L_SID:
      self->priv->local_sessionid = g_value_get_uint (value);
      break;
    case PROP_R_RID:
      self->priv->remote_recipientid = g_value_get_uint (value);
      break;
    case PROP_R_SID:
      self->priv->remote_sessionid = g_value_get_uint (value);
      break;      
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_msn_stream_constructed (GObject *object)
{

  FsMsnStream *self = FS_MSN_STREAM_CAST (object);

  //GstElement *ximagesink;
  
  //ximagesink = gst_element_factory_make("ximagesink","testximagesink");
  //gst_bin_add (GST_BIN (self->priv->conference), ximagesink);
  //gst_element_set_state (ximagesink, GST_STATE_PLAYING);
  fs_msn_open_listening_port(self,9898);
  
  if (self->priv->direction == FS_DIRECTION_SEND)
  {
    GstElement *media_fd_sink;
    GstElement *mimenc;
    GstElement *ffmpegcolorspace;
    GstElement *valve;
    
    valve = gst_element_factory_make ("fsvalve","send_valve");

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
		    "Could not add the valve element to the FsRtpConference");
		  gst_object_unref (valve);
		  return;
		}

		g_object_set (G_OBJECT (valve), "drop", FALSE, NULL);
		
		if (!gst_element_set_state (valve, GST_STATE_PLAYING))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not set state for valve element");
      return;
    }

    ffmpegcolorspace = gst_element_factory_make ("ffmpegcolorspace", "ffmpegcolorspace");

    if (!ffmpegcolorspace)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the ffmpegcolorspace element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), ffmpegcolorspace))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the ffmpegcolorspace element to the FsMsnConference");
      gst_object_unref (ffmpegcolorspace);
      return;
    }

    if (!gst_element_set_state (ffmpegcolorspace, GST_STATE_PLAYING))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not set state for ffmpegcolorspace element");
      return;
    }

    media_fd_sink = gst_element_factory_make ("multifdsink", "send_fd_sink");

    if (!media_fd_sink)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the media_fd_sink element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), media_fd_sink))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the media_fd_sink element to the FsMsnConference");
      gst_object_unref (media_fd_sink);
      return;
    }

    if (!gst_element_set_state (media_fd_sink, GST_STATE_PLAYING))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not set state for media_fd_sink element");
      return;
    }

    mimenc = gst_element_factory_make ("mimenc", "send_mim_enc");

    if (!mimenc)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the mimenc element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), mimenc))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the mimenc element to the FsMsnConference");
      gst_object_unref (mimenc);
      return;
    }

    if (!gst_element_set_state (mimenc, GST_STATE_PLAYING))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not set state for mimenc element");
      return;
    }

    GstPad *tmp_sink_pad = gst_element_get_static_pad (valve, "sink");
    self->priv->sink_pad = gst_ghost_pad_new ("sink", tmp_sink_pad);
		gst_pad_set_active(self->priv->sink_pad, TRUE);
		    	
		gst_element_link_many(valve,ffmpegcolorspace,mimenc,media_fd_sink,NULL);
  }
  else if (self->priv->direction == FS_DIRECTION_RECV)
  {

    GstElement *mimdec;
    GstElement *ffmpegcolorspace;
    GstElement *valve;
    
    valve = gst_element_factory_make ("fsvalve","recv_valve");

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
		    "Could not add the valve element to the FsRtpConference");
		  gst_object_unref (valve);
		  return;
		}

		g_object_set (G_OBJECT (valve), "drop", FALSE, NULL);
		
		if (!gst_element_sync_state_with_parent  (valve))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not sync state with parent for valve element");
      return;
    }

    ffmpegcolorspace = gst_element_factory_make ("ffmpegcolorspace", "ffmpegcolorspace");

    if (!ffmpegcolorspace)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the ffmpegcolorspace element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), ffmpegcolorspace))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the ffmpegcolorspace element to the FsMsnConference");
      gst_object_unref (ffmpegcolorspace);
      return;
    }

    if (!gst_element_sync_state_with_parent  (ffmpegcolorspace))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not sync state with parent for ffmpegcolorspace element");
      return;
    }


    self->priv->media_fd_src = gst_element_factory_make ("fdsrc", "recv_fd_src");
  
    if (!self->priv->media_fd_src)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the media_fd_src element");
      return;
    }
    
    g_object_set (G_OBJECT(self->priv->media_fd_src), "blocksize", 512, NULL);
    if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->media_fd_src))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the media_fd_src element to the FsMsnConference");
      gst_object_unref (self->priv->media_fd_src);
      return;
    }

    if (!gst_element_sync_state_with_parent  (self->priv->media_fd_src))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not sync state with parent for media_fd_src element");
      return;
    }

    mimdec = gst_element_factory_make ("mimdec", "recv_mim_dec");

    if (!mimdec)
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not create the mimdec element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->conference), mimdec))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the mimdec element to the FsMsnConference");
      gst_object_unref (mimdec);
      return;
    }
    if (!gst_element_sync_state_with_parent  (mimdec))
    {
      self->priv->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not sync state with parent for mimdec element");
      return;
    }


    GstPad *tmp_src_pad = gst_element_get_static_pad (valve, "src");
    self->priv->src_pad = gst_ghost_pad_new ("src", tmp_src_pad);
	  gst_pad_set_active(self->priv->src_pad, TRUE);
    
    gst_element_link_many(self->priv->media_fd_src,mimdec,ffmpegcolorspace,valve,NULL);
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, constructed, (object));
}

/**
 * fs_msn_stream_set_remote_candidate:
 */
static gboolean
fs_msn_stream_set_remote_candidates (FsMsnStream *stream, GList *candidates,
                                     GError **error)
{
  FsMsnStream *self = FS_MSN_STREAM (stream);
	GList *item = NULL;

  for (item = candidates; item; item = g_list_next (item))
  {
    FsCandidate *candidate = item->data;

    if (!candidate->ip || !candidate->port)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The candidate passed does not contain a valid ip or port");
      return FALSE;
    }
  }

  for (item = candidates; item; item = g_list_next (item))
  {
    FsCandidate *candidate = item->data;
    if (!fs_msn_stream_set_remote_candidate (self,candidate,error))
      return FALSE;
  }

  return TRUE;
}

static gboolean main_fd_closed_cb (GIOChannel *ch,
                                   GIOCondition cond,
                                   gpointer data)
{
    FsMsnStream *self = FS_MSN_STREAM (data);

    g_message ("disconnection on video feed %p %p", ch, self->priv->connection);
    g_source_remove (self->priv->main_watch);
    return FALSE;
}

static gboolean successfull_connection_cb (GIOChannel *ch,
                                           GIOCondition cond,
                                           gpointer data)
{
    FsMsnStream *self = FS_MSN_STREAM (data);
    gint error, len;
    gint fd = g_io_channel_unix_get_fd (ch);

    g_message ("handler called on fd %d", fd);

    errno = 0;
    if (!((cond & G_IO_IN) || (cond & G_IO_OUT)))
    {
        g_message ("Condition received is %d", cond);
        goto error;
    }

    len = sizeof(error);

    /* Get the error option */
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*) &error, &len) < 0)
    {
        g_warning ("getsockopt() failed");
        goto error;
    }

    /* Check if there is an error */
    if (error)
    {
        g_message ("getsockopt gave an error : %d", error);
        goto error;
    }

    /* Remove NON BLOCKING MODE */
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK) != 0)
    {
        g_warning ("fcntl() failed");
        goto error;
    }

    g_message ("Got connection on fd %d", fd);

    //g_source_remove (msnwebcam->connect_watch);
    //g_io_channel_unref (ch);
    // let's try to auth on this connection
    if (fs_msn_authenticate_outgoing (self,fd))
    {
        g_message ("Authenticated outgoing successfully fd %d", fd);
        self->priv->connection = ch;

        // success! we need to shutdown/close all other channels 
        gint i;
        for (i = 0; i < self->priv->fdlist->len; i++)
        {
            GIOChannel *chan = g_array_index(self->priv->fdlist, GIOChannel*, i);
            if (chan != ch)
            {
                g_message ("closing fd %d", g_io_channel_unix_get_fd (chan));
                g_io_channel_shutdown (chan, TRUE, NULL);
                g_io_channel_unref (chan);
                g_array_remove_index (self->priv->fdlist, i);
            }
        }
        g_source_remove (self->priv->out_watch);
        g_message("Setting media_fd_src on fd %d",fd);
        g_object_set (G_OBJECT(self->priv->media_fd_src), "fd", fd, NULL);
        gst_element_set_locked_state(self->priv->media_fd_src,TRUE);
        GstState *state = NULL;
        gst_element_get_state(self->priv->media_fd_src,state,NULL,GST_CLOCK_TIME_NONE);
        if ( state > GST_STATE_READY)
          { gst_element_set_state(self->priv->media_fd_src,GST_STATE_READY);
            g_object_set (G_OBJECT(self->priv->media_fd_src), "fd", fd, NULL);
            gst_element_set_locked_state(self->priv->media_fd_src,FALSE);
            gst_element_sync_state_with_parent(self->priv->media_fd_src);
          }  
        gst_element_set_locked_state(self->priv->media_fd_src,FALSE);
        // add a watch on this fd to when it disconnects
        self->priv->main_watch = g_io_add_watch (ch, 
                (G_IO_ERR|G_IO_HUP|G_IO_NVAL), 
                main_fd_closed_cb, self);
        return FALSE;
    }
    else
    {
        g_message ("Authentification failed on fd %d", fd);
    }

    /* Error */
error:
    g_message ("Got error from fd %d, closing", fd);
    // find, shutdown and remove channel from fdlist
    gint i;
    for (i = 0; i < self->priv->fdlist->len; i++)
    {
        GIOChannel *chan = g_array_index(self->priv->fdlist, GIOChannel*, i);
        if (ch == chan)
        {
            g_io_channel_shutdown (chan, TRUE, NULL);
            g_io_channel_unref (chan);
            g_array_remove_index (self->priv->fdlist, i);
        }
    }

    return FALSE;
}

static gboolean
fs_msn_stream_attempt_connection (FsMsnStream *stream,gchar *ip, guint16 port)
{
  FsMsnStream *self = FS_MSN_STREAM (stream);
  
  GIOChannel *chan;
  gint fd = -1;
  struct sockaddr_in theiraddr;
  memset(&theiraddr, 0, sizeof(theiraddr));

  if ( (fd = socket(PF_INET, SOCK_STREAM, 0)) == -1 )
  {
      // show error
      g_message ("could not create socket!");
      return FALSE;
  }

  chan = g_io_channel_unix_new (fd);
  g_io_channel_set_close_on_unref (chan, TRUE);
  g_array_append_val (self->priv->fdlist, chan);

  // set non-blocking mode
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

  theiraddr.sin_family = AF_INET;
  theiraddr.sin_addr.s_addr = inet_addr (ip);
  theiraddr.sin_port = htons (port);

  g_message ("Attempting connection to %s %d on socket %d", ip, port, fd);
  // this is non blocking, the return value isn't too usefull
  gint ret = connect (fd, (struct sockaddr *) &theiraddr, sizeof (theiraddr));
  if (ret < 0)
  {
      if (errno != EINPROGRESS)
      {
          g_io_channel_shutdown (chan, TRUE, NULL);
          g_io_channel_unref (chan);
          return FALSE;
      }
  }
  g_message("ret %d %d %s", ret, errno, strerror(errno));

  // add a watch on that io for when it connects
  self->priv->out_watch = g_io_add_watch (chan, 
          (G_IO_IN|G_IO_OUT|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL), 
          successfull_connection_cb, self);			
  return TRUE;				
}                

static gboolean 
fs_msn_stream_set_remote_candidate  (FsMsnStream *stream,
                                     FsCandidate *candidate,
                                     GError **error)
{
FsMsnStream *self = FS_MSN_STREAM (stream);
fs_msn_stream_attempt_connection(self,candidate->ip,candidate->port);
}

static gboolean
fs_msn_authenticate_incoming (FsMsnStream *stream, gint fd)
{
  FsMsnStream *self = FS_MSN_STREAM (stream);
    if (fd != 0)
    {
        gchar str[400];
        gchar check[400];

        memset(str, 0, sizeof(str));
        if (recv(fd, str, sizeof(str), 0) != -1)
        {
            g_message ("Got %s, checking if it's auth", str);
            sprintf(str, "recipientid=%d&sessionid=%d\r\n\r\n", 
                    self->priv->local_recipientid, self->priv->remote_sessionid);
            if (strcmp (str, check) != 0)
            {
                // send our connected message also
                memset(str, 0, sizeof(str));
                sprintf(str, "connected\r\n\r\n");
                send(fd, str, strlen(str), 0);

                // now we get connected
                memset(str, 0, sizeof(str));
                if (recv(fd, str, sizeof(str), 0) != -1)
                {
                    if (strcmp (str, "connected\r\n\r\n") == 0)
                    {
                        g_message ("Authentication successfull");
                        return TRUE;
                    }
                }
            }
        }
        else
        {
            perror("auth");
        }
    }
    return FALSE;
}

static gboolean
fd_accept_connection_cb (GIOChannel *ch, GIOCondition cond, gpointer data)
{
    FsMsnStream *self = FS_MSN_STREAM (data);
    struct sockaddr_in in;
    int fd;
    GIOChannel *newchan = NULL;
    socklen_t n = sizeof (in);

    if (!(cond & G_IO_IN))
    {
        g_message ("Error in condition not G_IO_IN");
        return FALSE;
    }

    if ((fd = accept(g_io_channel_unix_get_fd (ch), (struct sockaddr*) &in, &n)) == -1)
    {
        g_message ("Error while running accept() %d", errno);
        return FALSE;
    }

    // ok we got a connection, let's set it up
    newchan = g_io_channel_unix_new (fd);
    g_io_channel_set_close_on_unref (newchan, TRUE);

    /* Remove NON BLOCKING MODE */
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK) != 0)
    {
        g_warning ("fcntl() failed");
        goto error;
    }

    // now we try to auth
    if (fs_msn_authenticate_incoming(self,fd))
    {
        g_message ("Authenticated incoming successfully fd %d", fd);
        self->priv->connection = newchan;

        // success! we need to shutdown/close all other channels 
        gint i;
        for (i = 0; i < self->priv->fdlist->len; i++)
        {
            GIOChannel *chan = g_array_index(self->priv->fdlist, GIOChannel*, i);
            if (chan != newchan)
            {
                g_message ("closing fd %d", g_io_channel_unix_get_fd (chan));
                g_io_channel_shutdown (chan, TRUE, NULL);
                g_io_channel_unref (chan);
                g_array_remove_index (self->priv->fdlist, i);
            }
        }
        g_source_remove (self->priv->in_watch);
        g_message("Setting media_fd_src on fd %d",fd);
        g_object_set (G_OBJECT(self->priv->media_fd_src), "fd", fd, NULL);
        // add a watch on this fd to when it disconnects
        self->priv->main_watch = g_io_add_watch (newchan, 
                (G_IO_ERR|G_IO_HUP|G_IO_NVAL), 
                main_fd_closed_cb, self);
        return FALSE;
    }

    /* Error */
error:
    g_message ("Got error from fd %d, closing", fd);
    // find, shutdown and remove channel from fdlist
    gint i;
    for (i = 0; i < self->priv->fdlist->len; i++)
    {
        GIOChannel *chan = g_array_index(self->priv->fdlist, GIOChannel*, i);
        if (newchan == chan)
        {
            g_io_channel_shutdown (chan, TRUE, NULL);
            g_io_channel_unref (chan);
            g_array_remove_index (self->priv->fdlist, i);
        }
    }

    return FALSE;
}    

static void 
fs_msn_open_listening_port (FsMsnStream *stream,
                            guint16 port)
{
  FsMsnStream *self = FS_MSN_STREAM (stream);
  g_message ("Attempting to listen on port %d.....\n",port);

  GIOChannel *chan;
  gint fd = -1;
  struct sockaddr_in theiraddr;
  memset(&theiraddr, 0, sizeof(theiraddr));

 if ( (fd = socket(PF_INET, SOCK_STREAM, 0)) == -1 )
 	{
  	// show error
    g_message ("could not create socket!");
    return;
  }

  // set non-blocking mode
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  theiraddr.sin_family = AF_INET;
  theiraddr.sin_port = htons (port);
  // bind
  if (bind(fd, (struct sockaddr *) &theiraddr, sizeof(theiraddr)) != 0)
  	{
  		close (fd);
  		return;
  	}


  /* Listen */
	if (listen(fd, 3) != 0)
		{
    	close (fd);
    	return;
    }
  chan = g_io_channel_unix_new (fd);
  g_io_channel_set_close_on_unref (chan, TRUE);

  g_array_append_val (self->priv->fdlist, chan);
  g_message ("Listening on port %d\n",port);
  self->priv->in_watch = g_io_add_watch(chan,
                G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                fd_accept_connection_cb, self);

}

// Authenticate ourselves when connecting out
static gboolean
fs_msn_authenticate_outgoing (FsMsnStream *stream, gint fd)
{
    FsMsnStream *self = FS_MSN_STREAM (stream);
    gchar str[400];
    memset(str, 0, sizeof(str));
    if (fd != 0)
    {
        g_message ("Authenticating connection on %d...", fd);
        g_message ("sending : recipientid=%d&sessionid=%d\r\n\r\n",self->priv->remote_recipientid,self->priv->remote_sessionid);
        sprintf(str, "recipientid=%d&sessionid=%d\r\n\r\n",
                self->priv->remote_recipientid, self->priv->remote_sessionid);
        if (send(fd, str, strlen(str), 0) == -1)
        {
            g_message("sending failed");
            perror("auth");
        }

        memset(str, 0, sizeof(str));
        if (recv(fd, str, sizeof(str), 0) != -1)
        {
            g_message ("Got %s, checking if it's auth", str);
            // we should get a connected message now
            if (strcmp (str, "connected\r\n\r\n") == 0)
            {
                // send our connected message also
                memset(str, 0, sizeof(str));
                sprintf(str, "connected\r\n\r\n");
                send(fd, str, strlen(str), 0);
                g_message ("Authentication successfull");
                return TRUE;
            }
        }
        else
        {
            perror("auth");
        }
    }
    return FALSE;
}


/**
 * fs_msn_stream_new:
 * @session: The #FsMsnSession this stream is a child of
 * @participant: The #FsMsnParticipant this stream is for
 * @direction: the initial #FsDirection for this stream
 * 
 *
 * This function create a new stream
 *
 * Returns: the newly created string or NULL on error
 */

FsMsnStream *
fs_msn_stream_new (FsMsnSession *session,
                   FsMsnParticipant *participant,
                   FsStreamDirection direction,
                   FsMsnConference *conference,
                   GError **error)
{
  FsMsnStream *self = g_object_new (FS_TYPE_MSN_STREAM,
    "session", session,
    "participant", participant,
    "direction", direction,
    "conference",conference,
    NULL);

  if (self->priv->construction_error) {
    g_propagate_error (error, self->priv->construction_error);
    g_object_unref (self);
    return NULL;
  }

  return self;
}

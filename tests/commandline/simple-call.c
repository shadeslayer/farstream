
#include <glib.h>
#include <gst/gst.h>
#include <gst/farsight/fs-conference-iface.h>

#define DEFAULT_AUDIOSRC       "alsasrc"
#define DEFAULT_AUDIOSINK      "audioconvert ! audioresample ! audioconvert ! alsasink"

typedef enum {
  NONE,
  CLIENT,
  SERVER
} ClientServer;

typedef struct _TestSession
{
  FsSession *session;
  GstElement *src;
  FsStream *stream;
} TestSession;


static void
print_error (GError *error)
{
  if (error)
  {
    g_error ("Error: %s:%d : %s", g_quark_to_string (error->domain),
        error->code, error->message);
  }
}

static void
src_pad_added_cb (FsStream *stream, GstPad *pad, FsCodec *codec,
    gpointer user_data)
{
  GstElement *pipeline = GST_ELEMENT_CAST (user_data);
  GstElement *sink = NULL;
  GError *error = NULL;
  GstPad *pad2;

  if (g_getenv ("AUDIOSRC"))
    sink = gst_parse_bin_from_description (g_getenv ("AUDIOSINK"), TRUE,
        &error);
  else
    sink = gst_parse_bin_from_description (DEFAULT_AUDIOSINK, TRUE,
        &error);
  g_assert (sink);
  print_error (error);

  g_assert (gst_bin_add (GST_BIN (pipeline), sink));


  pad2 = gst_element_get_static_pad (sink, "sink");
  g_assert (pad2);

  g_assert (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (pad, pad2)));

  gst_object_unref (pad2);
}

static TestSession*
add_audio_session (GstElement *pipeline, FsConference *conf, guint id,
    FsParticipant *part)
{
  TestSession *ses = g_slice_new0 (TestSession);
  GError *error = NULL;
  GstPad *pad = NULL, *pad2 = NULL;;

  ses->session = fs_conference_new_session (conf, FS_MEDIA_TYPE_AUDIO, &error);
  g_assert (ses->session);
  print_error (error);

  g_object_get (ses->session, "sink-pad", &pad, NULL);

  if (g_getenv ("AUDIOSRC"))
    ses->src = gst_parse_bin_from_description (g_getenv ("AUDIOSRC"), TRUE,
        &error);
  else
    ses->src = gst_parse_bin_from_description (DEFAULT_AUDIOSRC, TRUE,
        &error);
  g_assert (ses->src);
  print_error (error);

  g_assert (gst_bin_add (GST_BIN (pipeline), ses->src));

  pad2 = gst_element_get_static_pad (ses->src, "src");
  g_assert (pad2);

  g_assert (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (pad2, pad)));

  gst_object_unref (pad2);
  gst_object_unref (pad);

  ses->stream = fs_session_new_stream (ses->session, part, FS_DIRECTION_BOTH,
      "rawudp", 0, NULL, &error);
  g_assert (ses->stream);
  print_error (error);

  g_signal_connect (ses->stream, "src-pad-added",
      G_CALLBACK (src_pad_added_cb), pipeline);

  return ses;
}

static gboolean
async_bus_cb (GstBus *bus, GstMessage *message, gpointer user_data)
{
  return TRUE;
}

int main (int argc, char **argv)
{
  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL;
  GstBus *bus = NULL;
  ClientServer mode = NONE;
  gchar *ip;
  guint port = 0;
  GstElement *conf = NULL;
  FsParticipant *part = NULL;
  GError *error = NULL;

  gst_init (&argc, &argv);

  if (argc == 0)
    mode = SERVER;
  else if (argc == 2)
  {
    mode = CLIENT;
    port = atoi (argv[2]);
    if (!port)
    {
      g_print ("Usage: %s [ip] [port]\n", argv[0]);
      return 1;
    }
    ip = argv[1];
  }
  else
  {
    g_print ("Usage: %s [ip] [port]\n", argv[0]);
    return 1;
  }

  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new (NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, async_bus_cb, pipeline);
  gst_object_unref (bus);

  conf = gst_element_factory_make ("fsrtpconference", NULL);

  part = fs_conference_new_participant (FS_CONFERENCE (conf), "test@ignore",
      &error);
  print_error (error);

  g_assert (gst_bin_add (GST_BIN (pipeline), conf));


  add_audio_session (pipeline, FS_CONFERENCE (conf), 1, part);


  g_assert (gst_element_set_state (pipeline, GST_STATE_PLAYING) !=
      GST_STATE_CHANGE_FAILURE);

  g_main_loop_run (loop);

  g_assert (gst_element_set_state (pipeline, GST_STATE_NULL) !=
      GST_STATE_CHANGE_FAILURE);

  g_object_unref (part);

  gst_object_unref (pipeline);
  g_main_loop_unref (loop);

  return 0;
}

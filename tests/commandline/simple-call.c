
#include <glib.h>
#include <gst/gst.h>
#include <gst/farsight/fs-conference-iface.h>

#define DEFAULT_AUDIO_SRC       "alsasrc"
#define DEFAULT_AUDIO_SINK      "alsasink"

typedef enum {
  NONE,
  CLIENT,
  SERVER }
  ClientServer;

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

  g_assert (gst_bin_add (GST_BIN (pipeline), conf));

  g_assert (gst_element_set_state (pipeline, GST_STATE_PLAYING)
      != GST_STATE_CHANGE_FAILURE);

  g_main_loop_run (loop);

  g_assert (gst_element_set_state (pipeline, GST_STATE_NULL)
      != GST_STATE_CHANGE_FAILURE);

  g_main_loop_unref (loop);
  gst_object_unref (pipeline);

  return 0;
}

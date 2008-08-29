
#include <glib.h>

#include "fs-upnp-simple-igd.h"

GMainLoop *loop = NULL;

static void
_new_external_ip (FsUpnpSimpleIgd *igd, gchar *ip, gpointer user_data)
{
  g_debug ("ip: %s", ip);

  g_main_loop_quit (loop);
}


static void
_error (FsUpnpSimpleIgd *igd, GError *error, gpointer user_data)
{
  g_error ("error: %s", error->message);
}
int
main (int argc, char **argv)
{
  FsUpnpSimpleIgd *igd = NULL;

  g_type_init ();
  g_thread_init (NULL);

  loop = g_main_loop_new (NULL, FALSE);

  igd = fs_upnp_simple_igd_new (NULL);

  g_signal_connect (igd, "new-external-ip", G_CALLBACK (_new_external_ip),
      NULL);
  g_signal_connect (igd, "error", G_CALLBACK (_error),
      NULL);

  fs_upnp_simple_igd_gather (igd, TRUE);

  g_main_loop_run (loop);

  g_main_loop_unref (loop);
  g_free (igd);

  return 0;
}

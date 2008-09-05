
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>

#include "fs-upnp-simple-igd-thread.h"

static void
_mapped_external_port (FsUpnpSimpleIgd *igd, gchar *proto,
    gchar *external_ip, gchar *replaces_external_ip, guint external_port,
    gchar *local_ip, guint local_port,
    gchar *description, gpointer user_data)
{
  g_debug ("proto:%s ex:%s oldex:%s exp:%u local:%s localp:%u desc:%s",
      proto, external_ip, replaces_external_ip, external_port, local_ip,
      local_port, description);

}



static void
_error_mapping_external_port (FsUpnpSimpleIgd *igd, GError *error,
    gchar *proto, guint external_port,
    gchar *description, gpointer user_data)
{
  g_error ("proto:%s port:%u desc:%s error: %s", proto, external_port,
      description, error->message);
}


static void
_error (FsUpnpSimpleIgd *igd, GError *error, gpointer user_data)
{
  g_error ("error: %s", error->message);
}

int
main (int argc, char **argv)
{
  FsUpnpSimpleIgdThread *igd = NULL;
  guint external_port, internal_port;


  if (argc != 5)
  {
    g_print ("Usage: %s <external port> <local ip> <local port> <description>\n",
        argv[0]);
    return 0;
  }

  external_port = atoi (argv[1]);
  internal_port = atoi (argv[3]);
  g_return_val_if_fail (external_port && internal_port, 1);

  g_type_init ();
  g_thread_init (NULL);

  igd = fs_upnp_simple_igd_thread_new ();

  g_signal_connect (igd, "mapped-external-port",
      G_CALLBACK (_mapped_external_port),
      NULL);
  g_signal_connect (igd, "error", G_CALLBACK (_error),
      NULL);
  g_signal_connect (igd, "error-mapping-port",
      G_CALLBACK (_error_mapping_external_port),
      NULL);

  fs_upnp_simple_igd_add_port (FS_UPNP_SIMPLE_IGD (igd),
      "TCP", external_port, argv[2],
      internal_port, 20, argv[4]);

  sleep (30);

  fs_upnp_simple_igd_remove_port (FS_UPNP_SIMPLE_IGD (igd), "TCP",
      external_port);

  sleep (5);

  g_object_unref (igd);

  return 0;
}

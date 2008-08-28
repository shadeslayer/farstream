
#include <libgupnp/gupnp-control-point.h>
#include <glib.h>


static void
service_proxy_avail (GUPnPControlPoint *control_point,
    GUPnPServiceProxy *proxy,
    gpointer           user_data)
{
  GError *error = NULL;
  gchar *ip = NULL;

  g_debug ("Serv type: %s",
      gupnp_service_info_get_service_type (GUPNP_SERVICE_INFO (proxy)));

  if (gupnp_service_proxy_send_action (proxy, "GetExternalIPAddress",
          &error,
          NULL,
          "NewExternalIPAddress", G_TYPE_STRING, &ip, NULL))
  {
    g_debug ("Got ip %s", ip);
  }
  else
  {
    g_warning ("got Error: %s", error->message);
  }
  g_clear_error (&error);
}

int
main (int argc, char **argv)
{
  GError *error = NULL;
  GUPnPContext *context;
  GUPnPControlPoint *cp;
  GMainLoop *loop;
  const GList *item;

  g_type_init ();
  g_thread_init (NULL);

  loop = g_main_loop_new (NULL, FALSE);

  context = gupnp_context_new (NULL, NULL, 0, &error);
  if (error) {
    g_critical (error->message);
    g_error_free (error);

    return 1;
  }

  cp = gupnp_control_point_new (context, "urn:schemas-upnp-org:service:WANIPConnection:1");


 g_signal_connect (cp, "service-proxy-available",
     G_CALLBACK (service_proxy_avail), NULL);

 gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (cp), TRUE);

 g_main_loop_run (loop);


 g_object_unref (cp);
 g_object_unref (context);
 g_object_unref (loop);

 return 0;
}

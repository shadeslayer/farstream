/* Farsigh2 unit tests for FsCodec
 *
 * Copyright (C) 2007 Collabora, Nokia
 * @author: Olivier Crete <olivier.crete@collabora.co.uk>
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
# include <config.h>
#endif

#include <string.h>

#include <gst/check/gstcheck.h>
#include <ext/fsupnp/fs-upnp-simple-igd.h>
#include <ext/fsupnp/fs-upnp-simple-igd-thread.h>

#include <libgupnp/gupnp.h>

static GMainLoop *loop = NULL;


GST_START_TEST (test_fsupnp_new)
{
  FsUpnpSimpleIgd *igd = fs_upnp_simple_igd_new (NULL);
  FsUpnpSimpleIgdThread *igdthread = fs_upnp_simple_igd_thread_new ();
  FsUpnpSimpleIgdThread *igdthread1 = fs_upnp_simple_igd_thread_new ();

  g_object_unref (igd);
  g_object_unref (igdthread);
  g_object_unref (igdthread1);
}
GST_END_TEST;



static void
get_external_ip_address_cb (GUPnPService *service,
    GUPnPServiceAction *action,
    gpointer user_data)
{
  gupnp_service_action_set (action,
      "NewExternalIPAddress", G_TYPE_STRING, "127.0.0.3",
      NULL);
  gupnp_service_action_return (action);

}

static void
add_port_mapping_cb (GUPnPService *service,
    GUPnPServiceAction *action,
    gpointer user_data)
{
  gchar *remote_host = NULL;
  guint external_port = 0;
  gchar *proto = NULL;
  guint internal_port = 0;
  gchar *internal_client = NULL;
  gboolean enabled = -1;
  gchar *desc = NULL;
  guint lease = 0;

  gupnp_service_action_get (action,
      "NewRemoteHost", G_TYPE_STRING, &remote_host,
      "NewExternalPort", G_TYPE_UINT, &external_port,
      "NewProtocol", G_TYPE_STRING, &proto,
      "NewInternalPort", G_TYPE_UINT, &internal_port,
      "NewInternalClient", G_TYPE_STRING, &internal_client,
      "NewEnabled", G_TYPE_BOOLEAN, &enabled,
      "NewPortMappingDescription", G_TYPE_STRING, &desc,
      "NewLeaseDuration", G_TYPE_UINT, &lease,
      NULL);

  fail_unless (remote_host && !strcmp (remote_host, ""), "Remote host invalid");
  fail_unless (external_port == 6543, "wrong external port");
  fail_unless (proto && (!strcmp (proto, "UDP") || !strcmp (proto, "TCP")));
  fail_unless (internal_port == 6543, "wrong internal port");
  fail_unless (internal_client && !strcmp (internal_client, "192.168.4.22"));
  fail_unless (enabled == TRUE, "enable is not true");
  fail_unless (desc != NULL, "no desc");
  fail_unless (lease == 10, "no lease");

  gupnp_service_action_return (action);
}


static void
delete_port_mapping_cb (GUPnPService *service,
    GUPnPServiceAction *action,
    gpointer user_data)
{
  gchar *remote_host = NULL;
  guint external_port = 0;
  gchar *proto = NULL;

  gupnp_service_action_get (action,
      "NewRemoteHost", G_TYPE_STRING, &remote_host,
      "NewExternalPort", G_TYPE_UINT, &external_port,
      "NewProtocol", G_TYPE_STRING, &proto,
      NULL);

  fail_if (remote_host == NULL, "remote host NULL on remove");
  fail_unless (external_port, "external port wrong on remove");
  fail_unless (proto && !strcmp (proto, "UDP"), "proto wrong on remove");

  gupnp_service_action_return (action);

  g_main_loop_quit (loop);
}

static void
mapping_external_port_cb (FsUpnpSimpleIgd *igd, gchar *proto,
    gchar *external_ip, gchar *replaces_external_ip, guint external_port,
    gchar *local_ip, guint local_port, gchar *description, gpointer user_data)
{
  GUPnPService *service = GUPNP_SERVICE (user_data);

  fail_unless (external_port == 6543, "wrong external port");
  fail_unless (proto && !strcmp (proto, "UDP"));
  fail_unless (local_port == 6543, "wrong internal port");
  fail_unless (local_ip && !strcmp (local_ip, "192.168.4.22"));
  fail_unless (description != NULL, "no description");

  if (replaces_external_ip)
  {
    fail_unless (!strcmp (replaces_external_ip, "127.0.0.3"));
    fail_unless (external_ip && !strcmp (external_ip, "127.0.0.2"));
    fs_upnp_simple_igd_remove_port (igd, "UDP", external_port);
  }
  else
  {
    fail_unless (external_ip && !strcmp (external_ip, "127.0.0.3"));
    gupnp_service_notify (service,
        "ExternalIPAddress", G_TYPE_STRING, "127.0.0.2", NULL);
  }
}

static void
error_mapping_port_cb (FsUpnpSimpleIgd *igd, GError *error, gchar *proto,
    guint external_port, gchar *description, gpointer user_data)
{
  fail ("Error mapping external port: %s", error->message);
}


static void
run_fsupnp_test (GMainContext *mainctx, FsUpnpSimpleIgd *igd)
{
  GUPnPContext *context;
  GUPnPRootDevice *dev;
  GUPnPServiceInfo *service;
  GUPnPDeviceInfo *subdev1;
  GUPnPDeviceInfo *subdev2;

  context = gupnp_context_new (mainctx, NULL, 0, NULL);
  fail_if (context == NULL, "Can't get gupnp context");

  gupnp_context_host_path (context, "upnp/InternetGatewayDevice.xml", "/InternetGatewayDevice.xml");
  gupnp_context_host_path (context, "upnp/WANIPConnection.xml", "/WANIPConnection.xml");

  dev = gupnp_root_device_new (context, "/InternetGatewayDevice.xml");
  fail_if (dev == NULL, "could not get root dev");
  gupnp_root_device_set_available (dev, TRUE);

  subdev1 = gupnp_device_info_get_device (GUPNP_DEVICE_INFO (dev),
      "urn:schemas-upnp-org:device:WANDevice:1");
  fail_if (subdev1 == NULL, "Could not get WANDevice");

  subdev2 = gupnp_device_info_get_device (subdev1,
      "urn:schemas-upnp-org:device:WANConnectionDevice:1");
  fail_if (subdev2 == NULL, "Could not get WANConnectionDevice");

  service = gupnp_device_info_get_service (subdev2,
      "urn:schemas-upnp-org:service:WANIPConnection:1");
  fail_if (service == NULL, "Could not get WANIPConnection");

  g_signal_connect (service, "action-invoked::GetExternalIPAddress",
      G_CALLBACK (get_external_ip_address_cb), NULL);
  g_signal_connect (service, "action-invoked::AddPortMapping",
      G_CALLBACK (add_port_mapping_cb), NULL);
  g_signal_connect (service, "action-invoked::DeletePortMapping",
      G_CALLBACK (delete_port_mapping_cb), NULL);

  g_signal_connect (igd, "mapped-external-port",
      G_CALLBACK (mapping_external_port_cb), service);
  g_signal_connect (igd, "error-mapping-port",
      G_CALLBACK (error_mapping_port_cb), NULL);

  fs_upnp_simple_igd_add_port (igd, "UDP", 6543, "192.168.4.22",
      6543, 10, "Farsight test");

  loop = g_main_loop_new (mainctx, FALSE);

  g_main_loop_run (loop);

  g_object_unref (context);
}

GST_START_TEST (test_fsupnp_default_ctx)
{
  FsUpnpSimpleIgd *igd = fs_upnp_simple_igd_new (NULL);

  run_fsupnp_test (NULL, igd);
  g_object_unref (igd);
}
GST_END_TEST;

GST_START_TEST (test_fsupnp_custom_ctx)
{
  GMainContext *mainctx = g_main_context_new ();
  FsUpnpSimpleIgd *igd = fs_upnp_simple_igd_new (mainctx);

  run_fsupnp_test (mainctx, igd);
  g_object_unref (igd);
  g_main_context_unref (mainctx);
}
GST_END_TEST;


GST_START_TEST (test_fsupnp_thread)
{
  FsUpnpSimpleIgdThread *igd = fs_upnp_simple_igd_thread_new ();
  GMainContext *mainctx = g_main_context_new ();

  run_fsupnp_test (mainctx, FS_UPNP_SIMPLE_IGD (igd));
  g_object_unref (igd);
  g_main_context_unref (mainctx);
}
GST_END_TEST;


static Suite *
fsupnp_suite (void)
{
  Suite *s = suite_create ("fsupnp");
  TCase *tc_chain = tcase_create ("fsupnp");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_fsupnp_new);
  tcase_add_test (tc_chain, test_fsupnp_default_ctx);
  tcase_add_test (tc_chain, test_fsupnp_custom_ctx);
  tcase_add_test (tc_chain, test_fsupnp_thread);

  return s;
}

GST_CHECK_MAIN (fsupnp);

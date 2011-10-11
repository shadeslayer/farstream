/* Farstream unit tests for FsRawUdpTransmitter UPnP code
 *
 * Copyright (C) 2009 Collabora, Nokia
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

#include "check-threadsafe.h"


gboolean got_address = FALSE;
gboolean added_mapping = FALSE;

void
get_vars (gboolean *out_got_address,
    gboolean *out_added_mapping)
{
  *out_got_address = got_address;
  *out_added_mapping = added_mapping;
}


#ifdef HAVE_GUPNP

#include <libgupnp/gupnp.h>

static void
get_external_ip_address_cb (GUPnPService *service,
    GUPnPServiceAction *action,
    gpointer user_data)
{
  gupnp_service_action_set (action,
      "NewExternalIPAddress", G_TYPE_STRING, "127.0.0.1",
      NULL);
  gupnp_service_action_return (action);
  got_address = TRUE;
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

  ts_fail_unless (remote_host && !strcmp (remote_host, ""), "Remote host invalid");
  ts_fail_unless (external_port == internal_port, "External and internal ports different");
  ts_fail_unless (proto && (!strcmp (proto, "UDP") || !strcmp (proto, "TCP")));
  ts_fail_unless (enabled == TRUE, "enable is not true");
  ts_fail_unless (desc != NULL, "no desc");

  g_free (remote_host);
  g_free (proto);
  g_free (internal_client);
  g_free (desc);

  gupnp_service_action_return (action);
  added_mapping = TRUE;
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

  ts_fail_if (remote_host == NULL, "remote host NULL on remove");
  ts_fail_unless (external_port, "external port wrong on remove");
  ts_fail_unless (proto && !strcmp (proto, "UDP"), "proto wrong on remove");

  gupnp_service_action_return (action);
}


GObject *
start_upnp_server (void)
{
  GUPnPContext *context;
  GUPnPRootDevice *dev;
  GUPnPServiceInfo *service;
  GUPnPDeviceInfo *subdev1;
  GUPnPDeviceInfo *subdev2;
  const gchar *upnp_xml_path;

  context = gupnp_context_new (NULL, NULL, 0, NULL);
  ts_fail_if (context == NULL, "Can't get gupnp context");

  if (g_getenv ("UPNP_XML_PATH"))
    upnp_xml_path = g_getenv ("UPNP_XML_PATH");
  else
    upnp_xml_path  = ".";

  gupnp_context_host_path (context, upnp_xml_path, "");

#ifdef HAVE_GUPNP_013
  dev = gupnp_root_device_new (context, "InternetGatewayDevice.xml",
      upnp_xml_path);
#else
  dev = gupnp_root_device_new (context, "/InternetGatewayDevice.xml");
#endif
  ts_fail_if (dev == NULL, "could not get root dev");

  subdev1 = gupnp_device_info_get_device (GUPNP_DEVICE_INFO (dev),
      "urn:schemas-upnp-org:device:WANDevice:1");
  ts_fail_if (subdev1 == NULL, "Could not get WANDevice");

  subdev2 = gupnp_device_info_get_device (subdev1,
      "urn:schemas-upnp-org:device:WANConnectionDevice:1");
  ts_fail_if (subdev2 == NULL, "Could not get WANConnectionDevice");
  g_object_unref (subdev1);

  service = gupnp_device_info_get_service (subdev2,
      "urn:schemas-upnp-org:service:WANIPConnection:1");
  ts_fail_if (service == NULL, "Could not get WANIPConnection");
  g_object_unref (subdev2);

  g_signal_connect (service, "action-invoked::GetExternalIPAddress",
      G_CALLBACK (get_external_ip_address_cb), NULL);
  g_signal_connect (service, "action-invoked::AddPortMapping",
      G_CALLBACK (add_port_mapping_cb), NULL);
  g_signal_connect (service, "action-invoked::DeletePortMapping",
      G_CALLBACK (delete_port_mapping_cb), NULL);

  gupnp_root_device_set_available (dev, TRUE);

  return G_OBJECT (context);
}

#else

GObject *
start_upnp_server (void)
{
  return NULL;
}

#endif

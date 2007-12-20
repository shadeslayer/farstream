/*
 * farsight-interfaces.c - Source for interface discovery code
 *
 * Farsight Helper functions
 * Copyright (C) 2006 Youness Alaoui <kakaroto@kakaroto.homelinux.net>
 * Copyright (C) 2007 Collabora, Nokia
 * @author: Youness Alaoui <kakaroto@kakaroto.homelinux.net>
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

#include "fs-interfaces.h"


#ifdef HAVE_CONFIG_H
 #include "config.h"
#endif

#if 0
#define DEBUG(args...) g_debug (args)
#else
#define DEBUG(args...) while(0) {}
#endif


#ifdef G_OS_UNIX

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#ifdef HAVE_GETIFADDRS
 #include <sys/socket.h>
 #include <ifaddrs.h>
#endif
#include <net/if.h>
#include <net/if_arp.h>
#include <arpa/inet.h>

/**
 * farsight_get_local_interfaces:
 *
 * Get the list of local interfaces
 *
 * Returns: a #GList of strings.
 */
#ifdef HAVE_GETIFADDRS
GList *
farsight_get_local_interfaces(void)
{
  GList *interfaces = NULL;
  struct ifaddrs *ifa, *results;

  if (getifaddrs (&results) < 0) {
    if (errno == ENOMEM)
      return NULL;
    else
      return NULL;
  }

  /* Loop and get each interface the system has, one by one... */
  for (ifa = results; ifa; ifa = ifa->ifa_next) {
    /* no ip address from interface that is down */
    if ((ifa->ifa_flags & IFF_UP) == 0)
      continue;

    if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
      continue;

    DEBUG("Found interface : %s", ifa->ifa_name);
    interfaces = g_list_prepend (interfaces, g_strdup(ifa->ifa_name));
  }

  return interfaces;
}

#else /* ! HAVE_GETIFADDRS */

GList *
farsight_get_local_interfaces (void)
{
  GList *interfaces = NULL;
  gint sockfd;
  gint size = 0;
  struct ifreq *ifr;
  struct ifconf ifc;

  if (0 > (sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_IP))) {
    g_warning ("Cannot open socket to retreive interface list");
    return NULL;
  }

  ifc.ifc_len = 0;
  ifc.ifc_req = NULL;

  /* Loop and get each interface the system has, one by one... */
  do {
    size += sizeof(struct ifreq);
    /* realloc buffer size until no overflow occurs  */
    if (NULL == (ifc.ifc_req = realloc (ifc.ifc_req, size))) {
      g_warning ("Out of memory while allocation interface configuration structure");
      close (sockfd);
      return NULL;
    }
    ifc.ifc_len = size;

    if (ioctl (sockfd, SIOCGIFCONF, &ifc)) {
      perror ("ioctl SIOCFIFCONF");
      close (sockfd);
      return NULL;
    }
  } while (size <= ifc.ifc_len);


  /* Loop throught the interface list and get the IP address of each IF */
  for (ifr = ifc.ifc_req;
       (gchar *) ifr < (gchar *) ifc.ifc_req + ifc.ifc_len;
       ++ifr) {
    DEBUG ("Found interface : %s", ifr->ifr_name);
    interfaces = g_list_prepend (interfaces, g_strdup(ifr->ifr_name));
  }

  close(sockfd);

  return interfaces;
}
#endif /* HAVE_GETIFADDRS */


static gboolean
farsight_is_private_ip (const struct in_addr in)
{
  /* 10.x.x.x/8 */
  if (in.s_addr >> 24 == 0x0A)
    return TRUE;

  /* 172.16.0.0 - 172.31.255.255 = 172.16.0.0/10 */
  if (in.s_addr >> 22 == 0x2B0)
    return TRUE;

  /* 192.168.x.x/16 */
  if (in.s_addr >> 16 == 0xc0A8)
    return TRUE;

  /* 169.254.x.x/16  (for APIPA) */
  if (in.s_addr >> 16 == 0xA9FE)
    return TRUE;

  return FALSE;
}

/**
 * farsight_get_local_ips:
 * @include_loopback: Include any loopback devices
 *
 * Get a list of local ip4 interface addresses
 *
 * Returns: A #GList of strings
 */

#ifdef HAVE_GETIFADDRS

GList *
farsight_get_local_ips (gboolean include_loopback)
{
  GList *ips = NULL;
  struct sockaddr_in *sa;
  struct ifaddrs *ifa, *results;
  gchar *loopback = NULL;


  if (getifaddrs (&results) < 0)
      return NULL;

  /* Loop through the interface list and get the IP address of each IF */
  for (ifa = results; ifa; ifa = ifa->ifa_next)
  {
    /* no ip address from interface that is down */
    if ((ifa->ifa_flags & IFF_UP) == 0)
      continue;

    if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
      continue;

    sa = (struct sockaddr_in *) ifa->ifa_addr;

    DEBUG("Interface:  %s", ifa->ifa_name);
    DEBUG("IP Address: %s", inet_ntoa(sa->sin_addr));
    if ((ifa->ifa_flags & IFF_LOOPBACK) == IFF_LOOPBACK)
    {
      if (include_loopback)
        loopback = g_strdup (inet_ntoa (sa->sin_addr));
      else
        DEBUG("Ignoring loopback interface");
    }
    else
    {
      if (farsight_is_private_ip (sa->sin_addr))
        ips = g_list_append (ips, g_strdup (inet_ntoa (sa->sin_addr)));
      else
        ips = g_list_prepend (ips, g_strdup (inet_ntoa (sa->sin_addr)));
    }
  }

  freeifaddrs (results);

  if (loopback)
    ips = g_list_append (ips, loopback);

  return ips;
}

#else /* ! HAVE_GETIFADDRS */

GList *
farsight_get_local_ips (gboolean include_loopback)
{
  GList *ips = NULL;
  gint sockfd;
  gint size = 0;
  struct ifreq *ifr;
  struct ifconf ifc;
  struct sockaddr_in *sa;

  if (0 > (sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_IP))) {
    g_warning("Cannot open socket to retreive interface list");
    return NULL;
  }

  ifc.ifc_len = 0;
  ifc.ifc_req = NULL;

  /* Loop and get each interface the system has, one by one... */
  do {
    size += sizeof(struct ifreq);
    /* realloc buffer size until no overflow occurs  */
    if (NULL == (ifc.ifc_req = realloc (ifc.ifc_req, size))) {
      g_warning ("Out of memory while allocation interface configuration"
        " structure");
      close (sockfd);
      return NULL;
    }
    ifc.ifc_len = size;

    if (ioctl (sockfd, SIOCGIFCONF, &ifc)) {
      perror ("ioctl SIOCFIFCONF");
      close (sockfd);
      return NULL;
    }
  } while  (size <= ifc.ifc_len);


  /* Loop throught the interface list and get the IP address of each IF */
  for (ifr = ifc.ifc_req;
       (gchar *) ifr < (gchar *) ifc.ifc_req + ifc.ifc_len;
       ++ifr) {

    if (ioctl (sockfd, SIOCGIFFLAGS, ifr)) {
      g_warning ("Unable to get IP information for interface %s. Skipping...",
        ifr->ifr_name);
      continue;  /* failed to get flags, skip it */
    }
    sa = (struct sockaddr_in *) &ifr->ifr_addr;
    DEBUG("Interface:  %s", ifr->ifr_name);
    DEBUG("IP Address: %s", inet_ntoa(sa->sin_addr));
    if (!include_loopback && (ifr->ifr_flags & IFF_LOOPBACK) == IFF_LOOPBACK){
      DEBUG("Ignoring loopback interface");
    } else {
      if (farsight_is_private_ip (sa->sin_addr)) {
        ips = g_list_append (ips, g_strdup (inet_ntoa (sa->sin_addr)));
      } else {
        ips = g_list_prepend (ips, g_strdup (inet_ntoa (sa->sin_addr)));
      }
    }
  }

  close(sockfd);

  return ips;
}

#endif /* HAVE_GETIFADDRS */


/**
 * farsight_get_ip_for_interface:
 * @interface_name: name of local interface
 *
 * Retreives the IP Address of an interface by its name
 *
 * Returns:
 **/
gchar *
farsight_get_ip_for_interface (gchar *interface_name)
{
  struct ifreq ifr;
  struct sockaddr_in *sa;
  gint sockfd;


  ifr.ifr_addr.sa_family = AF_INET;
  memset (ifr.ifr_name, 0, sizeof(ifr.ifr_name));
  strcpy (ifr.ifr_name, interface_name);

  if (0 > (sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_IP))) {
    g_warning("Cannot open socket to retreive interface list");
    return NULL;
  }

  if (ioctl (sockfd, SIOCGIFADDR, &ifr) < 0) {
    g_warning ("Unable to get IP information for interface %s",
      interface_name);
    close (sockfd);
    return NULL;
  }

  close (sockfd);
  sa = (struct sockaddr_in *) &ifr.ifr_addr;
  DEBUG ("Address for %s: %s", interface_name, inet_ntoa (sa->sin_addr));
  return inet_ntoa(sa->sin_addr);
}

#else /* G_OS_UNIX */
#ifdef G_OS_WIN32

#include <windows.h>
#include <winsock.h>

static gboolean started_wsa_engine = FALSE;

#error Windows support is not yet implemented

/**
 * private function that initializes the WinSock engine and
 *  returns a prebuilt socket
 **/
SOCKET farsight_get_WSA_socket() {

  WORD wVersionRequested;
  WSADATA wsaData;
  int err;
  SOCKET sock;

  if (started_wsa_engine == FALSE) {
    wVersionRequested = MAKEWORD( 2, 0 );

    err = WSAStartup( wVersionRequested, &wsaData );
    if ( err != 0 ) {
      g_warning("Could not start the winsocket engine");
      return INVALID_SOCKET;
    }
    started_wsa_engine = TRUE;
  }


  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
    g_warning("Could not open socket to retreive interface list, error no : %d", WSAGetLastError());
    return INVALID_SOCKET;
  }

  return sock;
}

/**
 * Returns the list of local interfaces
 **/
GList * farsight_get_local_interfaces()
{
  return NULL;
}


/**
 * Returns the list of local ips
 **/
GList * farsight_get_local_ips()
{
  return NULL;
}

/**
 * retreives the IP Address of an interface by its name
 **/
gchar * farsight_get_ip_for_interface(gchar *interface_name)
{
  return NULL;

}


#else /* G_OS_WIN32 */
#error Can\'t use this method for retreiving ip list from OS other than unix or windows
#endif /* G_OS_WIN32 */
#endif /* G_OS_UNIX */

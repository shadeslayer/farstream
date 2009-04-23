/* Farsight 2 unit tests generic utilities
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

#include "testutils.h"

#ifdef HAVE_GETIFADDRS
 #include <sys/socket.h>
 #include <ifaddrs.h>
 #include <net/if.h>
 #include <arpa/inet.h>
#endif

gchar *
find_multicast_capable_address (void)
{
#ifdef HAVE_GETIFADDRS
  gchar *retval = NULL;
  struct ifaddrs *ifa, *results;

  if (getifaddrs (&results) < 0)
    return NULL;

  for (ifa = results; ifa; ifa = ifa->ifa_next) {
    /* no ip address from interface that is down */
    if ((ifa->ifa_flags & IFF_UP) == 0)
      continue;

    if ((ifa->ifa_flags & IFF_MULTICAST) == 0)
      continue;

    if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
      continue;

    if (retval)
    {
      g_free (retval);
      retval = NULL;
      g_debug ("Disabling test, more than one multicast capable interface");
      break;
    }

    retval = g_strdup (
        inet_ntoa (((struct sockaddr_in *) ifa->ifa_addr)->sin_addr));
    g_debug ("Sending from %s on interface %s", retval, ifa->ifa_name);
  }

  freeifaddrs (results);

  if (retval == NULL)
    g_message ("Skipping multicast transmitter tests, "
        "no multicast capable interface found");
  return retval;

#else
  g_message ("This system does not have getifaddrs,"
      " this test will be disabled");
  return NULL;
#endif
}

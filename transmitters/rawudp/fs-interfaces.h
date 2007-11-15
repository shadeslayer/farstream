/*
 * farsight-interfaces.h - Source for interface discovery code
 *
 * Farsight Helper functions
 * Copyright (C) 2006 Youness Alaoui <kakaroto@kakaroto.homelinux.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __FARSIGHT_INTERFACES_H_ 
#define __FARSIGHT_INTERFACES_H_ 

#include <glib.h>

gchar * farsight_get_ip_for_interface(gchar *interface_name);
GList * farsight_get_local_ips(gboolean include_loopback);
GList * farsight_get_local_interfaces();

#endif

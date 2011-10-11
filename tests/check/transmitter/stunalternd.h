/* Farstream unit tests for FsRawUdpTransmitter
 * This file is taken from the Nice GLib ICE library. 
 *
 * (C) 2007-2009 Nokia Corporation
 *  @contributor: Rémi Denis-Courmont
 * (C) 2008-2009 Collabora Ltd
 *  @author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
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

#ifndef __STUNALTERND_H__
#define __STUNALTERND_H__

#include <sys/socket.h>
#include <pthread.h>

void *stun_alternd_init (int family,
    char *redirect_ip,
    unsigned int redirect_port,
    unsigned int listen_port);

void stun_alternd_stop (void *data);

#endif /* __STUNALTERND_H__ */

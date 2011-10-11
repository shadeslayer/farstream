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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <sys/types.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>

#include <pthread.h>

#ifndef SOL_IP
# define SOL_IP IPPROTO_IP
#endif

#ifndef SOL_IPV6
# define SOL_IPV6 IPPROTO_IPV6
#endif

#ifndef IPV6_RECVPKTINFO
# define IPV6_RECVPKTINFO IPV6_PKTINFO
#endif

/** Default port for STUN binding discovery */
#define IPPORT_STUN  3478

#include <stun/stunagent.h>
#include "stunalternd.h"

static const uint16_t known_attributes[] =  {
  0
};

/**
 * Creates a listening socket
 */
int listen_socket (int fam, int type, int proto, unsigned int port)
{
  int yes = 1;
  int fd = socket (fam, type, proto);
  union {
    struct sockaddr addr;
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
    struct sockaddr_storage storage;
  } addr;
  socklen_t socklen;

  if (fd == -1)
  {
    perror ("Error creating socket");
    return -1;
  }
  if (fd < 3)
    goto error;

  memset (&addr, 0, sizeof (addr));
  addr.storage.ss_family = fam;

  switch (fam)
  {
    case AF_INET:
      addr.in.sin_port = htons (port);
      socklen = sizeof (struct sockaddr_in);
      break;

    case AF_INET6:
      addr.in6.sin6_port = htons (port);
      socklen = sizeof (struct sockaddr_in6);
      break;
    default:
      socklen = 0;
      abort ();
  }

  if (bind (fd, (struct sockaddr *)&addr, socklen))
  {
    perror ("Error binding to port");
    goto error;
  }

  if ((type == SOCK_DGRAM) || (type == SOCK_RAW))
  {
    switch (fam)
    {
#ifdef IP_RECVERR
      case AF_INET:
        setsockopt (fd, SOL_IP, IP_RECVERR, &yes, sizeof (yes));
        break;
#endif
#ifdef IPV6_RECVERR
      case AF_INET6:
        setsockopt (fd, SOL_IPV6, IPV6_RECVERR, &yes, sizeof (yes));
        break;
#endif
    }
  }
  else
  {
    if (listen (fd, INT_MAX))
    {
      perror ("Error listening on port");
      goto error;
    }
  }

  return fd;

error:
  close (fd);
  return -1;
}


/** Dequeue error from a socket if applicable */
static int recv_err (int fd)
{
  struct msghdr hdr;
#ifdef MSG_ERRQUEUE
  memset (&hdr, 0, sizeof (hdr));
  return recvmsg (fd, &hdr, MSG_ERRQUEUE) >= 0;
#endif
}


/** Receives a message or dequeues an error from a socket */
ssize_t recv_safe (int fd, struct msghdr *msg)
{
  ssize_t len = recvmsg (fd, msg, 0);
  if (len == -1)
    recv_err (fd);
  else
  if (msg->msg_flags & MSG_TRUNC)
  {
    errno = EMSGSIZE;
    return -1;
  }

  return len;
}


/** Sends a message through a socket */
ssize_t send_safe (int fd, const struct msghdr *msg)
{
  ssize_t len;

  do
    len = sendmsg (fd, msg, 0);
  while ((len == -1) && (recv_err (fd) == 0));

  return len;
}


static int dgram_process (int sock, StunAgent *oldagent, StunAgent *newagent,
    struct sockaddr *alt_addr, socklen_t alt_addr_len)
{
  struct sockaddr_storage addr;
  uint8_t buf[STUN_MAX_MESSAGE_SIZE];
  char ctlbuf[256];
  struct iovec iov = { buf, sizeof (buf) };
  StunMessage request;
  StunMessage response;
  StunValidationStatus validation;
  StunAgent *agent = NULL;

  struct msghdr mh =
  {
    .msg_name = (struct sockaddr *)&addr,
    .msg_namelen = sizeof (addr),
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = ctlbuf,
    .msg_controllen = sizeof (ctlbuf)
  };

  size_t len = recv_safe (sock, &mh);
  if (len == (size_t)-1)
    return -1;

  validation = stun_agent_validate (newagent, &request, buf, len, NULL, 0);

  if (validation == STUN_VALIDATION_SUCCESS) {
    agent = newagent;
  }
  else {
    validation = stun_agent_validate (oldagent, &request, buf, len, NULL, 0);
    agent = oldagent;
  }

  /* Unknown attributes */
  if (validation == STUN_VALIDATION_UNKNOWN_REQUEST_ATTRIBUTE)
  {
    stun_agent_build_unknown_attributes_error (agent, &response, buf,
        sizeof (buf), &request);
    goto send_buf;
  }

  /* Mal-formatted packets */
  if (validation != STUN_VALIDATION_SUCCESS ||
      stun_message_get_class (&request) != STUN_REQUEST) {
    return -1;
  }

  switch (stun_message_get_method (&request))
  {
    case STUN_BINDING:
      stun_agent_init_error (agent, &response, buf, sizeof (buf), &request,
          STUN_ERROR_TRY_ALTERNATE);
      stun_message_append_addr (&response, STUN_ATTRIBUTE_ALTERNATE_SERVER,
          alt_addr, alt_addr_len);
      break;

    default:
      stun_agent_init_error (agent, &response, buf, sizeof (buf),
          &request, STUN_ERROR_BAD_REQUEST);
  }

  iov.iov_len = stun_agent_finish_message (agent, &response, NULL, 0);
send_buf:

  len = send_safe (sock, &mh);
  return (len < iov.iov_len) ? -1 : 0;
}



static int
resolve_addr (char *server, unsigned int port, int family,
    struct sockaddr *addr, socklen_t *addr_len)
{
  struct addrinfo hints, *res;
  int ret = -1;
  char portstr[10];

  memset (&hints, 0, sizeof (hints));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_NUMERICHOST;

  snprintf (portstr, 9, "%u", port);

  ret = getaddrinfo (server, portstr, &hints, &res);
  if (ret)
  {
    fprintf (stderr, "%s: %s:%s\n", server, portstr,
             gai_strerror (ret));
    return 0;
  }

  memcpy (addr, res->ai_addr, res->ai_addrlen);
  *addr_len = res->ai_addrlen;

  freeaddrinfo (res);

  return 1;
}

struct thread_data {
  pthread_t thread;
  struct sockaddr_storage alt_addr;
  socklen_t alt_addr_len;
  StunAgent oldagent;
  StunAgent newagent;
  int sock;
};

void * stund_thread (void *data)
{
  struct thread_data *td = data;

  pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);

  for (;;)
    dgram_process (td->sock, &td->oldagent, &td->newagent,
        (struct sockaddr*) &td->alt_addr, td->alt_addr_len);


  return NULL;
}

void *stun_alternd_init (int family, char *redirect_ip,
    unsigned int redirect_port,
    unsigned int listen_port)
{
  struct thread_data *td;

  td = malloc (sizeof(struct thread_data));

  if (!redirect_port)
    redirect_port = IPPORT_STUN;

  if (!listen_port)
    listen_port = IPPORT_STUN;

  if (!resolve_addr (redirect_ip, redirect_port, family,
          (struct sockaddr *)&td->alt_addr, &td->alt_addr_len))
  {
    free (td);
    return NULL;
  }

  td->sock = listen_socket (family, SOCK_DGRAM, IPPROTO_UDP, listen_port);
  if (td->sock == -1)
  {
    free (td);
    return NULL;
  }

  stun_agent_init (&td->oldagent, known_attributes,
      STUN_COMPATIBILITY_RFC3489, 0);
  stun_agent_init (&td->newagent, known_attributes,
      STUN_COMPATIBILITY_RFC5389, STUN_AGENT_USAGE_USE_FINGERPRINT);

  pthread_create (&td->thread, NULL, stund_thread, td);

  return td;
}


void stun_alternd_stop (void *data)
{
  struct thread_data *td = data;

  pthread_cancel (td->thread);
  pthread_join (td->thread, NULL);
  free (data);
}

/*
 * Farsight2 - Farsight MSN Connection
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  @author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * fs-msn-connection.c - A MSN Connection gobject
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
#include "config.h"
#endif

#include "fs-msn-connection.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <gst/gst.h>

/* Signals */
enum
{
  LAST_SIGNAL
};


/* props */
enum
{
  PROP_0,
};


typedef enum {
  FS_MSN_STATUS_AUTH,
  FS_MSN_STATUS_CONNECTED,
  FS_MSN_STATUS_CONNECTED2,
  FS_MSN_STATUS_SEND_RECEIVE,
  FS_MSN_STATUS_PAUSED,
} FsMsnStatus;

typedef struct _FsMsnPollFD FsMsnPollFD;
typedef void (*PollFdCallback) (FsMsnConnection *self, FsMsnPollFD *pollfd);

struct _FsMsnPollFD {
  GstPollFD pollfd;
  FsMsnStatus status;
  gboolean server;
  gboolean want_read;
  gboolean want_write;
  PollFdCallback callback;
};


G_DEFINE_TYPE(FsMsnConnection, fs_msn_connection, G_TYPE_OBJECT);

static GObjectClass *parent_class = NULL;

static void fs_msn_connection_dispose (GObject *object);


static gboolean fs_msn_connection_attempt_connection (
    FsMsnConnection *connection,
    FsCandidate *candidate);
static gboolean fs_msn_open_listening_port (FsMsnConnection *connection,
    guint16 port, NewLocalCandidateCB cb, gpointer data);

static void successful_connection_cb (FsMsnConnection *self, FsMsnPollFD *fd);
static void accept_connection_cb (FsMsnConnection *self, FsMsnPollFD *fd);
static void connection_cb (FsMsnConnection *self, FsMsnPollFD *fd);

static gpointer connection_polling_thread (gpointer data);
static void shutdown_fd (FsMsnConnection *self, FsMsnPollFD *pollfd);
static FsMsnPollFD * add_pollfd (FsMsnConnection *self, int fd,
    PollFdCallback callback, gboolean read, gboolean write);

static void
fs_msn_connection_class_init (FsMsnConnectionClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = fs_msn_connection_dispose;
}

static void
fs_msn_connection_init (FsMsnConnection *self)
{
  /* member init */

  self->disposed = FALSE;

  self->poll_timeout = GST_CLOCK_TIME_NONE;
  self->poll = gst_poll_new (TRUE);
  gst_poll_set_flushing (self->poll, FALSE);
  self->pollfds = g_array_new (TRUE, TRUE, sizeof(FsMsnPollFD *));

  g_static_rec_mutex_init (&self->mutex);
}

static void
fs_msn_connection_dispose (GObject *object)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (object);
  gint i;

  g_static_rec_mutex_lock (&self->mutex);

  /* If dispose did already run, return. */
  if (self->disposed)
  {
    g_static_rec_mutex_unlock (&self->mutex);
    return;
  }

  if (self->polling_thread)
  {
    gst_poll_set_flushing (self->poll, TRUE);
    g_thread_join (self->polling_thread);
    self->polling_thread = NULL;
  }

  if (self->local_recipient_id)
    g_free (self->local_recipient_id);
  if (self->remote_recipient_id)
    g_free (self->remote_recipient_id);

  gst_poll_free (self->poll);

  for (i = 0; i < self->pollfds->len; i++)
    close (g_array_index(self->pollfds, FsMsnPollFD *, i)->pollfd.fd);
  g_array_free (self->pollfds, TRUE);


  /* Make sure dispose does not run twice. */
  self->disposed = TRUE;

  parent_class->dispose (object);

  g_static_rec_mutex_unlock (&self->mutex);
}

/**
 * fs_msn_connection_new:
 * @session: The #FsMsnSession this connection is a child of
 * @participant: The #FsMsnParticipant this connection is for
 * @direction: the initial #FsDirection for this connection
 *
 *
 * This function create a new connection
 *
 * Returns: the newly created string or NULL on error
 */

FsMsnConnection *
fs_msn_connection_new (guint session_id, guint initial_port)
{
  FsMsnConnection *self = g_object_new (FS_TYPE_MSN_CONNECTION, NULL);

  if (self) {
    self->session_id = session_id;
    self->initial_port = initial_port;
  }

  return self;
}

gboolean
fs_msn_connection_gather_local_candidates (FsMsnConnection *self,
    NewLocalCandidateCB cb, gpointer data)
{
  gboolean ret = FALSE;
  g_static_rec_mutex_lock (&self->mutex);

  self->polling_thread = g_thread_create (connection_polling_thread,
      self, TRUE, NULL);

  if (self->polling_thread)
    ret = fs_msn_open_listening_port (self, self->initial_port, cb, data);

  g_static_rec_mutex_unlock (&self->mutex);
  return ret;
}


/**
 * fs_msn_connection_set_remote_candidate:
 */
gboolean
fs_msn_connection_set_remote_candidates (FsMsnConnection *self,
    GList *candidates, GError **error)
{
  GList *item = NULL;
  gchar *recipient_id = NULL;
  gboolean ret = FALSE;

  g_static_rec_mutex_lock (&self->mutex);

  recipient_id = self->remote_recipient_id;

  for (item = candidates; item; item = g_list_next (item))
  {
    FsCandidate *candidate = item->data;

    if (!candidate->ip || !candidate->port)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The candidate passed does not contain a valid ip or port");
      goto out;
    }
    if (!candidate->foundation)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The candidate passed does not have a foundation (MSN recipient ID)");
      goto out;
    }
    if (recipient_id)
    {
      if (g_strcmp0 (candidate->foundation, recipient_id) != 0)
      {
        g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
            "The candidates do not have the same recipient ID");
        goto out;
      }
    }
    else
    {
      recipient_id = candidate->foundation;
    }
  }

  self->remote_recipient_id = g_strdup (recipient_id);
  for (item = candidates; item; item = g_list_next (item))
  {
    FsCandidate *candidate = item->data;
    fs_msn_connection_attempt_connection(self, candidate);
  }

  ret = TRUE;
 out:
  g_static_rec_mutex_unlock (&self->mutex);
  return ret;
}


static gboolean
fs_msn_open_listening_port (FsMsnConnection *self,
    guint16 port, NewLocalCandidateCB cb, gpointer data)
{
  gint fd = -1;
  struct sockaddr_in myaddr;
  memset(&myaddr, 0, sizeof(myaddr));

  g_debug ("Attempting to listen on port %d.....",port);

  if ( (fd = socket(PF_INET, SOCK_STREAM, 0)) == -1 )
  {
    // show error
    g_debug ("could not create socket!");
    return FALSE;
  }

  // set non-blocking mode
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  myaddr.sin_family = AF_INET;
  do
  {
    g_debug ("Attempting to listen on port %d.....",port);
    myaddr.sin_port = htons (port);
    // bind
    if (bind(fd, (struct sockaddr *) &myaddr, sizeof(myaddr)) != 0)
    {
      if (port != 0 && errno == EADDRINUSE)
      {
        port++;
      }
      else
      {
        perror ("bind");
        close (fd);
        return FALSE;
      }
    } else {
      /* Listen */
      if (listen(fd, 3) != 0)
      {
        if (port != 0 && errno == EADDRINUSE)
        {
          port++;
        }
        else
        {
          perror ("listen");
          close (fd);
          return FALSE;
        }
      }
    }
  } while (errno == EADDRINUSE);

  add_pollfd (self, fd, accept_connection_cb, TRUE, TRUE);

  g_debug ("Listening on port %d", port);

  FsCandidate * cand = fs_candidate_new ("123", 1, FS_CANDIDATE_TYPE_HOST,
      FS_NETWORK_PROTOCOL_TCP, "127.0.0.1", port);
  g_static_rec_mutex_unlock (&self->mutex);
  cb (cand, data);
  g_static_rec_mutex_lock (&self->mutex);
  fs_candidate_destroy (cand);

  self->local_recipient_id = g_strdup ("123");

  return TRUE;
}

static gboolean
fs_msn_connection_attempt_connection (FsMsnConnection *connection,
    FsCandidate *candidate)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (connection);
  FsMsnPollFD *pollfd;
  gint fd = -1;
  struct sockaddr_in theiraddr;
  memset(&theiraddr, 0, sizeof(theiraddr));


  if ( (fd = socket(PF_INET, SOCK_STREAM, 0)) == -1 )
  {
    // show error
    g_debug ("could not create socket!");
    return FALSE;
  }

  // set non-blocking mode
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

  theiraddr.sin_family = AF_INET;
  theiraddr.sin_addr.s_addr = inet_addr (candidate->ip);
  theiraddr.sin_port = htons (candidate->port);

  g_debug ("Attempting connection to %s %d on socket %d", candidate->ip,
      candidate->port, fd);
  // this is non blocking, the return value isn't too usefull
  gint ret = connect (fd, (struct sockaddr *) &theiraddr, sizeof (theiraddr));
  if (ret < 0)
  {
    if (errno != EINPROGRESS)
    {
      g_debug("ret %d %d %s", ret, errno, strerror(errno));
      close (fd);
      return FALSE;
    }
  }
  g_debug("ret %d %d %s", ret, errno, strerror(errno));


  pollfd = add_pollfd (self, fd, successful_connection_cb, TRUE, TRUE);
  pollfd->server = FALSE;

  return TRUE;
}

static void
accept_connection_cb (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  struct sockaddr_in in;
  int fd = -1;
  socklen_t n = sizeof (in);
  FsMsnPollFD *newpollfd = NULL;

  if (gst_poll_fd_has_error (self->poll, &pollfd->pollfd) ||
      gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
  {
    g_debug ("Error in accept socket : %d", pollfd->pollfd.fd);
    goto error;
  }

  if ((fd = accept(pollfd->pollfd.fd,
              (struct sockaddr*) &in, &n)) == -1)
  {
    g_debug ("Error while running accept() %d", errno);
    return;
  }

  newpollfd = add_pollfd (self, fd, connection_cb, TRUE, FALSE);
  newpollfd->server = TRUE;

  return;

  /* Error */
 error:
  g_debug ("Got error from fd %d, closing", fd);
  // find, shutdown and remove channel from fdlist
  gint i;
  for (i = 0; i < self->pollfds->len; i++)
  {
    FsMsnPollFD *pollfd2 = g_array_index(self->pollfds, FsMsnPollFD *, i);
    if (pollfd == pollfd2)
    {
      g_debug ("closing fd %d", pollfd2->pollfd.fd);
      shutdown_fd (self, pollfd2);
      i--;
    }
  }

  return;
}


static void
successful_connection_cb (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  gint error;
  socklen_t option_len;

  g_debug ("handler called on fd %d", pollfd->pollfd.fd);

  errno = 0;
  if (gst_poll_fd_has_error (self->poll, &pollfd->pollfd) ||
      gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
  {
    g_debug ("connecton closed or error");
    goto error;
  }

  option_len = sizeof(error);

  /* Get the error option */
  if (getsockopt(pollfd->pollfd.fd, SOL_SOCKET, SO_ERROR, (void*) &error, &option_len) < 0)
  {
    g_warning ("getsockopt() failed");
    goto error;
  }

  /* Check if there is an error */
  if (error)
  {
    g_debug ("getsockopt gave an error : %d", error);
    goto error;
  }

  pollfd->callback = connection_cb;

  g_debug ("connection succeeded on socket %p", pollfd);
  return;

  /* Error */
 error:
  g_debug ("Got error from fd %d, closing", pollfd->pollfd.fd);
  // find, shutdown and remove channel from fdlist
  gint i;
  for (i = 0; i < self->pollfds->len; i++)
  {
    FsMsnPollFD *pollfd2 = g_array_index(self->pollfds, FsMsnPollFD *, i);
    if (pollfd == pollfd2)
    {
      g_debug ("closing fd %d", pollfd2->pollfd.fd);
      shutdown_fd (self, pollfd2);
      i--;
    }
  }

  return;
}


static void
connection_cb (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  gboolean success = FALSE;

  g_debug ("handler called on fd %d. %d %d %d %d", pollfd->pollfd.fd,
      pollfd->server, pollfd->status,
      gst_poll_fd_can_read (self->poll, &pollfd->pollfd),
      gst_poll_fd_can_write (self->poll, &pollfd->pollfd));

  if (gst_poll_fd_has_error (self->poll, &pollfd->pollfd) ||
      gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
  {
    g_debug ("connecton closed or error");
    goto error;
  }

  if (gst_poll_fd_can_read (self->poll, &pollfd->pollfd))
  {
    switch (pollfd->status)
    {
      case FS_MSN_STATUS_AUTH:
        if (pollfd->server)
        {
          gchar str[35] = {0};
          gchar check[35] = {0};

          if (recv(pollfd->pollfd.fd, str, 34, 0) != -1)
          {
            g_debug ("Got %s, checking if it's auth", str);
            sprintf(check, "recipientid=%s&sessionid=%d\r\n\r\n",
                self->local_recipient_id, self->session_id);
            if (strcmp (str, check) == 0)
            {
              g_debug ("Authentication successful");
              pollfd->status = FS_MSN_STATUS_CONNECTED;
              pollfd->want_write = TRUE;
              gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, TRUE);
            }
            else
            {
              g_debug ("Authentication failed");
              goto error;
            }
          }
          else
          {
            perror ("auth");
            goto error;
          }

        } else {
          g_debug ("shouldn't receive data when client on AUTH state");
          goto error;
        }
        break;
      case FS_MSN_STATUS_CONNECTED:
        if (!pollfd->server)
        {
          gchar str[14] = {0};

          if (recv(pollfd->pollfd.fd, str, 13, 0) != -1)
          {
            g_debug ("Got %s, checking if it's connected", str);
            if (strcmp (str, "connected\r\n\r\n") == 0)
            {
              g_debug ("connection successful");
              pollfd->status = FS_MSN_STATUS_CONNECTED2;
              pollfd->want_write = TRUE;
              gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, TRUE);
            }
            else
            {
              g_debug ("connected failed");
              goto error;
            }
          }
          else
          {
            perror ("connected");
            goto error;
          }
        } else {
          g_debug ("shouldn't receive data when server on CONNECTED state");
          goto error;
        }
        break;
      case FS_MSN_STATUS_CONNECTED2:
        if (pollfd->server)
        {
          gchar str[14] = {0};

          if (recv(pollfd->pollfd.fd, str, 13, 0) != -1)
          {
            g_debug ("Got %s, checking if it's connected", str);
            if (strcmp (str, "connected\r\n\r\n") == 0)
            {
              g_debug ("connection successful");
              pollfd->status = FS_MSN_STATUS_SEND_RECEIVE;
              success = TRUE;
            }
            else
            {
              g_debug ("connected failed");
              goto error;
            }
          }
          else
          {
            perror ("connected");
            goto error;
          }

        } else {
          g_debug ("shouldn't receive data when client on CONNECTED2 state");
          goto error;
        }
        break;
      default:
        g_debug ("Invalid status %d", pollfd->status);
        goto error;
        break;

    }
  } else if (gst_poll_fd_can_write (self->poll, &pollfd->pollfd))
  {
    pollfd->want_write = FALSE;
    gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, FALSE);
    switch (pollfd->status)
    {
      case FS_MSN_STATUS_AUTH:
        if (!pollfd->server)
        {
          gchar *str = g_strdup_printf("recipientid=%s&sessionid=%d\r\n\r\n",
              self->remote_recipient_id, self->session_id);
          if (send(pollfd->pollfd.fd, str, strlen (str), 0) != -1)
          {
            g_debug ("Sent %s", str);
            pollfd->status = FS_MSN_STATUS_CONNECTED;
            g_free (str);
          }
          else
          {
            g_free (str);
            perror ("auth");
            goto error;
          }

        }
        break;
      case FS_MSN_STATUS_CONNECTED:
        if (pollfd->server)
        {

          if (send(pollfd->pollfd.fd, "connected\r\n\r\n", 13, 0) != -1)
          {
            g_debug ("sent connected");
            pollfd->status = FS_MSN_STATUS_CONNECTED2;
          }
          else
          {
            perror ("sending connected");
            goto error;
          }
        } else {
          g_debug ("shouldn't receive data when server on CONNECTED state");
          goto error;
        }
        break;
      case FS_MSN_STATUS_CONNECTED2:
        if (!pollfd->server)
        {

          if (send(pollfd->pollfd.fd, "connected\r\n\r\n", 13, 0) != -1)
          {
            g_debug ("sent connected");
            pollfd->status = FS_MSN_STATUS_SEND_RECEIVE;
            success = TRUE;
          }
          else
          {
            perror ("sending connected");
            goto error;
          }
        } else {
          g_debug ("shouldn't receive data when client on CONNECTED2 state");
          goto error;
        }
        break;
      default:
        g_debug ("Invalid status %d", pollfd->status);
        goto error;
        break;
    }
  }

  if (success) {
    // success! we need to shutdown/close all other channels
    gint i;
    for (i = 0; i < self->pollfds->len; i++)
    {
      FsMsnPollFD *pollfd2 = g_array_index(self->pollfds, FsMsnPollFD *, i);
      if (pollfd != pollfd2)
      {
        g_debug ("closing fd %d", pollfd2->pollfd.fd);
        shutdown_fd (self, pollfd2);
        i--;
      }
    }

    /* TODO : callback */

    pollfd->want_read = FALSE;
    pollfd->want_write = FALSE;
    gst_poll_fd_ctl_read (self->poll, &pollfd->pollfd, FALSE);
    gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, FALSE);
  }

  return;
 error:
  /* Error */
  g_debug ("Got error from fd %d, closing", pollfd->pollfd.fd);
  // find, shutdown and remove channel from fdlist
  gint i;
  for (i = 0; i < self->pollfds->len; i++)
  {
    FsMsnPollFD *pollfd2 = g_array_index(self->pollfds, FsMsnPollFD *, i);
    if (pollfd == pollfd2)
    {
      g_debug ("closing fd %d", pollfd2->pollfd.fd);
      shutdown_fd (self, pollfd2);
      i--;
    }
  }

  return;
}

static gpointer
connection_polling_thread (gpointer data)
{
  FsMsnConnection *self = data;
  gint ret;
  GstClockTime timeout;
  GstPoll * poll;

  g_static_rec_mutex_lock (&self->mutex);
  timeout = self->poll_timeout;
  poll = self->poll;
  g_debug ("poll waiting %d", self->pollfds->len);
  g_static_rec_mutex_unlock (&self->mutex);

  while ((ret = gst_poll_wait (poll, timeout)) >= 0)
  {
    g_debug ("gst_poll_wait returned : %d", ret);
    g_static_rec_mutex_lock (&self->mutex);
    if (ret > 0)
    {
      gint i;

      for (i = 0; i < self->pollfds->len; i++)
      {
        FsMsnPollFD *pollfd = NULL;

        pollfd = g_array_index(self->pollfds, FsMsnPollFD *, i);

        g_debug ("ret %d - i = %d, len = %d", ret, i, self->pollfds->len);

        g_debug ("%p - error %d, close %d, read %d-%d, write %d-%d",
            pollfd,
            gst_poll_fd_has_error (poll, &pollfd->pollfd),
            gst_poll_fd_has_closed (poll, &pollfd->pollfd),
            pollfd->want_read,
            gst_poll_fd_can_read (poll, &pollfd->pollfd),
            pollfd->want_write,
            gst_poll_fd_can_write (poll, &pollfd->pollfd));

        if (gst_poll_fd_has_error (poll, &pollfd->pollfd) ||
            gst_poll_fd_has_closed (poll, &pollfd->pollfd))
        {
          pollfd->callback (self, pollfd);
          shutdown_fd (self, pollfd);
          i--;
          continue;
        }
        if ((pollfd->want_read &&
                gst_poll_fd_can_read (poll, &pollfd->pollfd)) ||
            (pollfd->want_write &&
                gst_poll_fd_can_write (poll, &pollfd->pollfd)))
        {
          pollfd->callback (self, pollfd);
        }

      }
    }
    timeout = self->poll_timeout;
    g_static_rec_mutex_unlock (&self->mutex);
  }

  return NULL;
}


static void
shutdown_fd (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  gint i;

  g_debug ("Shutting down pollfd %p", pollfd);

  if (!gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
    close (pollfd->pollfd.fd);
  g_debug ("gst poll remove : %d",
      gst_poll_remove_fd (self->poll, &pollfd->pollfd));
  for (i = 0; i < self->pollfds->len; i++)
  {
    FsMsnPollFD *p = g_array_index(self->pollfds, FsMsnPollFD *, i);
    if (p == pollfd)
    {
      g_array_remove_index_fast (self->pollfds, i);
      break;
    }

  }
  gst_poll_restart (self->poll);
}

static FsMsnPollFD *
add_pollfd (FsMsnConnection *self, int fd, PollFdCallback callback,
    gboolean read, gboolean write)
{
  FsMsnPollFD *pollfd = g_slice_new0 (FsMsnPollFD);
  gst_poll_fd_init (&pollfd->pollfd);
  pollfd->pollfd.fd = fd;
  pollfd->want_read = read;
  pollfd->want_write = write;
  pollfd->status = FS_MSN_STATUS_AUTH;

  gst_poll_add_fd (self->poll, &pollfd->pollfd);

  gst_poll_fd_ctl_read (self->poll, &pollfd->pollfd, read);
  gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, write);
  pollfd->callback = callback;

        g_debug ("ADD_POLLFD %p (%p) - error %d, close %d, read %d-%d, write %d-%d",
            self->pollfds, pollfd,
            gst_poll_fd_has_error (self->poll, &pollfd->pollfd),
            gst_poll_fd_has_closed (self->poll, &pollfd->pollfd),
            pollfd->want_read,
            gst_poll_fd_can_read (self->poll, &pollfd->pollfd),
            pollfd->want_write,
            gst_poll_fd_can_write (self->poll, &pollfd->pollfd));

  g_array_append_val (self->pollfds, pollfd);
  gst_poll_restart (self->poll);
  return pollfd;
}
// (gdb) p ((FsMsnPollFD **) ((FsMsnConnection *)data)->pollfds->data)[1]

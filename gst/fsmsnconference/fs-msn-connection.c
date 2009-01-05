/*
 * Farsight2 - Farsight MSN Connection
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
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


typedef struct _FsMsnPollFD FsMsnPollFD;

struct _FsMsnPollFD {
  GstPollFD pollfd;
  gboolean want_read;
  gboolean want_write;
  void (*next_step) (FsMsnConnection *self, FsMsnPollFD *pollfd);
};


G_DEFINE_TYPE(FsMsnConnection, fs_msn_connection, G_TYPE_OBJECT);

static void fs_msn_connection_dispose (GObject *object);
static void fs_msn_connection_finalize (GObject *object);

static void main_fd_closed_cb (FsMsnConnection *self, FsMsnPollFD *fd);

static void successfull_connection_cb (FsMsnConnection *self, FsMsnPollFD *fd);

static void fd_accept_connection_cb (FsMsnConnection *self, FsMsnPollFD *fd);

static gboolean fs_msn_connection_attempt_connection (FsMsnConnection *connection,
    gchar const *ip,
    guint16 port);

static gboolean fs_msn_authenticate_outgoing (FsMsnConnection *connection,
    gint fd);

static void fs_msn_open_listening_port (FsMsnConnection *connection,
    guint16 port);

static gpointer connection_polling_thread (gpointer data);

static void shutdown_fd (FsMsnConnection *self, FsMsnPollFD *pollfd);

static GObjectClass *parent_class = NULL;

static void
fs_msn_connection_class_init (FsMsnConnectionClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = fs_msn_connection_dispose;
  gobject_class->finalize = fs_msn_connection_finalize;
}

static void
fs_msn_connection_init (FsMsnConnection *self)
{
  /* member init */

  self->disposed = FALSE;

  self->poll_timeout = GST_CLOCK_TIME_NONE;
  self->poll = gst_poll_new (TRUE);
  gst_poll_set_controllable (self->poll, TRUE);
  self->pollfds = g_array_new (TRUE, TRUE, sizeof(FsMsnPollFD));
}

static void
fs_msn_connection_dispose (GObject *object)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (object);

  /* If dispose did already run, return. */
  if (self->disposed)
    return;

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

  /* Make sure dispose does not run twice. */
  self->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_msn_connection_finalize (GObject *object)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (object);
  guint i;

  /* TODO : why not in dispose */
  gst_poll_free (self->poll);

  for (i = 0; i < self->pollfds->len; i++)
    close (g_array_index(self->pollfds, FsMsnPollFD, i).pollfd.fd);
  g_array_free (self->pollfds, TRUE);

  parent_class->finalize (object);
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
fs_msn_connection_gather_local_candidates (FsMsnConnection *self)
{

  fs_msn_open_listening_port (self, self->initial_port);

  self->polling_thread = g_thread_create (connection_polling_thread,
      self, TRUE, NULL);

  return self->polling_thread != NULL;
}

/**
 * fs_msn_connection_set_remote_candidate:
 */
gboolean
fs_msn_connection_set_remote_candidates (FsMsnConnection *self,
    GList *candidates, GError **error)
{
  GList *item = NULL;

  for (item = candidates; item; item = g_list_next (item))
  {
    FsCandidate *candidate = item->data;

    if (!candidate->ip || !candidate->port)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The candidate passed does not contain a valid ip or port");
      return FALSE;
    }
    if (!candidate->foundation)
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "The candidate passed does not have a foundation (MSN recipient ID)");
      return FALSE;
    }
    if (self->remote_recipient_id) {
      if (g_strcmp0 (candidate->foundation, self->remote_recipient_id) != 0)
      {
        g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
            "The candidates do not have the same recipient ID");
        return FALSE;
      }
    } else {
      self->remote_recipient_id = g_strdup (candidate->foundation);
    }
    fs_msn_connection_attempt_connection(self, candidate->ip, candidate->port);
  }

  return TRUE;
}


static void
main_fd_closed_cb (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  g_message ("disconnection on video feed");
  /* IXME - How to handle the disconnection of the connection
     Destroy the elements involved?
     Set the state to Null ?
  */
}

static void
successfull_connection_cb (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  gint error;
  socklen_t option_len;

  g_message ("handler called on fd %d", pollfd->pollfd.fd);

  errno = 0;
  if (gst_poll_fd_has_error (self->poll, &pollfd->pollfd) ||
      gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
  {
    g_message ("connecton closed or error");
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
    g_message ("getsockopt gave an error : %d", error);
    goto error;
  }

  /* Remove NON BLOCKING MODE */
  if (fcntl(pollfd->pollfd.fd, F_SETFL,
          fcntl(pollfd->pollfd.fd, F_GETFL) & ~O_NONBLOCK) != 0)
  {
    g_warning ("fcntl() failed");
    goto error;
  }

  g_message ("Got connection on fd %d", pollfd->pollfd.fd);

  if (fs_msn_authenticate_outgoing (self, pollfd->pollfd.fd))
  {
    g_message ("Authenticated outgoing successfully fd %d", pollfd->pollfd.fd);

    // success! we need to shutdown/close all other channels
    gint i;
    for (i = 0; i < self->pollfds->len; i++)
    {
      FsMsnPollFD *pollfd2 = &g_array_index(self->pollfds, FsMsnPollFD, i);
      if (pollfd != pollfd2)
      {
        g_message ("closing fd %d", pollfd2->pollfd.fd);
        shutdown_fd (self, pollfd2);
        i--;
      }
    }

    /* TODO : callback */

    pollfd->want_read = FALSE;
    pollfd->want_write = FALSE;
    gst_poll_fd_ctl_read (self->poll, &pollfd->pollfd, FALSE);
    gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, FALSE);
    pollfd->next_step = main_fd_closed_cb;
    return;
  }
  else
  {
    g_message ("Authentification failed on fd %d", pollfd->pollfd.fd);
  }

  /* Error */
 error:
  g_message ("Got error from fd %d, closing", pollfd->pollfd.fd);
  // find, shutdown and remove channel from fdlist
  gint i;
  for (i = 0; i < self->pollfds->len; i++)
  {
    FsMsnPollFD *pollfd2 = &g_array_index(self->pollfds, FsMsnPollFD, i);
    if (pollfd == pollfd2)
    {
      g_message ("closing fd %d", pollfd2->pollfd.fd);
      shutdown_fd (self, pollfd2);
      i--;
    }
  }

  return;
}

static gboolean
fs_msn_connection_attempt_connection (FsMsnConnection *connection,
    const gchar *ip,
    guint16 port)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (connection);
  FsMsnPollFD *pollfd = NULL;
  gint fd = -1;
  struct sockaddr_in theiraddr;
  memset(&theiraddr, 0, sizeof(theiraddr));

  if ( (fd = socket(PF_INET, SOCK_STREAM, 0)) == -1 )
  {
    // show error
    g_message ("could not create socket!");
    return FALSE;
  }

  // set non-blocking mode
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

  theiraddr.sin_family = AF_INET;
  theiraddr.sin_addr.s_addr = inet_addr (ip);
  theiraddr.sin_port = htons (port);

  g_message ("Attempting connection to %s %d on socket %d", ip, port, fd);
  // this is non blocking, the return value isn't too usefull
  gint ret = connect (fd, (struct sockaddr *) &theiraddr, sizeof (theiraddr));
  if (ret < 0)
  {
    if (errno != EINPROGRESS)
    {
      close (fd);
      return FALSE;
    }
  }
  g_message("ret %d %d %s", ret, errno, strerror(errno));

  pollfd = g_slice_new0 (FsMsnPollFD);
  gst_poll_fd_init (&pollfd->pollfd);
  pollfd->pollfd.fd = fd;
  pollfd->want_read = TRUE;
  pollfd->want_write = TRUE;
  gst_poll_fd_ctl_read (self->poll, &pollfd->pollfd, TRUE);
  gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, TRUE);
  pollfd->next_step = successfull_connection_cb;
  g_array_append_val (self->pollfds, pollfd);

  return TRUE;
}

static gboolean
fs_msn_authenticate_incoming (FsMsnConnection *connection, gint fd)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (connection);
  if (fd != 0)
    {
      gchar str[400];
      gchar check[400];

      memset(str, 0, sizeof(str));
      if (recv(fd, str, sizeof(str), 0) != -1)
        {
          g_message ("Got %s, checking if it's auth", str);
          sprintf(str, "recipientid=%s&sessionid=%d\r\n\r\n",
                  self->local_recipient_id, self->session_id);
          if (strcmp (str, check) != 0)
            {
              // send our connected message also
              memset(str, 0, sizeof(str));
              sprintf(str, "connected\r\n\r\n");
              send(fd, str, strlen(str), 0);

              // now we get connected
              memset(str, 0, sizeof(str));
              if (recv(fd, str, sizeof(str), 0) != -1)
                {
                  if (strcmp (str, "connected\r\n\r\n") == 0)
                    {
                      g_message ("Authentication successfull");
                      return TRUE;
                    }
                }
            }
        }
      else
        {
          perror("auth");
        }
    }
  return FALSE;
}

static void
fd_accept_connection_cb (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  struct sockaddr_in in;
  int fd = -1;
  socklen_t n = sizeof (in);
  FsMsnPollFD *newpollfd = NULL;

  if (gst_poll_fd_has_error (self->poll, &pollfd->pollfd) ||
      gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
  {
    g_message ("Error in accept socket : %d", pollfd->pollfd.fd);
    goto error;
  }

  if ((fd = accept(pollfd->pollfd.fd,
              (struct sockaddr*) &in, &n)) == -1)
  {
    g_message ("Error while running accept() %d", errno);
    return;
  }

  /* Remove NON BLOCKING MODE */
  if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK) != 0)
  {
    g_warning ("fcntl() failed");
    goto error;
  }

  // now we try to auth
  if (fs_msn_authenticate_incoming(self,fd))
  {
    g_message ("Authenticated incoming successfully fd %d", fd);

    // success! we need to shutdown/close all other channels
    gint i;
    for (i = 0; i < self->pollfds->len; i++)
    {
      FsMsnPollFD *pollfd2 = &g_array_index(self->pollfds, FsMsnPollFD, i);
      g_message ("closing fd %d", pollfd2->pollfd.fd);
      shutdown_fd (self, pollfd2);
      i--;
    }
    /* TODO callback */

    newpollfd = g_slice_new0 (FsMsnPollFD);
    gst_poll_fd_init (&newpollfd->pollfd);
    newpollfd->pollfd.fd = fd;
    newpollfd->want_read = FALSE;
    newpollfd->want_write = FALSE;
    gst_poll_fd_ctl_read (self->poll, &newpollfd->pollfd, FALSE);
    gst_poll_fd_ctl_write (self->poll, &newpollfd->pollfd, FALSE);
    newpollfd->next_step = main_fd_closed_cb;
    g_array_append_val (self->pollfds, newpollfd);
    return;
  }

  /* Error */
 error:
  g_message ("Got error from fd %d, closing", fd);
  // find, shutdown and remove channel from fdlist
  gint i;
  for (i = 0; i < self->pollfds->len; i++)
  {
    FsMsnPollFD *pollfd2 = &g_array_index(self->pollfds, FsMsnPollFD, i);
    if (pollfd == pollfd2)
    {
      g_message ("closing fd %d", pollfd2->pollfd.fd);
      shutdown_fd (self, pollfd2);
      i--;
    }
  }

  if (fd > 0)
    close (fd);

  return;
}

static void
fs_msn_open_listening_port (FsMsnConnection *connection,
                            guint16 port)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (connection);
  gint fd = -1;
  FsMsnPollFD *pollfd = NULL;
  struct sockaddr_in myaddr;
  memset(&myaddr, 0, sizeof(myaddr));

  g_message ("Attempting to listen on port %d.....\n",port);

  if ( (fd = socket(PF_INET, SOCK_STREAM, 0)) == -1 )
  {
    // show error
    g_message ("could not create socket!");
    return;
  }

  // set non-blocking mode
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  myaddr.sin_family = AF_INET;
  myaddr.sin_port = htons (port);
  // bind
  if (bind(fd, (struct sockaddr *) &myaddr, sizeof(myaddr)) != 0)
  {
    close (fd);
    return;
  }


  /* Listen */
  if (listen(fd, 3) != 0)
  {
    close (fd);
    return;
  }
  pollfd = g_slice_new0 (FsMsnPollFD);
  gst_poll_fd_init (&pollfd->pollfd);
  pollfd->pollfd.fd = fd;
  pollfd->want_read = TRUE;
  pollfd->want_write = FALSE;
  gst_poll_fd_ctl_read (self->poll, &pollfd->pollfd, TRUE);
  gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, FALSE);
  pollfd->next_step = fd_accept_connection_cb;

  g_array_append_val (self->pollfds, pollfd);
  g_message ("Listening on port %d\n",port);

}

// Authenticate ourselves when connecting out
static gboolean
fs_msn_authenticate_outgoing (FsMsnConnection *connection, gint fd)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (connection);
  gchar str[400];
  memset(str, 0, sizeof(str));
  if (fd != 0)
  {
    g_message ("Authenticating connection on %d...", fd);
    g_message ("sending : recipientid=%s&sessionid=%d\r\n\r\n",
        self->remote_recipient_id, self->session_id);
    sprintf(str, "recipientid=%s&sessionid=%d\r\n\r\n",
        self->remote_recipient_id, self->session_id);
    if (send(fd, str, strlen(str), 0) == -1)
    {
      g_message("sending failed");
      perror("auth");
    }

    memset(str, 0, sizeof(str));
    if (recv(fd, str, 13, 0) != -1)
    {
      g_message ("Got %s, checking if it's auth", str);
      // we should get a connected message now
      if (strcmp (str, "connected\r\n\r\n") == 0)
      {
        // send our connected message also
        memset(str, 0, sizeof(str));
        sprintf(str, "connected\r\n\r\n");
        send(fd, str, strlen(str), 0);
        g_message ("Authentication successfull");
        return TRUE;
      }
    }
    else
    {
      perror("auth");
    }
  }
  return FALSE;
}


static gpointer
connection_polling_thread (gpointer data)
{
  FsMsnConnection *self = data;
  gint ret;
  GstClockTime timeout;

  timeout = self->poll_timeout;

  while ((ret = gst_poll_wait (self->poll, timeout)) >= 0)
  {
    if (ret > 0)
    {
      gint i;

      for (i = 0; i < self->pollfds->len; i++)
      {
        FsMsnPollFD *pollfd = &g_array_index(self->pollfds,
            FsMsnPollFD, i);

        if (gst_poll_fd_has_error (self->poll, &pollfd->pollfd) ||
            gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
        {
          pollfd->next_step (self, pollfd);
          shutdown_fd (self, pollfd);
          i--;
          continue;
        }
        if ((pollfd->want_read &&
                gst_poll_fd_can_read (self->poll, &pollfd->pollfd)) ||
            (pollfd->want_write &&
                gst_poll_fd_can_write (self->poll, &pollfd->pollfd)))
          pollfd->next_step (self, pollfd);
      }

    }
    timeout = self->poll_timeout;
  }

  return NULL;
}

static void
shutdown_fd (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  gint i;


  if (!gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
    close (pollfd->pollfd.fd);
  gst_poll_remove_fd (self->poll, &pollfd->pollfd);
  for (i = 0; i < self->pollfds->len; i++)
  {
    FsMsnPollFD *p = &g_array_index(self->pollfds, FsMsnPollFD, i);
    if (p == pollfd)
    {
      g_array_remove_index_fast (self->pollfds, i);
      break;
    }

  }
  gst_poll_restart (self->poll);
}

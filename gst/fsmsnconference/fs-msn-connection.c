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

#include <gst/farsight/fs-interfaces.h>

#define GST_CAT_DEFAULT fsmsnconference_debug

/* Signals */
enum
{
  SIGNAL_NEW_LOCAL_CANDIDATE,
  SIGNAL_LOCAL_CANDIDATES_PREPARED,
  SIGNAL_CONNECTED,
  SIGNAL_CONNECTION_FAILED,
  N_SIGNALS
};


static guint signals[N_SIGNALS];

/* props */
enum
{
  PROP_0,
  PROP_SESSION_ID
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

#define FS_MSN_CONNECTION_LOCK(conn)   g_static_rec_mutex_lock(&(conn)->mutex)
#define FS_MSN_CONNECTION_UNLOCK(conn) g_static_rec_mutex_unlock(&(conn)->mutex)


G_DEFINE_TYPE(FsMsnConnection, fs_msn_connection, G_TYPE_OBJECT);

static void fs_msn_connection_dispose (GObject *object);
static void fs_msn_connection_finalize (GObject *object);
static void fs_msn_connection_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);
static void fs_msn_connection_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);


static gboolean fs_msn_connection_attempt_connection_locked (
    FsMsnConnection *connection,
    FsCandidate *candidate,
    GError **error);
static gboolean fs_msn_open_listening_port_unlock (FsMsnConnection *connection,
    guint16 port,
    GError **error);

static void successful_connection_cb (FsMsnConnection *self, FsMsnPollFD *fd);
static void accept_connection_cb (FsMsnConnection *self, FsMsnPollFD *fd);
static void connection_cb (FsMsnConnection *self, FsMsnPollFD *fd);

static gpointer connection_polling_thread (gpointer data);
static void shutdown_fd (FsMsnConnection *self, FsMsnPollFD *pollfd,
    gboolean equal);
static void shutdown_fd_locked (FsMsnConnection *self, FsMsnPollFD *pollfd,
    gboolean equal);
static FsMsnPollFD * add_pollfd_locked (FsMsnConnection *self, int fd,
    PollFdCallback callback, gboolean read, gboolean write, gboolean server);

static void
fs_msn_connection_class_init (FsMsnConnectionClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = fs_msn_connection_dispose;
  gobject_class->finalize = fs_msn_connection_finalize;
  gobject_class->get_property = fs_msn_connection_get_property;
  gobject_class->set_property = fs_msn_connection_set_property;

  signals[SIGNAL_NEW_LOCAL_CANDIDATE] = g_signal_new
      ("new-local-candidate",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL,
          NULL,
          g_cclosure_marshal_VOID__BOXED,
          G_TYPE_NONE, 1, FS_TYPE_CANDIDATE);

  signals[SIGNAL_LOCAL_CANDIDATES_PREPARED] = g_signal_new
    ("local-candidates-prepared",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  signals[SIGNAL_CONNECTED] = g_signal_new
    ("connected",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__UINT,
        G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[SIGNAL_CONNECTION_FAILED] = g_signal_new
    ("connection-failed",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  g_object_class_install_property (gobject_class,
      PROP_SESSION_ID,
      g_param_spec_uint ("session-id",
          "The session-id of the session",
          "This is the session-id of the MSN session",
          1, 9999, 9000,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
fs_msn_connection_init (FsMsnConnection *self)
{
  /* member init */

  self->poll_timeout = GST_CLOCK_TIME_NONE;
  self->poll = gst_poll_new (TRUE);
  gst_poll_set_flushing (self->poll, FALSE);
  self->pollfds = g_ptr_array_new ();

  g_static_rec_mutex_init (&self->mutex);
}

static void
fs_msn_connection_dispose (GObject *object)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (object);

  FS_MSN_CONNECTION_LOCK(self);

  if (self->polling_thread)
  {
    gst_poll_set_flushing (self->poll, TRUE);
    g_thread_join (self->polling_thread);
    self->polling_thread = NULL;
  }

  FS_MSN_CONNECTION_UNLOCK(self);

  G_OBJECT_CLASS (fs_msn_connection_parent_class)->dispose (object);
}

static void
fs_msn_connection_finalize (GObject *object)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (object);
  gint i;

  g_free (self->local_recipient_id);
  g_free (self->remote_recipient_id);

  gst_poll_free (self->poll);

  for (i = 0; i < self->pollfds->len; i++)
  {
    FsMsnPollFD *p = g_ptr_array_index(self->pollfds, i);
    close (p->pollfd.fd);
    g_slice_free (FsMsnPollFD, p);
  }
  g_ptr_array_free (self->pollfds, TRUE);

  g_static_rec_mutex_free (&self->mutex);

  G_OBJECT_CLASS (fs_msn_connection_parent_class)->finalize (object);
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

static void
fs_msn_connection_get_property (GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (object);

  FS_MSN_CONNECTION_LOCK (self);
  switch (prop_id)
  {
    case PROP_SESSION_ID:
      g_value_set_uint (value, self->session_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  FS_MSN_CONNECTION_UNLOCK (self);
}

static void
fs_msn_connection_set_property (GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (object);

  FS_MSN_CONNECTION_LOCK (self);
  switch (prop_id)
  {
    case PROP_SESSION_ID:
      self->session_id = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  FS_MSN_CONNECTION_LOCK (self);

}

gboolean
fs_msn_connection_gather_local_candidates (FsMsnConnection *self,
    GError **error)
{
  gboolean ret;

  FS_MSN_CONNECTION_LOCK(self);

  self->polling_thread = g_thread_create (connection_polling_thread,
      self, TRUE, NULL);

  if (!self->polling_thread)
  {
    FS_MSN_CONNECTION_UNLOCK(self);
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL, "Could not start thread");
    return FALSE;
  }

  ret = fs_msn_open_listening_port_unlock (self, self->initial_port, error);

  g_signal_emit (self, signals[SIGNAL_LOCAL_CANDIDATES_PREPARED], 0);

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
  guint session_id = 0;

  if (!candidates)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Candidate list can no be empty");
    return FALSE;
  }

  FS_MSN_CONNECTION_LOCK(self);

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

    if (candidate->username)
    {
      gint sid = atoi (candidate->username);

      if (sid < 1 || session_id > 9999)
      {
          g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
              "The session ID (in the username) must be between 9000 and 9999,"
              " %d is invalid", sid);
          goto out;
      }

      if (session_id)
      {
        if (session_id != sid)
        {
          g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
              "The candidates do not have the same session ID"
              " (in the username)");
          goto out;
        }
      }
      else
      {
        session_id = sid;
      }
    }
  }

  self->remote_recipient_id = g_strdup (recipient_id);
  self->session_id = session_id;
  ret = TRUE;
  for (item = candidates; item; item = g_list_next (item))
  {
    FsCandidate *candidate = item->data;
    if (!fs_msn_connection_attempt_connection_locked (self, candidate, error))
    {
      ret = FALSE;
      break;
    }
  }

 out:
  FS_MSN_CONNECTION_UNLOCK(self);
  return ret;
}


static gboolean
fs_msn_open_listening_port_unlock (FsMsnConnection *self, guint16 port,
    GError **error)
{
  gint fd = -1;
  struct sockaddr_in myaddr;
  guint myaddr_len = sizeof (struct sockaddr_in);
  FsCandidate * candidate = NULL;
  GList *addresses = fs_interfaces_get_local_ips (FALSE);
  GList *item = NULL;
  gchar *session_id;


  GST_DEBUG ("Attempting to listen on port %d.....",port);

  if ( (fd = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
  {
    gchar error_str[256];
    strerror_r (errno, error_str, 256);
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Could not create socket: %s", error_str);
    goto error;
  }

  // set non-blocking mode
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  for (;;) {
    GST_DEBUG ("Attempting to listen on port %d.....",port);
    memset(&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
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
        gchar error_str[256];
        strerror_r (errno, error_str, 256);
        g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
            "Could not bind socket: %s", error_str);
        goto error;
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
          gchar error_str[256];
          strerror_r (errno, error_str, 256);
          g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
              "Could not listen on socket: %s", error_str);
          goto error;
        }
      }
      else
      {
        goto done;
      }
    }
  }

 done:

  if (getsockname (fd, (struct sockaddr *) &myaddr, &myaddr_len) < 0) {
    gchar error_str[256];
    strerror_r (errno, error_str, 256);
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Could not get the socket name: %s", error_str);
    goto error;
  }
  port = ntohs (myaddr.sin_port);
  add_pollfd_locked (self, fd, accept_connection_cb, TRUE, TRUE, FALSE);

  GST_DEBUG ("Listening on port %d", port);

  self->local_recipient_id = g_strdup_printf ("%d",
      g_random_int_range (100, 199));
  session_id = g_strdup_printf ("%u", self->session_id);

  FS_MSN_CONNECTION_UNLOCK (self);

  for (item = addresses;
       item;
       item = g_list_next (item))
  {
    candidate = fs_candidate_new (self->local_recipient_id, 1,
        FS_CANDIDATE_TYPE_HOST, FS_NETWORK_PROTOCOL_TCP, item->data, port);
    candidate->username = g_strdup (session_id);

    g_signal_emit (self, signals[SIGNAL_NEW_LOCAL_CANDIDATE], 0, candidate);

    fs_candidate_destroy (candidate);
  }

  g_free (session_id);

  g_list_foreach (addresses, (GFunc) g_free, NULL);
  g_list_free (addresses);

  return TRUE;

 error:
  if (fd >= 0)
    close (fd);
  g_list_foreach (addresses, (GFunc) g_free, NULL);
  g_list_free (addresses);
  FS_MSN_CONNECTION_UNLOCK (self);
  return FALSE;
}

static gboolean
fs_msn_connection_attempt_connection_locked (FsMsnConnection *connection,
    FsCandidate *candidate,
    GError **error)
{
  FsMsnConnection *self = FS_MSN_CONNECTION (connection);
  FsMsnPollFD *pollfd;
  gint fd = -1;
  gint ret;
  struct sockaddr_in theiraddr;
  memset(&theiraddr, 0, sizeof(theiraddr));


  if ( (fd = socket(PF_INET, SOCK_STREAM, 0)) == -1 )
  {
    gchar error_str[256];
    strerror_r (errno, error_str, 256);
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Could not create socket: %s", error_str);
    return FALSE;
  }

  // set non-blocking mode
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

  theiraddr.sin_family = AF_INET;
  theiraddr.sin_addr.s_addr = inet_addr (candidate->ip);
  theiraddr.sin_port = htons (candidate->port);

  GST_DEBUG ("Attempting connection to %s %d on socket %d", candidate->ip,
      candidate->port, fd);
  // this is non blocking, the return value isn't too usefull
  ret = connect (fd, (struct sockaddr *) &theiraddr, sizeof (theiraddr));
  if (ret < 0 && errno != EINPROGRESS)
  {
    gchar error_str[256];
    strerror_r (errno, error_str, 256);
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Could not connect socket: %s", error_str);
    close (fd);
    return FALSE;
  }

  FS_MSN_CONNECTION_LOCK (self);
  pollfd = add_pollfd_locked (self, fd, successful_connection_cb, TRUE, TRUE,
      FALSE);
  FS_MSN_CONNECTION_UNLOCK (self);

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
    GST_WARNING ("Error in accept socket : %d", pollfd->pollfd.fd);
    goto error;
  }

  if ((fd = accept(pollfd->pollfd.fd,
              (struct sockaddr*) &in, &n)) == -1)
  {
    GST_ERROR ("Error while running accept() %d", errno);
    return;
  }

  FS_MSN_CONNECTION_LOCK (self);
  newpollfd = add_pollfd_locked (self, fd, connection_cb, TRUE, FALSE, TRUE);
  FS_MSN_CONNECTION_UNLOCK (self);

  return;

  /* Error */
 error:
  GST_WARNING ("Got error from fd %d, closing", fd);
  // find, shutdown and remove channel from fdlist
  shutdown_fd (self, pollfd, TRUE);

  return;
}


static void
successful_connection_cb (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  gint error;
  socklen_t option_len;

  GST_DEBUG ("handler called on fd %d", pollfd->pollfd.fd);

  errno = 0;
  if (gst_poll_fd_has_error (self->poll, &pollfd->pollfd) ||
      gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
  {
    GST_WARNING ("connecton closed or error");
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
    GST_WARNING ("getsockopt gave an error : %d", error);
    goto error;
  }

  pollfd->callback = connection_cb;

  GST_DEBUG ("connection succeeded on socket %p", pollfd);
  return;

  /* Error */
 error:
  GST_WARNING ("Got error from fd %d, closing", pollfd->pollfd.fd);
  // find, shutdown and remove channel from fdlist
  shutdown_fd (self, pollfd, TRUE);

  return;
}


static void
connection_cb (FsMsnConnection *self, FsMsnPollFD *pollfd)
{
  gboolean success = FALSE;

  GST_DEBUG ("handler called on fd %d. %d %d %d %d", pollfd->pollfd.fd,
      pollfd->server, pollfd->status,
      gst_poll_fd_can_read (self->poll, &pollfd->pollfd),
      gst_poll_fd_can_write (self->poll, &pollfd->pollfd));

  if (gst_poll_fd_has_error (self->poll, &pollfd->pollfd) ||
      gst_poll_fd_has_closed (self->poll, &pollfd->pollfd))
  {
    GST_WARNING ("connecton closed or error");
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
            GST_DEBUG ("Got %s, checking if it's auth", str);
            FS_MSN_CONNECTION_LOCK(self);
            snprintf(check, 35, "recipientid=%s&sessionid=%d\r\n\r\n",
                self->local_recipient_id, self->session_id);
            FS_MSN_CONNECTION_UNLOCK(self);
            if (strncmp (str, check, 35) == 0)
            {
              GST_DEBUG ("Authentication successful");
              pollfd->status = FS_MSN_STATUS_CONNECTED;
              pollfd->want_write = TRUE;
              gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, TRUE);
            }
            else
            {
              GST_WARNING ("Authentication failed");
              goto error;
            }
          }
          else
          {
            gchar error_str[256];
            strerror_r (errno, error_str, 256);
            GST_WARNING ("auth: %s", error_str);
            goto error;
          }

        } else {
          GST_ERROR ("shouldn't receive data when client on AUTH state");
          goto error;
        }
        break;
      case FS_MSN_STATUS_CONNECTED:
        if (!pollfd->server)
        {
          gchar str[14] = {0};

          if (recv(pollfd->pollfd.fd, str, 13, 0) != -1)
          {
            GST_DEBUG ("Got %s, checking if it's connected", str);
            if (strcmp (str, "connected\r\n\r\n") == 0)
            {
              GST_DEBUG ("connection successful");
              pollfd->status = FS_MSN_STATUS_CONNECTED2;
              pollfd->want_write = TRUE;
              gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, TRUE);
            }
            else
            {
              GST_WARNING ("connected failed");
              goto error;
            }
          }
          else
          {
            gchar error_str[256];
            strerror_r (errno, error_str, 256);
            GST_WARNING ("recv: %s", error_str);
            goto error;
          }
        } else {
          GST_ERROR ("shouldn't receive data when server on CONNECTED state");
          goto error;
        }
        break;
      case FS_MSN_STATUS_CONNECTED2:
        if (pollfd->server)
        {
          gchar str[14] = {0};

          if (recv(pollfd->pollfd.fd, str, 13, 0) != -1)
          {
            GST_DEBUG ("Got %s, checking if it's connected", str);
            if (strcmp (str, "connected\r\n\r\n") == 0)
            {
              GST_DEBUG ("connection successful");
              pollfd->status = FS_MSN_STATUS_SEND_RECEIVE;
              success = TRUE;
            }
            else
            {
              GST_WARNING ("connected failed");
              goto error;
            }
          }
          else
          {
            gchar error_str[256];
            strerror_r (errno, error_str, 256);
            GST_WARNING ("recv: %s", error_str);
            goto error;
          }

        } else {
          GST_ERROR ("shouldn't receive data when client on CONNECTED2 state");
          goto error;
        }
        break;
      default:
        GST_ERROR ("Invalid status %d", pollfd->status);
        goto error;
        break;

    }
  }
  else if (gst_poll_fd_can_write (self->poll, &pollfd->pollfd))
  {
    pollfd->want_write = FALSE;
    gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, FALSE);
    switch (pollfd->status)
    {
      case FS_MSN_STATUS_AUTH:
        if (!pollfd->server)
        {
          gchar *str;
          FS_MSN_CONNECTION_LOCK(self);
          str = g_strdup_printf("recipientid=%s&sessionid=%d\r\n\r\n",
              self->remote_recipient_id, self->session_id);
          FS_MSN_CONNECTION_UNLOCK(self);
          if (send(pollfd->pollfd.fd, str, strlen (str), 0) != -1)
          {
            GST_DEBUG ("Sent %s", str);
            pollfd->status = FS_MSN_STATUS_CONNECTED;
            g_free (str);
          }
          else
          {
            gchar error_str[256];
            strerror_r (errno, error_str, 256);
            GST_WARNING ("auth send: %s", error_str);
            g_free (str);
            goto error;
          }

        }
        break;
      case FS_MSN_STATUS_CONNECTED:
        if (pollfd->server)
        {

          if (send(pollfd->pollfd.fd, "connected\r\n\r\n", 13, 0) != -1)
          {
            GST_DEBUG ("sent connected");
            pollfd->status = FS_MSN_STATUS_CONNECTED2;
          }
          else
          {
            gchar error_str[256];
            strerror_r (errno, error_str, 256);
            GST_WARNING ("sending connected: %s", error_str);
            goto error;
          }
        } else {
          GST_DEBUG ("shouldn't receive data when server on CONNECTED state");
          goto error;
        }
        break;
      case FS_MSN_STATUS_CONNECTED2:
        if (!pollfd->server)
        {

          if (send(pollfd->pollfd.fd, "connected\r\n\r\n", 13, 0) != -1)
          {
            GST_DEBUG ("sent connected");
            pollfd->status = FS_MSN_STATUS_SEND_RECEIVE;
            success = TRUE;
          }
          else
          {
            gchar error_str[256];
            strerror_r (errno, error_str, 256);
            GST_WARNING ("sending connected: %s", error_str);
            goto error;
          }
        } else {
          GST_ERROR ("shouldn't receive data when client on CONNECTED2 state");
          goto error;
        }
        break;
      default:
        GST_ERROR ("Invalid status %d", pollfd->status);
        goto error;
        break;
    }
  }

  if (success) {
    // success! we need to shutdown/close all other channels
    shutdown_fd (self, pollfd, FALSE);

    g_signal_emit (self, signals[SIGNAL_CONNECTED], 0, pollfd->pollfd.fd);

    pollfd->want_read = FALSE;
    pollfd->want_write = FALSE;
    gst_poll_fd_ctl_read (self->poll, &pollfd->pollfd, FALSE);
    gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, FALSE);
  }

  return;
 error:
  /* Error */
  GST_WARNING ("Got error from fd %d, closing", pollfd->pollfd.fd);
  shutdown_fd (self, pollfd, TRUE);

  FS_MSN_CONNECTION_LOCK (self);
  success = (self->pollfds->len > 1);
  FS_MSN_CONNECTION_LOCK (self);

  if (!success)
    g_signal_emit (self, signals[SIGNAL_CONNECTION_FAILED], 0);

  return;
}

static gpointer
connection_polling_thread (gpointer data)
{
  FsMsnConnection *self = data;
  gint ret;
  GstClockTime timeout;
  GstPoll * poll;

  FS_MSN_CONNECTION_LOCK(self);
  timeout = self->poll_timeout;
  poll = self->poll;
  GST_DEBUG ("poll waiting %d", self->pollfds->len);
  FS_MSN_CONNECTION_UNLOCK(self);

  while ((ret = gst_poll_wait (poll, timeout)) >= 0)
  {
    GST_DEBUG ("gst_poll_wait returned : %d", ret);
    FS_MSN_CONNECTION_LOCK(self);
    if (ret > 0)
    {
      gint i;

      for (i = 0; i < self->pollfds->len; i++)
      {
        FsMsnPollFD *pollfd = NULL;

        pollfd = g_ptr_array_index(self->pollfds, i);

        GST_DEBUG ("ret %d - i = %d, len = %d", ret, i, self->pollfds->len);

        GST_DEBUG ("%p - error %d, close %d, read %d-%d, write %d-%d",
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
          shutdown_fd_locked (self, pollfd, TRUE);
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
    FS_MSN_CONNECTION_UNLOCK(self);
  }

  return NULL;
}


static void
shutdown_fd (FsMsnConnection *self, FsMsnPollFD *pollfd, gboolean equal)
{
  FS_MSN_CONNECTION_LOCK (self);
  shutdown_fd_locked (self, pollfd, equal);
  FS_MSN_CONNECTION_UNLOCK (self);
}

static void
shutdown_fd_locked (FsMsnConnection *self, FsMsnPollFD *pollfd, gboolean equal)
{
  gint i;
  guint closed = 0;

  for (i = 0; i < self->pollfds->len; i++)
  {
    FsMsnPollFD *p = g_ptr_array_index(self->pollfds, i);
    if ((equal && p == pollfd) || (!equal && p != pollfd))
    {
      GST_DEBUG ("Shutting down p %p (fd %d)", p, p->pollfd.fd);

      if (!gst_poll_fd_has_closed (self->poll, &p->pollfd))
        close (p->pollfd.fd);
      if (!gst_poll_remove_fd (self->poll, &p->pollfd))
        GST_WARNING ("Could not remove pollfd %p", p);
      g_ptr_array_remove_index_fast (self->pollfds, i);
      g_slice_free (FsMsnPollFD, p);
      closed++;
      i--;
    }
  }

  if (closed)
    gst_poll_restart (self->poll);
  else
    GST_WARNING ("Could find pollfd to remove");
}

static FsMsnPollFD *
add_pollfd_locked (FsMsnConnection *self, int fd, PollFdCallback callback,
    gboolean read, gboolean write, gboolean server)
{
  FsMsnPollFD *pollfd = g_slice_new0 (FsMsnPollFD);
  gst_poll_fd_init (&pollfd->pollfd);
  pollfd->pollfd.fd = fd;
  pollfd->server = server;
  pollfd->want_read = read;
  pollfd->want_write = write;
  pollfd->status = FS_MSN_STATUS_AUTH;

  gst_poll_add_fd (self->poll, &pollfd->pollfd);

  gst_poll_fd_ctl_read (self->poll, &pollfd->pollfd, read);
  gst_poll_fd_ctl_write (self->poll, &pollfd->pollfd, write);
  pollfd->callback = callback;

  GST_DEBUG ("ADD_POLLFD %p (%p) - error %d, close %d, read %d-%d, write %d-%d",
      self->pollfds, pollfd,
      gst_poll_fd_has_error (self->poll, &pollfd->pollfd),
      gst_poll_fd_has_closed (self->poll, &pollfd->pollfd),
      pollfd->want_read,
      gst_poll_fd_can_read (self->poll, &pollfd->pollfd),
      pollfd->want_write,
      gst_poll_fd_can_write (self->poll, &pollfd->pollfd));

  g_ptr_array_add (self->pollfds, pollfd);
  gst_poll_restart (self->poll);
  return pollfd;
}

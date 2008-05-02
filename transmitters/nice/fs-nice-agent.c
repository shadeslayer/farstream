/*
 * Farsight2 - Farsight libnice Transmitter agent object
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-nice-agent.c - A Farsight libnice transmitter agent object
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

/**
 * SECTION:fs-nice-agent
 * @short_description: A transmitter for agents for libnice
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/farsight/fs-conference-iface.h>
#include <gst/farsight/fs-interfaces.h>

#include "fs-nice-transmitter.h"
#include "fs-nice-agent.h"

#include <nice/nice.h>

#include <string.h>
#include <sys/types.h>

#define GST_CAT_DEFAULT fs_nice_transmitter_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_COMPATIBILITY_MODE,
  PROP_PREFERRED_LOCAL_CANDIDATES,
};

struct _FsNiceAgentPrivate
{
  GMainContext *main_context;
  GMainLoop *main_loop;

  guint compatibility_mode;

  NiceUDPSocketFactory udpfactory;

  GList *preferred_local_candidates;

  GMutex *mutex;

  /* Everything below is protected by the mutex */

  GThread *thread;
};

#define FS_NICE_AGENT_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_NICE_AGENT, \
    FsNiceAgentPrivate))


#define FS_NICE_AGENT_LOCK(o)   g_mutex_lock ((o)->priv->mutex)
#define FS_NICE_AGENT_UNLOCK(o) g_mutex_unlock ((o)->priv->mutex)

static void fs_nice_agent_class_init (
    FsNiceAgentClass *klass);
static void fs_nice_agent_init (FsNiceAgent *self);
static void fs_nice_agent_dispose (GObject *object);
static void fs_nice_agent_finalize (GObject *object);
static void fs_nice_agent_stop_thread (FsNiceAgent *self);

static void fs_nice_agent_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec);
static void fs_nice_agent_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec);


static GObjectClass *parent_class = NULL;


/*
 * Lets register the plugin
 */

static GType type = 0;

GType
fs_nice_agent_get_type (void)
{
  g_assert (type);
  return type;
}

GType
fs_nice_agent_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsNiceAgentClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_nice_agent_class_init,
    NULL,
    NULL,
    sizeof (FsNiceAgent),
    0,
    (GInstanceInitFunc) fs_nice_agent_init
  };

  type = g_type_module_register_type (G_TYPE_MODULE (module),
      G_TYPE_OBJECT, "FsNiceAgent", &info, 0);

  return type;
}

static void
fs_nice_agent_class_init (FsNiceAgentClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_nice_agent_set_property;
  gobject_class->get_property = fs_nice_agent_get_property;
  gobject_class->dispose = fs_nice_agent_dispose;
  gobject_class->finalize = fs_nice_agent_finalize;

  g_type_class_add_private (klass, sizeof (FsNiceAgentPrivate));

  g_object_class_install_property (gobject_class, PROP_COMPATIBILITY_MODE,
      g_param_spec_uint (
          "compatibility-mode",
          "The compability-mode",
          "The id of the stream according to libnice",
          NICE_COMPATIBILITY_ID19, NICE_COMPATIBILITY_LAST,
          NICE_COMPATIBILITY_ID19,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_PREFERRED_LOCAL_CANDIDATES,
      g_param_spec_boxed ("preferred-local-candidates",
        "The preferred candidates",
        "A GList of FsCandidates",
        FS_TYPE_CANDIDATE_LIST,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static void
fs_nice_agent_init (FsNiceAgent *self)
{

  /* member init */
  self->priv = FS_NICE_AGENT_GET_PRIVATE (self);

  nice_udp_bsd_socket_factory_init (&self->priv->udpfactory);

  self->priv->mutex = g_mutex_new ();

  self->priv->main_context = g_main_context_new ();
  self->priv->main_loop = g_main_loop_new (self->priv->main_context, FALSE);

  self->priv->compatibility_mode = NICE_COMPATIBILITY_ID19;
}


static void
fs_nice_agent_dispose (GObject *object)
{
  FsNiceAgent *self = FS_NICE_AGENT (object);

  fs_nice_agent_stop_thread (self);

  if (self->agent)
    g_object_unref (self->agent);
  self->agent = NULL;

  parent_class->dispose (object);
}
static void
fs_nice_agent_finalize (GObject *object)
{
  FsNiceAgent *self = FS_NICE_AGENT (object);

  if (self->priv->main_context)
    g_main_context_unref (self->priv->main_context);
  self->priv->main_context = NULL;

  if (self->priv->main_loop)
    g_main_loop_unref (self->priv->main_loop);
  self->priv->main_loop = NULL;

  fs_candidate_list_destroy (self->priv->preferred_local_candidates);
  self->priv->preferred_local_candidates = NULL;

  g_mutex_free (self->priv->mutex);
  self->priv->mutex = NULL;

  nice_udp_socket_factory_close (&self->priv->udpfactory);

  parent_class->finalize (object);
}

static void
fs_nice_agent_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  FsNiceAgent *self = FS_NICE_AGENT (object);

  switch (prop_id)
  {
    case PROP_COMPATIBILITY_MODE:
      self->priv->compatibility_mode = g_value_get_uint (value);
      break;
    case PROP_PREFERRED_LOCAL_CANDIDATES:
      self->priv->preferred_local_candidates = g_value_dup_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
fs_nice_agent_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  FsNiceAgent *self = FS_NICE_AGENT (object);

  switch (prop_id)
  {
    case PROP_COMPATIBILITY_MODE:
      g_value_set_uint (value, self->priv->compatibility_mode);
      break;
    case PROP_PREFERRED_LOCAL_CANDIDATES:
      g_value_set_boxed (value, self->priv->preferred_local_candidates);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
thread_unlock_idler (gpointer data)
{
  FsNiceAgent *self = FS_NICE_AGENT (data);

  g_main_loop_quit (self->priv->main_loop);

  return TRUE;
}

static void
fs_nice_agent_stop_thread (FsNiceAgent *self)
{
  GSource *idle_source;

  FS_NICE_AGENT_LOCK(self);

  if (self->priv->thread == NULL)
  {
    FS_NICE_AGENT_UNLOCK (self);
    return;
  }
  FS_NICE_AGENT_UNLOCK (self);

  g_main_loop_quit (self->priv->main_loop);

  idle_source = g_idle_source_new ();
  g_source_set_priority (idle_source, G_PRIORITY_HIGH);
  g_source_set_callback (idle_source, thread_unlock_idler, self, NULL);
  g_source_attach (idle_source, self->priv->main_context);

  g_thread_join (self->priv->thread);

  g_source_destroy (idle_source);
  g_source_unref (idle_source);

  FS_NICE_AGENT_LOCK (self);
  self->priv->thread = NULL;
  FS_NICE_AGENT_UNLOCK (self);
}

GMainContext *
fs_nice_agent_get_context (FsNiceAgent *self)
{
  return self->priv->main_context;
}


void
fs_nice_agent_add_weak_object (FsNiceAgent *self,
    GObject *object)
{
  g_object_weak_ref (G_OBJECT (object), (GWeakNotify) g_object_unref, self);

  g_object_ref (self);
}



static gpointer
fs_nice_agent_main_thread (gpointer data)
{
  FsNiceAgent *self = FS_NICE_AGENT (data);

  g_main_loop_run (self->priv->main_loop);

  return NULL;
}

static gboolean
fs_nice_agent_init_agent (FsNiceAgent *self, GError **error)
{
  GList *item;
  gboolean set = FALSE;

  for (item = self->priv->preferred_local_candidates;
       item;
       item = g_list_next (item))
  {
    FsCandidate *cand = item->data;
    NiceAddress *addr = nice_address_new ();

    if (nice_address_set_from_string (addr, cand->ip))
    {
      if (!nice_agent_add_local_address (self->agent, addr))
      {
        g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
            "Unable to set preferred local candidate");
        return FALSE;
      }
      set = TRUE;
    }
    else
    {
      g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
          "Invalid local address passed");
      nice_address_free (addr);
      return FALSE;
    }
    nice_address_free (addr);
  }

  if (!set)
  {      GList *addresses = fs_interfaces_get_local_ips (FALSE);

    for (item = addresses;
         item;
         item = g_list_next (item))
    {
      NiceAddress *addr = nice_address_new ();;

      if (nice_address_set_from_string (addr, item->data))
      {
        if (!nice_agent_add_local_address (self->agent,
                addr))
        {
          g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
              "Unable to set preferred local candidate");
          return FALSE;
        }
      }
      else
      {
        g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
            "Invalid local address passed");
        nice_address_free (addr);
        return FALSE;
      }
      nice_address_free (addr);
    }

    g_list_foreach (addresses, (GFunc) g_free, NULL);
    g_list_free (addresses);
  }

  return TRUE;
}

FsNiceAgent *
fs_nice_agent_new (guint compatibility_mode,
    GList *preferred_local_candidates,
    GError **error)
{
  FsNiceAgent *self = NULL;

  self = g_object_new (FS_TYPE_NICE_AGENT,
      "compatibility-mode", compatibility_mode,
      "preferred-local-candidates", preferred_local_candidates,
      NULL);

  self->agent = nice_agent_new (&self->priv->udpfactory,
      self->priv->main_context,
      self->priv->compatibility_mode);

  if (self->agent == NULL)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INTERNAL,
        "Could not make nice agent");
    g_object_unref (self);
    return NULL;
  }

  if (!fs_nice_agent_init_agent (self, error))
  {
    g_object_unref (self);
    return NULL;
  }

  FS_NICE_AGENT_LOCK (self);

  self->priv->thread = g_thread_create (fs_nice_agent_main_thread,
      self, TRUE, error);

  if (!self->priv->thread)
  {
    FS_NICE_AGENT_UNLOCK (self);
    g_object_unref (self);
    return NULL;
  }
  FS_NICE_AGENT_UNLOCK (self);

  return self;
}

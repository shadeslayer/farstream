/*
 * Farsight2 - Farsight libnice Transmitter thread object
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-nice-thread.c - A Farsight libnice transmitter thread object
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
 * SECTION:fs-nice-thread
 * @short_description: A transmitter for threads for libnice
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-nice-transmitter.h"
#include "fs-nice-thread.h"

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
  PROP_0
};

struct _FsNiceThreadPrivate
{
  GMainContext *main_context;
  GMainLoop *main_loop;

  GMutex *mutex;

  /* Everything below is protected by the mutex */

  GThread *thread;
};

#define FS_NICE_THREAD_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_NICE_THREAD, \
    FsNiceThreadPrivate))


#define FS_NICE_THREAD_LOCK(o)   g_mutex_lock ((o)->priv->mutex)
#define FS_NICE_THREAD_UNLOCK(o) g_mutex_unlock ((o)->priv->mutex)

static void fs_nice_thread_class_init (
    FsNiceThreadClass *klass);
static void fs_nice_thread_init (FsNiceThread *self);
static void fs_nice_thread_finalize (GObject *object);
static void fs_nice_thread_stop_thread (FsNiceThread *self);


static GObjectClass *parent_class = NULL;
//static guint signals[LAST_SIGNAL] = { 0 };


/*
 * Lets register the plugin
 */

static GType type = 0;

GType
fs_nice_thread_get_type (void)
{
  g_assert (type);
  return type;
}

GType
fs_nice_thread_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsNiceThreadClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_nice_thread_class_init,
    NULL,
    NULL,
    sizeof (FsNiceThread),
    0,
    (GInstanceInitFunc) fs_nice_thread_init
  };

  type = g_type_module_register_type (G_TYPE_MODULE (module),
      G_TYPE_INITIALLY_UNOWNED, "FsNiceThread", &info, 0);

  return type;
}

static void
fs_nice_thread_class_init (FsNiceThreadClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = fs_nice_thread_finalize;

  g_type_class_add_private (klass, sizeof (FsNiceThreadPrivate));
}

static void
fs_nice_thread_init (FsNiceThread *self)
{

  /* member init */
  self->priv = FS_NICE_THREAD_GET_PRIVATE (self);

  self->priv->mutex = g_mutex_new ();

  self->priv->main_context = g_main_context_new ();
  self->priv->main_loop = g_main_loop_new (self->priv->main_context, FALSE);
}

static void
fs_nice_thread_finalize (GObject *object)
{
  FsNiceThread *self = FS_NICE_THREAD (object);

  fs_nice_thread_stop_thread (self);

  if (self->priv->main_context)
  {
    g_main_context_unref (self->priv->main_context);
    self->priv->main_context = NULL;
  }

  if (self->priv->main_loop)
  {
    g_main_loop_unref (self->priv->main_loop);
    self->priv->main_loop = NULL;
  }

  g_mutex_free (self->priv->mutex);

  parent_class->finalize (object);
}


static gboolean
thread_unlock_idler (gpointer data)
{
  FsNiceThread *self = FS_NICE_THREAD (data);

  g_main_loop_quit (self->priv->main_loop);

  return TRUE;
}

static void
fs_nice_thread_stop_thread (FsNiceThread *self)
{
  GSource *idle_source;

  FS_NICE_THREAD_LOCK(self);

  if (self->priv->thread == NULL)
  {
    FS_NICE_THREAD_UNLOCK (self);
    return;
  }
  FS_NICE_THREAD_UNLOCK (self);

  g_main_loop_quit (self->priv->main_loop);

  idle_source = g_idle_source_new ();
  g_source_set_priority (idle_source, G_PRIORITY_HIGH);
  g_source_set_callback (idle_source, thread_unlock_idler, self, NULL);
  g_source_attach (idle_source, self->priv->main_context);

  g_thread_join (self->priv->thread);

  g_source_destroy (idle_source);
  g_source_unref (idle_source);

  FS_NICE_THREAD_LOCK (self);
  self->priv->thread = NULL;
  FS_NICE_THREAD_UNLOCK (self);
}

GMainContext *
fs_nice_thread_get_context (FsNiceThread *self)
{
  return self->priv->main_context;
}


void
fs_nice_thread_add_weak_object (FsNiceThread *self,
    GObject *object)
{
  g_object_weak_ref (G_OBJECT (object), (GWeakNotify) g_object_unref, self);

  g_object_ref_sink (self);
}


static gpointer
fs_nice_thread_main_thread (gpointer data)
{
  FsNiceThread *self = FS_NICE_THREAD (data);

  g_main_loop_run (self->priv->main_loop);

  return NULL;
}

FsNiceThread *
fs_nice_thread_new (GError **error)
{
  FsNiceThread *self = NULL;

  self = g_object_new (FS_TYPE_NICE_THREAD, NULL);

  FS_NICE_THREAD_LOCK (self);
  self->priv->thread = g_thread_create (fs_nice_thread_main_thread,
      self, TRUE, error);

  if (!self->priv->thread)
  {
    FS_NICE_THREAD_UNLOCK (self);
    g_object_unref (self);
    return NULL;
  }
  FS_NICE_THREAD_UNLOCK (self);

  return self;
}

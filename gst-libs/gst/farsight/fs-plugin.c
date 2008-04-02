/*
 * fs-plugin.c - Source for farsight plugin infrastructure
 *
 * Farsight Voice+Video library
 * Copyright (c) 2005 INdT.
 *   @author Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 * Copyright 2005-2007 Collabora Ltd.
 * Copyright 2005-2007 Nokia Corp.
 *   @author Rob Taylor <rob.taylor@collabora.co.uk>
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
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

#include "fs-plugin.h"

#include "fs-conference-iface.h"
#include "fs-private.h"

#include <string.h>

#define GST_CAT_DEFAULT fs_base_conference_debug

/**
 * SECTION:fs-plugin
 * @short_description: A class for defining Farsight plugins
 *
 * This class is a generic class to load GType plugins based on their name.
 * With this simple class, you can only have one type per plugin.
 */

#define FS_PLUGIN_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_PLUGIN, FsPluginPrivate))

static gboolean fs_plugin_load (GTypeModule *module);
static void fs_plugin_unload (GTypeModule *module);


static gchar **search_paths = NULL;

static GList *plugins = NULL;

static GObjectClass *parent_class = NULL;

struct _FsPluginPrivate
{
  GModule *handle;
  gboolean disposed;
};

G_DEFINE_TYPE(FsPlugin, fs_plugin, G_TYPE_TYPE_MODULE);

static void fs_plugin_dispose (GObject * object);
static void fs_plugin_finalize (GObject * object);

static void
fs_plugin_search_path_init (void)
{
  const gchar *env;

  if (search_paths)
    return;

  env = g_getenv ("FS_PLUGIN_PATH");

  if (env == NULL)
    {
      search_paths = g_new (gchar *, 2);
      search_paths[0] = g_strdup (FS2_PLUGIN_PATH);
      search_paths[1] = NULL;
      return;
    }
  else
    {
      gchar *path;

      path = g_strjoin (":", env, FS2_PLUGIN_PATH, NULL);
      search_paths = g_strsplit (path, ":", -1);
      g_free (path);
    }
}

static void
fs_plugin_class_init (FsPluginClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GTypeModuleClass *module_class = G_TYPE_MODULE_CLASS (klass);

  gobject_class->dispose = fs_plugin_dispose;
  gobject_class->finalize = fs_plugin_finalize;

  parent_class = g_type_class_peek_parent (klass);

  module_class->load = fs_plugin_load;
  module_class->unload = fs_plugin_unload;

  g_type_class_add_private (klass, sizeof (FsPluginPrivate));

  /* Calling from class initializer so it only gets init'ed once */
  fs_plugin_search_path_init ();
}



static void
fs_plugin_init (FsPlugin * plugin)
{
  /* member init */
  plugin->priv = FS_PLUGIN_GET_PRIVATE (plugin);
  plugin->priv->handle = NULL;
  plugin->priv->disposed = FALSE;
}


static void
fs_plugin_dispose (GObject * object)
{
  FsPlugin *plugin = FS_PLUGIN (object);

  if (plugin->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  plugin->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_plugin_finalize (GObject * object)
{
  parent_class->finalize (object);
}

static gboolean fs_plugin_load (GTypeModule *module)
{
  FsPlugin *plugin = FS_PLUGIN(module);
  gchar **search_path = NULL;
  gchar *path=NULL;

  gboolean (*fs_init_plugin) (FsPlugin *);

  g_return_val_if_fail (plugin != NULL, FALSE);
  g_return_val_if_fail (plugin->name != NULL && plugin->name[0] != '\0', FALSE);

  for (search_path = search_paths; *search_path; search_path++) {
    GST_DEBUG("looking for plugins in %s", *search_path);

    path = g_module_build_path (*search_path, plugin->name);

    plugin->priv->handle = g_module_open (path, G_MODULE_BIND_LOCAL);
    GST_INFO ("opening module %s: %s\n", path,
      (plugin->priv->handle != NULL) ? "succeeded" : g_module_error ());
    g_free (path);

    if (!plugin->priv->handle) {
      continue;
    }

    else if (!g_module_symbol (plugin->priv->handle,
                          "fs_init_plugin",
                          (gpointer) & fs_init_plugin)) {
      g_module_close (plugin->priv->handle);
      plugin->priv->handle = NULL;
      GST_WARNING ("could not find init function in plugin\n");
      continue;
    }

    else
      break;
  }

  if (!plugin->priv->handle) {
    return FALSE;
  }

  fs_init_plugin (plugin);
  if (!plugin->type) {
    /* TODO error handling (init error or no info defined) */
    GST_WARNING ("init error or no info defined");
    goto err_close_module;
  }

  return TRUE;

 err_close_module:
  g_module_close (plugin->priv->handle);
  return FALSE;

}

static void
fs_plugin_unload (GTypeModule *module)
{
  FsPlugin *plugin = NULL;

  g_return_if_fail (module != NULL);

  plugin = FS_PLUGIN (module);

  GST_INFO("Unloading plugin %s", plugin->name);

  if (plugin->unload != NULL)
    plugin->unload (plugin);

  if (plugin->priv->handle != NULL)
    g_module_close (plugin->priv->handle);
}

static FsPlugin *
fs_plugin_get_by_name (const gchar * name, const gchar * type_suffix)
{
  gchar *fullname;
  FsPlugin *plugin = NULL;
  GList *plugin_item;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (type_suffix != NULL, NULL);

  fullname = g_strdup_printf ("%s-%s",name,type_suffix);

  for (plugin_item = plugins;
       plugin_item;
       plugin_item = g_list_next (plugin_item))  {
    plugin = plugin_item->data;
    if (plugin->name == NULL || plugin->name[0] == 0)
      continue;
    if (!strcmp (plugin->name, fullname)) {
      break;
    }

  }
  g_free (fullname);

  if (plugin_item)
    return plugin;

  return NULL;
}


/**
 * fs_plugin_create_valist:
 * @name: The name of the plugin to load
 * @type_suffix: The type of plugin to load (normally "transmitter")
 * @error: location of a #GError, or NULL if no error occured
 * @first_property_name: The name of the first property to be set on the
 *   object
 * @var_args: The rest of the arguments
 *
 * Loads the appropriate plugin if necessary and creates a GObject of
 * the requested type
 *
 * Returns: The object created (or NULL if there is an error)
 **/

GObject *
fs_plugin_create_valist (const gchar *name, const gchar *type_suffix,
  GError **error, const gchar *first_property_name, va_list var_args)
{
  GObject *object;
  FsPlugin *plugin;

  fs_base_conference_init_debug ();

  if (name == NULL) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "You need to pass a name");
    return NULL;
  }
  if (type_suffix == NULL) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "You need to pass a type suffix");
    return NULL;
  }

  plugin = fs_plugin_get_by_name (name, type_suffix);

  if (!plugin) {
    plugin = g_object_new (FS_TYPE_PLUGIN, NULL);
    if (!plugin) {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
        "Could not create a fsplugin object");
      return NULL;
    }
    plugin->name = g_strdup_printf ("%s-%s",name,type_suffix);
    g_type_module_set_name (G_TYPE_MODULE (plugin), plugin->name);
    plugins = g_list_append (plugins, plugin);
  }

  if (!g_type_module_use (G_TYPE_MODULE (plugin))) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not load the %s-%s transmitter plugin", name, type_suffix);
    return NULL;
  }

  object = g_object_new_valist (plugin->type, first_property_name, var_args);

  g_type_module_unuse (G_TYPE_MODULE (plugin));

  return object;
}


/**
 * fs_plugin_create:
 * @name: The name of the plugin to load
 * @type_suffix: The type of plugin to load (normally "transmitter")
 * @error: location of a #GError, or NULL if no error occured
 * @first_property_name: The name of the first property to be set on the
 *   object
 * @...: The NULL-terminated list of properties to set on the transmitter
 *
 * Loads the appropriate plugin if necessary and creates a GObject of
 * the requested type
 *
 * Returns: The object created (or NULL if there is an error)
 **/

GObject *
fs_plugin_create (const gchar *name, const gchar *type_suffix,
  GError **error, const gchar *first_property_name, ...)
{
  va_list var_args;
  GObject *obj;

  va_start (var_args, first_property_name);
  obj = fs_plugin_create_valist (name, type_suffix, error, first_property_name,
    var_args);
  va_end (var_args);

  return obj;
}

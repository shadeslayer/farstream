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


static gchar **search_paths = NULL;

static GList *plugins = NULL;

struct _FsPluginPrivate
{
  GModule *handle;
};

G_DEFINE_TYPE(FsPlugin, fs_plugin, G_TYPE_TYPE_MODULE);

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
  GTypeModuleClass *module_class = G_TYPE_MODULE_CLASS (klass);

  module_class->load = fs_plugin_load;

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

  g_return_val_if_fail (name, NULL);
  g_return_val_if_fail (type_suffix, NULL);

  fs_base_conference_init_debug ();

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

    /* We do the use once and then we keep it loaded forever because
     * the gstreamer libraries can't be unloaded
     */
    if (!g_type_module_use (G_TYPE_MODULE (plugin))) {
      g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
          "Could not load the %s-%s transmitter plugin", name, type_suffix);
      return NULL;
    }
  }
  object = g_object_new_valist (plugin->type, first_property_name, var_args);

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

/**
 * fs_plugin_list_available:
 * @type_suffix: Get list of plugins with this type suffix
 *
 * Gets the list of all available plugins of a certain type
 *
 * Returns: a newly allocated NULL terminated array of strings or %NULL if no
 * strings were found. It should be freed with g_strfreev().
 */

gchar **
fs_plugin_list_available (const gchar *type_suffix)
{
  GPtrArray *list = g_ptr_array_new ();
  gchar **retval = NULL;
  gchar **search_path = NULL;
  GRegex *matcher;
  GError *error = NULL;
  gchar *tmp1, *tmp2, *tmp3;

  fs_plugin_search_path_init ();

  tmp1 = g_strdup_printf ("(.+)-%s", type_suffix);
  tmp2 = g_module_build_path ("", tmp1);
  tmp3 = g_strconcat ("^", tmp2, NULL);
  matcher = g_regex_new (tmp3, 0, 0, NULL);
  g_free (tmp1);
  g_free (tmp2);
  g_free (tmp3);


  for (search_path = search_paths; *search_path; search_path++)
  {
    GDir *dir = NULL;
    const gchar *entry;

    dir = g_dir_open (*search_path, 0, &error);
    if (!dir)
    {
      GST_WARNING ("Could not open path %s to look for plugins: %s",
          search_path, error ? error->message : "Unknown error");
      g_clear_error (&error);
      continue;
    }

    while ((entry = g_dir_read_name (dir)))
    {
      gchar **matches = NULL;

      matches = g_regex_split (matcher, entry, 0);

      if (matches && g_strv_length (matches) == 3)
      {
        gint i;
        gboolean found = FALSE;

        for (i = 0; i < list->len; i++)
        {
          if (!strcmp (matches[1], g_ptr_array_index (list, i)))
          {
            found = TRUE;
            break;
          }
        }
        if (!found)
          g_ptr_array_add (list, g_strdup (matches[1]));
      }

      g_strfreev (matches);
    }

    g_dir_close (dir);
  }

  g_regex_unref (matcher);

  if (list->len)
  {
    g_ptr_array_add (list, NULL);
    retval = (gchar**) list->pdata;
    g_ptr_array_free (list, FALSE);
  }
  else
  {
    g_ptr_array_free (list, TRUE);
  }

  return retval;
}

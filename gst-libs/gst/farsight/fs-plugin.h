/*
 * fs-plugin.h - Header for farsight plugin infrastructure
 *
 * Farsight Voice+Video library
 * Copyright (c) 2005 INdT.
 *   @author: Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 * Copyright (c) 2005-2007 Collabora Ltd.
 * Copyright (c) 2005-2007 Nokia Corp.
 *   @author: Rob Taylor <rob.taylor@collabora.co.uk>
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
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

#ifndef __FS_PLUGIN_H__
#define __FS_PLUGIN_H__

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <gst/gst.h>

#include <stdarg.h>

G_BEGIN_DECLS


/* TYPE MACROS */
#define FS_TYPE_PLUGIN \
  (fs_plugin_get_type())
#define FS_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_PLUGIN, FsPlugin))
#define FS_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_PLUGIN, FsPluginClass))
#define FS_IS_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_PLUGIN))
#define FS_IS_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_PLUGIN))
#define FS_PLUGIN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_PLUGIN, FsPluginClass))

typedef struct _FsPlugin FsPlugin;
typedef struct _FsPluginClass FsPluginClass;
typedef struct _FsPluginPrivate FsPluginPrivate;

struct _FsPlugin
{
  GTypeModule parent;

  /*< private >*/

  GType  type;

  gchar *name;			/* name of the plugin */

  /* callbacks */
  /* This function is called when the last instance of the plugin is
   * unloaded. It can be useful to deallocate resources common for all
   * instances of the plugin. */
  void (*unload) (FsPlugin * plugin);

  FsPluginPrivate *priv;
};

struct _FsPluginClass
{
  GTypeModuleClass parent_class;

};

GType fs_plugin_get_type (void);


GObject *fs_plugin_create_valist (const gchar *name,
                                  const gchar *type_suffix,
                                  GError **error,
                                  const gchar *first_property_name,
                                  va_list var_args);


/**
 * FS_INIT_PLUGIN:
 * @type: The #GType that this plugin provides
 * @unload: a function of type void (*unload) (FsPlugin * plugin) to be called
 * when the plugin is unloaded
 *
 * This macro is used to declare Farsight plugins and must be used once
 * in any farsight plugin.
 */

#define FS_INIT_PLUGIN(type_register_func, inunload)            \
    G_MODULE_EXPORT void fs_init_plugin (FsPlugin *plugin) {    \
      plugin->type = (type_register_func (plugin));             \
      plugin->unload = (inunload);                              \
    }


G_END_DECLS
#endif


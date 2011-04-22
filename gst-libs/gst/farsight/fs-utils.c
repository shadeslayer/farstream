/*
 * Farsight2 - Miscellaneous useful functions
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
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
# include <config.h>
#endif

#include "fs-utils.h"

#include <string.h>

/**
 * SECTION:fs-utils
 * @short_description: Miscellaneous useful functions
 */

static GList *
load_default_codec_preferences_from_path (const gchar *element_name,
    const gchar *path)
{
  GList *codec_prefs = NULL;
  gchar *filename;

  filename = g_build_filename (path, PACKAGE, FS2_MAJORMINOR, element_name,
      "default-codec-preferences", NULL);
  codec_prefs = fs_codec_list_from_keyfile (filename, NULL);
  g_free (filename);

  return codec_prefs;
}

static const gchar *
factory_name_from_element (GstElement *element)
{
  GstElementFactory *factory = gst_element_get_factory (element);

  if (factory)
    return gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory));
  else
    return NULL;
}

/**
 * fs_utils_get_default_codec_preferences:
 * @element: Element for which to fetch default codec preferences
 *
 * These default codec preferences should work with the elements that are
 * available in the main GStreamer element repositories.
 * They should be suitable for standards based protocols like SIP or XMPP.
 *
 * Returns: The default codec preferences for this plugin,
 * this #GList should be freed with fs_codec_list_destroy()
 */
GList *
fs_utils_get_default_codec_preferences (GstElement *element)
{
  const gchar * const * system_data_dirs = g_get_system_data_dirs ();
  GList *codec_prefs = NULL;
  guint i;
  const gchar *factory_name = factory_name_from_element (element);

  if (!factory_name)
    return NULL;

  codec_prefs = load_default_codec_preferences_from_path (factory_name,
      g_get_user_data_dir ());
  if (codec_prefs)
    return codec_prefs;

  for (i = 0; system_data_dirs[i]; i++)
  {
    codec_prefs = load_default_codec_preferences_from_path (factory_name,
        system_data_dirs[i]);
    if (codec_prefs)
      return codec_prefs;
  }

  return NULL;
}

/**
 * fs_utils_get_default_element_properties:
 * @element: Element for which to fetch default element properties
 *
 * This function produces a #GKeyFile that can be fed to
 * fs_element_added_notifier_set_properties_from_keyfile(). If no
 * default properties have been found, it will return %NULL.
 *
 * Returns: a #GKeyFile containing the default element properties for this
 *  element or %NULL if no properties were found. Caller must free
 * the #GKeyFile when he is done.
 */

GKeyFile *
fs_utils_get_default_element_properties (GstElement *element)
{
  gboolean file_loaded;
  GKeyFile *keyfile = g_key_file_new ();
  gchar *filename;
  const gchar *factory_name = factory_name_from_element (element);

  filename = g_build_filename (PACKAGE, FS2_MAJORMINOR, factory_name,
      "default-element-properties", NULL);
  file_loaded = g_key_file_load_from_data_dirs (keyfile, filename, NULL,
      G_KEY_FILE_NONE, NULL);
  g_free (filename);

  if (file_loaded)
  {
    return keyfile;
  }
  else
  {
    g_key_file_free (keyfile);
    return NULL;
  }
}

/**
 * fs_utils_set_bitrate:
 * @element: The #GstElement
 * @bitrate: The bitrate in bits/sec
 *
 * This allows setting the bitrate on all elements that have a "bitrate"
 * property without having to know the type or of the unit used by that element.
 *
 * This will be obsolete in 0.11 (when all elements use bit/sec for the
 * "bitrate" property.
 */

void
fs_utils_set_bitrate (GstElement *element, glong bitrate)
{
  GParamSpec *spec;
  const char *elements_in_kbps[] = { "lamemp3enc", "lame", "x264enc", "twolame",
    "mpeg2enc", NULL
  };
  int i;
  GstElementFactory *factory;
  const gchar *factory_name = NULL;

  g_return_if_fail (GST_IS_ELEMENT (element));

  spec = g_object_class_find_property (G_OBJECT_GET_CLASS (element), "bitrate");
  g_return_if_fail (spec != NULL);

  factory = gst_element_get_factory (element);
  if (factory)
    factory_name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory));

  /* divide by 1000 for elements that are known to use kbs */
  for (i = 0; elements_in_kbps[i]; i++)
    if (factory_name && !strcmp (factory_name, elements_in_kbps[i]))
    {
      bitrate /= 1000;
      break;
    }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_LONG)
  {
    g_object_set (element, "bitrate", (glong) CLAMP (bitrate,
            G_PARAM_SPEC_LONG (spec)->minimum,
            G_PARAM_SPEC_LONG (spec)->maximum), NULL);
  }
  else if (G_PARAM_SPEC_VALUE_TYPE (spec) == G_TYPE_ULONG)
  {
    g_object_set (element, "bitrate", (gulong) CLAMP (bitrate,
            G_PARAM_SPEC_ULONG (spec)->minimum,
            G_PARAM_SPEC_ULONG (spec)->maximum), NULL);
  }
  else if (G_PARAM_SPEC_VALUE_TYPE (spec) == G_TYPE_INT)
  {
    gint tmp = MIN (bitrate, G_MAXINT);

    g_object_set (element, "bitrate", (gint)  CLAMP (tmp,
            G_PARAM_SPEC_INT (spec)->minimum,
            G_PARAM_SPEC_INT (spec)->maximum), NULL);
  }
  else if (G_PARAM_SPEC_VALUE_TYPE (spec) == G_TYPE_UINT)
  {
    guint tmp = MIN (bitrate, G_MAXUINT);

    g_object_set (element, "bitrate", (guint) CLAMP (tmp,
            G_PARAM_SPEC_UINT (spec)->minimum,
            G_PARAM_SPEC_UINT (spec)->maximum), NULL);
  }
  else
  {
    g_warning ("bitrate parameter of unknown type");
  }
}

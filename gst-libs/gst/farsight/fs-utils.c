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

/**
 * fs_utils_get_default_codec_preferences:
 * @element_name: Name of the Farsight2 element that these codec
 *                preferences are for
 *
 * These default codec preferences should work with the elements that are
 * available in the main GStreamer element repositories.
 * They should be suitable for standards based protocols like SIP or XMPP.
 *
 * Returns: The default codec preferences for this plugin,
 * this #GList should be freed with fs_codec_list_destroy()
 */
GList *
fs_utils_get_default_codec_preferences (const gchar *element_name)
{
  const gchar * const * system_data_dirs = g_get_system_data_dirs ();
  GList *codec_prefs = NULL;
  guint i;

  for (i = 0; system_data_dirs[i]; i++)
  {
    gchar *filename = g_build_filename (system_data_dirs[i], PACKAGE,
        FS2_MAJORMINOR, element_name, "default-codec-preferences", NULL);

    codec_prefs = fs_codec_list_from_keyfile (filename, NULL);
    g_free (filename);

    if (codec_prefs)
      return codec_prefs;
  }

  return NULL;
}

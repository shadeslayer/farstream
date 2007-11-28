/* Farsight 2 ad-hoc test for the rtp codec discovery
 *
 * Copyright (C) 2007 Collabora, Nokia
 * @author: Olivier Crete <olivier.crete@collabora.co.uk>
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

#include <gst/gst.h>

#include <gst/farsight/fs-codec.h>

#include "fs-rtp-discover-codecs.h"

int main (int argc, char **argv)
{
  GList *elements = NULL;
  GError *error = NULL;

  gst_init (&argc, &argv);

  g_debug ("AUDIO STARTING!!");

  elements = fs_rtp_blueprints_get (FS_MEDIA_TYPE_AUDIO, &error);

  if (error)
    g_debug ("Error: %s", error->message);

  g_clear_error (&error);
  fs_rtp_blueprints_unref (FS_MEDIA_TYPE_AUDIO);

  g_debug ("AUDIO FINISHED!!");


  g_debug ("VIDEO STARTING!!");

  elements = fs_rtp_blueprints_get (FS_MEDIA_TYPE_VIDEO, &error);

  if (error)
    g_debug ("Error: %s", error->message);

  g_clear_error (&error);

  fs_rtp_blueprints_unref (FS_MEDIA_TYPE_VIDEO);

  g_debug ("VIDEO FINISHED!!");

  return 0;
}

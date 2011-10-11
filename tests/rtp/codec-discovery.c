/* Farstream ad-hoc test for the rtp codec discovery
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

#include <gst/farstream/fs-codec.h>

#include "fs-rtp-discover-codecs.h"
#include "fs-rtp-conference.h"


static void
debug_pipeline (GList *pipeline)
{
  GList *walk;

  for (walk = pipeline; walk; walk = g_list_next (walk))
  {
    GList *walk2;
    for (walk2 = g_list_first (walk->data); walk2; walk2 = g_list_next (walk2))
      g_message ("%p:%d:%s ", walk2->data,
        GST_OBJECT_REFCOUNT_VALUE(GST_OBJECT (walk2->data)),
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (walk2->data)));
    g_message ("--");
  }
}

static void
debug_blueprint (CodecBlueprint *blueprint)
{
  gchar *str;

  str = fs_codec_to_string (blueprint->codec);
  g_message ("Codec: %s", str);
  g_free (str);

  str = gst_caps_to_string (blueprint->media_caps);
  g_message ("media_caps: %s", str);
  g_free (str);

  str = gst_caps_to_string (blueprint->rtp_caps);
  g_message ("rtp_caps: %s", str);
  g_free (str);

  g_message ("send pipeline:");
  debug_pipeline (blueprint->send_pipeline_factory);

  g_message ("recv pipeline:");
  debug_pipeline (blueprint->receive_pipeline_factory);

  g_message ("================================");
}

int main (int argc, char **argv)
{
  GList *elements = NULL;
  GError *error = NULL;

  gst_init (&argc, &argv);

  GST_DEBUG_CATEGORY_INIT (fsrtpconference_debug, "fsrtpconference", 0,
      "Farstream RTP Conference Element");
  GST_DEBUG_CATEGORY_INIT (fsrtpconference_disco, "fsrtpconference_disco",
      0, "Farstream RTP Codec Discovery");
  GST_DEBUG_CATEGORY_INIT (fsrtpconference_nego, "fsrtpconference_nego",
      0, "Farstream RTP Codec Negotiation");

  gst_debug_set_default_threshold (GST_LEVEL_WARNING);

  g_message ("AUDIO STARTING!!");

  elements = fs_rtp_blueprints_get (FS_MEDIA_TYPE_AUDIO, &error);

  if (error)
    g_message ("Error: %s", error->message);
  else
    g_list_foreach (elements, (GFunc) debug_blueprint, NULL);

  g_clear_error (&error);

  fs_rtp_blueprints_unref (FS_MEDIA_TYPE_AUDIO);

  g_message ("AUDIO FINISHED!!");


  g_message ("VIDEO STARTING!!");

  elements = fs_rtp_blueprints_get (FS_MEDIA_TYPE_VIDEO, &error);

  if (error)
    g_message ("Error: %s", error->message);
  else
    g_list_foreach (elements, (GFunc) debug_blueprint, NULL);

  g_clear_error (&error);

  fs_rtp_blueprints_unref (FS_MEDIA_TYPE_VIDEO);

  g_message ("VIDEO FINISHED!!");

  return 0;
}

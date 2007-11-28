

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

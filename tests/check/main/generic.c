/* Farsigh2 generic unit tests for conferences
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif


#include "generic.h"

#include <gst/check/gstcheck.h>
#include <gst/farsight/fs-conference-iface.h>

struct SimpleTestConference *
setup_simple_conference (
    gchar *conference_elem,
    gchar *cname)
{
  struct SimpleTestConference *dat = g_new0 (struct SimpleTestConference, 1);
  GError *error = NULL;

  dat->pipeline = gst_pipeline_new ("pipeline");
  fail_if (dat->pipeline == NULL);

  dat->conference = gst_element_factory_make (conference_elem, NULL);
  fail_if (dat->conference == NULL, "Could not build %s", conference_elem);
  fail_unless (gst_bin_add (GST_BIN (dat->pipeline), dat->conference),
      "Could not add conference to the pipeline");

  dat->session = fs_conference_new_session (FS_CONFERENCE (dat->conference),
      FS_MEDIA_TYPE_AUDIO, &error);
  if (error)
    fail ("Error while creating new session (%d): %s",
        error->code, error->message);
  fail_if (dat->session == NULL, "Could not make session, but no GError!");

  dat->participant = fs_conference_new_participant (
      FS_CONFERENCE (dat->conference), cname, &error);
  if (error)
    fail ("Error while creating new participant (%d): %s",
        error->code, error->message);
  fail_if (dat->session == NULL, "Could not make participant, but no GError!");

  dat->stream = fs_session_new_stream (dat->session, dat->participant,
      FS_DIRECTION_BOTH, "rawudp", 0, NULL, &error);
  if (error)
    fail ("Error while creating new stream (%d): %s",
        error->code, error->message);
  fail_if (dat->stream == NULL, "Could not make stream, but no GError!");

  return dat;
}


void
cleanup_simple_conference (struct SimpleTestConference *dat)
{

  g_object_unref (dat->stream);
  g_object_unref (dat->session);
  g_object_unref (dat->participant);
  gst_object_unref (dat->pipeline);
  g_free (dat);
}


void
setup_fakesrc (struct SimpleTestConference *dat)
{
  GstElement *capsfilter = NULL;
  GstCaps *caps = NULL;
  GstPad *sinkpad = NULL, *srcpad = NULL;

  g_debug ("Adding fakesrc");

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  fail_if (capsfilter == NULL, "Could not make capsfilter");
  gst_bin_add (GST_BIN (dat->pipeline), capsfilter);

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, 8000,
      "channels", G_TYPE_INT, 1,
      NULL);

  g_object_set (capsfilter, "caps", caps, NULL);

  gst_caps_unref (caps);

  g_object_get (dat->session, "sink-pad", &sinkpad, NULL);
  fail_if (sinkpad == NULL, "Could not get session sinkpad");

  srcpad = gst_element_get_static_pad (capsfilter, "src");

  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link the capsfilter and the fsrtpconference");

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  dat->fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fail_if (dat->fakesrc == NULL, "Could not make fakesrc");
  gst_bin_add (GST_BIN (dat->pipeline), dat->fakesrc);

  g_object_set (dat->fakesrc,
      /* "num-buffers", 2000, */
      "sizetype", 2,
      "sizemax", 10,
      "is-live", TRUE,
      NULL);

  fail_unless (gst_element_link_pads (dat->fakesrc, "src", capsfilter, "sink"),
      "Could not link capsfilter to sink");

  if (dat->started)
  {
    gst_element_set_state (dat->pipeline, GST_STATE_PLAYING);
    dat->started = TRUE;
  }

}

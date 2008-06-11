/* Farsight 2 unit tests for FsRtpConferenceu
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

#include <gst/check/gstcheck.h>
#include <gst/farsight/fs-conference-iface.h>
#include <gst/farsight/fs-stream-transmitter.h>

#include "generic.h"

GMainLoop *loop = NULL;

void
_notify_local_codecs (GObject *object, GParamSpec *param, gpointer user_data)
{
  guint *value = user_data;
  *value = 1;
}

GST_START_TEST (test_rtpcodecs_local_codecs_config)
{
  struct SimpleTestConference *dat = NULL;
  GList *orig_codecs = NULL, *codecs = NULL, *codecs2 = NULL, *item = NULL;
  gint has0 = FALSE, has8 = FALSE;
  gboolean local_codecs_notified = FALSE;
  GError *error = NULL;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");

  g_object_get (dat->session, "local-codecs", &orig_codecs, NULL);

  fail_unless (fs_session_set_local_codecs_config (dat->session, orig_codecs,
          &error), "Could not set local codecs as codec config");

  g_object_get (dat->session, "local-codecs", &codecs, NULL);

  fail_unless (fs_codec_list_are_equal (orig_codecs, codecs),
      "Setting local codecs as preferences changes the list of local codecs");

  fs_codec_list_destroy (codecs);
  codecs = NULL;

  for (item = g_list_first (orig_codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == 0)
      has0 = TRUE;
    else if (codec->id == 8)
      has8 = TRUE;
  }
  fail_unless (has0 && has8, "You need the PCMA and PCMU encoder and payloades"
      " from gst-plugins-good");

  codecs = g_list_append (NULL,
      fs_codec_new (
          FS_CODEC_ID_DISABLE,
          "PCMU",
          FS_MEDIA_TYPE_AUDIO,
          8000));

  {
    FsCodec *codec = fs_codec_new (
        FS_CODEC_ID_ANY,
        "PCMA",
        FS_MEDIA_TYPE_AUDIO,
        8000);
    fs_codec_add_optional_parameter (codec, "p1", "v1");
    codecs = g_list_append (codecs, codec);
  }

  g_signal_connect (dat->session, "notify::local-codecs",
      G_CALLBACK (_notify_local_codecs), &local_codecs_notified);

  fail_unless (
      fs_session_set_local_codecs_config (dat->session, codecs, &error),
      "Could not set local codecs config");
  fail_unless (error == NULL, "Setting the local codecs config failed,"
      " but the error is still there");

  fail_unless (local_codecs_notified == TRUE, "Not notified of codec changed");
  local_codecs_notified = FALSE;

  g_object_get (dat->session, "local-codecs-config", &codecs2, NULL);

  fail_unless (g_list_length (codecs2) == 2,
      "Returned list from local-codecs-config is wrong length");

  fail_unless (fs_codec_are_equal (codecs->data, codecs2->data),
      "local-codecs-config first element wrong");
  fail_unless (fs_codec_are_equal (codecs->next->data, codecs2->next->data),
      "local-codecs-config second element wrong");

  fs_codec_list_destroy (codecs);
  fs_codec_list_destroy (codecs2);

  g_object_get (dat->session, "local-codecs", &codecs, NULL);

  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    fail_if (!strcmp (codec->encoding_name, "PCMU"),
        "PCMU codec was not removed as requested");

    if (!strcmp (codec->encoding_name, "PCMA"))
    {
      fail_if (codec->optional_params == NULL, "No optional params for PCMA");
      fail_unless (g_list_length (codec->optional_params) == 1,
          "Too many optional params for PCMA");
      fail_unless (
          !strcmp (((FsCodecParameter*)codec->optional_params->data)->name,
              "p1") &&
          !strcmp (((FsCodecParameter*)codec->optional_params->data)->value,
              "v1"),
          "Not the right data in optional params for PCMA");
    }
  }

  fs_codec_list_destroy (codecs);

  fail_unless (fs_session_set_local_codecs_config (dat->session, NULL, &error),
      "Could not set local-codecs-config");
  fail_if (error, "Error set while function succeeded?");
  fail_unless (local_codecs_notified, "We were not notified of the change"
      " in local-codecs");

  g_object_get (dat->session, "local-codecs", &codecs, NULL);

  fail_unless (fs_codec_list_are_equal (codecs, orig_codecs),
      "Resetting local-codecs-config failed, codec lists are not equal");

  fs_codec_list_destroy (orig_codecs);

  for (item = codecs;
       item;
       item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    codec->id = FS_CODEC_ID_DISABLE;
  }

  fail_if (fs_session_set_local_codecs_config (dat->session, codecs,
          &error),
      "Disabling all codecs did not fail");
  fail_unless (error != NULL, "The error is not set");
  fail_unless (error->code == FS_ERROR_NO_CODECS,
      "The error code is %d, not FS_ERROR_NO_CODECS");

  g_clear_error (&error);

  fs_codec_list_destroy (codecs);

  cleanup_simple_conference (dat);
}
GST_END_TEST;

static gboolean has_negotiated = FALSE;

static void
_negotiated_codecs_notify (GObject *object, GParamSpec *paramspec,
    gpointer user_data)
{
  has_negotiated = TRUE;
}

GST_START_TEST (test_rtpcodecs_two_way_negotiation)
{
  struct SimpleTestConference *dat = NULL;
  struct SimpleTestStream *st = NULL;
  GList *codecs = NULL, *codecs2 = NULL;
  GError *error = NULL;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");
  st = simple_conference_add_stream (dat, dat);

  g_signal_connect (dat->session, "notify::negotiated-codecs",
      G_CALLBACK (_negotiated_codecs_notify), dat);

  codecs = g_list_append (codecs,
      fs_codec_new (
          FS_CODEC_ID_ANY,
          "PCMU",
          FS_MEDIA_TYPE_AUDIO,
          8000));

  fail_if (fs_stream_set_remote_codecs (st->stream, codecs, &error),
      "set_remote_codecs did not reject invalid PT");

  fail_unless (error && error->code == FS_ERROR_INVALID_ARGUMENTS,
      "Did not get the right error codec");

  g_clear_error (&error);

  fs_codec_list_destroy (codecs);
  codecs = NULL;

  codecs = g_list_append (codecs,
      fs_codec_new (
          0,
          "PCMU",
          FS_MEDIA_TYPE_AUDIO,
          8000));


  fail_unless (fs_stream_set_remote_codecs (st->stream, codecs, &error),
      "Could not set remote PCMU codec");

  fail_unless (has_negotiated == TRUE,
      "Did not receive the notify::negotiated-codecs signal");

  g_object_get (dat->session, "negotiated-codecs", &codecs2, NULL);
  fail_unless (g_list_length (codecs2) == 1, "Too many negotiated codecs");
  fail_unless (fs_codec_are_equal (codecs->data, codecs2->data),
      "Negotiated codec does not match remote codec");
  fs_codec_list_destroy (codecs2);

  has_negotiated = FALSE;

  fail_unless (fs_stream_set_remote_codecs (st->stream, codecs, &error),
      "Could not re-set remote PCMU codec");

  fail_if (has_negotiated == TRUE,
      "We received the notify::negotiated-codecs signal even though codecs"
      " have not changed");

  fs_codec_list_destroy (codecs);

  cleanup_simple_conference (dat);
}
GST_END_TEST;

GST_START_TEST (test_rtpcodecs_invalid_remote_codecs)
{
  struct SimpleTestConference *dat = NULL;
  struct SimpleTestStream *st = NULL;
  GList *codecs = NULL;
  GError *error = NULL;
  gboolean rv;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");
  st = simple_conference_add_stream (dat, dat);

  codecs = g_list_prepend (codecs,
      fs_codec_new (1, "INVALID1", FS_MEDIA_TYPE_AUDIO, 1));
  codecs = g_list_prepend (codecs,
      fs_codec_new (2, "INVALID2", FS_MEDIA_TYPE_AUDIO, 1));

  rv = fs_stream_set_remote_codecs (st->stream, codecs, &error);

  fail_unless (rv == FALSE, "Invalid codecs did not fail");
  fail_if (error == NULL, "Error not set on invalid codecs");
  fail_unless (error->domain == FS_ERROR, "Error not of domain FS_ERROR");
  fail_unless (error->code == FS_ERROR_NEGOTIATION_FAILED, "Error isn't"
      " negotiation failed, it is %d", error->code);

  g_clear_error (&error);

  fs_codec_list_destroy (codecs);

  cleanup_simple_conference (dat);
}
GST_END_TEST;


GST_START_TEST (test_rtpcodecs_reserved_pt)
{
  struct SimpleTestConference *dat = NULL;
  GList *codecs = NULL, *item = NULL;
  GList *codec_prefs = NULL;
  FsParticipant *p = NULL;
  FsStream *s = NULL;
  guint id = 96;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");

  g_object_get (dat->session, "local-codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;

    id = codec->id;
    if (codec->id >= 96)
      break;
  }
  fs_codec_list_destroy (codecs);

  if (!item)
  {
    g_warning ("Could not find a dynamically allocated codec, skipping testing"
               " of the payload-type reservation mecanism");
    goto out;
  }

  codec_prefs = g_list_prepend (NULL, fs_codec_new (id, "reserve-pt",
                                               FS_MEDIA_TYPE_AUDIO, 0));

  fail_unless (fs_session_set_local_codecs_config (dat->session, codec_prefs,
          NULL), "Could not set local-codes config");

  g_object_get (dat->session, "local-codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == id)
      break;
  }
  fail_if (item, "Found codec with payload type %u, even though it should have"
      " been reserved", id);
  fs_codec_list_destroy (codecs);

  cleanup_simple_conference (dat);

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");

  p = fs_conference_new_participant (FS_CONFERENCE (dat->conference),
      "aa", NULL);
  fail_if (p == NULL, "Could not add participant");

  s = fs_session_new_stream (dat->session, p,
      FS_DIRECTION_BOTH, "rawudp", 0, NULL, NULL);
  fail_if (s == NULL, "Could not add stream");
  g_object_unref (p);

  g_object_get (dat->session, "local-codecs", &codecs, NULL);

  fail_unless (fs_stream_set_remote_codecs (s, codecs, NULL),
               "Could not set local codecs as remote codecs");

  fs_codec_list_destroy (codecs);

  g_object_get (dat->session, "negotiated-codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == id)
      break;
  }
  fs_codec_list_destroy (codecs);

  fail_if (item == NULL, "There is no pt %u in the negotiated codecs, "
      "but there was one in the local codecs", id);

  fail_unless (fs_session_set_local_codecs_config (dat->session, codec_prefs,
          NULL), "Could not set local-codes config after set_remote_codecs");

  g_object_get (dat->session, "local-codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == id)
      break;
  }
  fail_if (item, "Found codec with payload type %u, even though it should have"
      " been disabled", id);
  fs_codec_list_destroy (codecs);


  fail_unless (fs_session_set_local_codecs_config (dat->session, codec_prefs,
          NULL), "Could not re-set local-codes config after set_remote_codecs");

  g_object_get (dat->session, "local-codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == id)
      break;
  }
  fail_if (item, "Found codec with payload type %u, even though it should have"
      " been disabled", id);
  fs_codec_list_destroy (codecs);

  fs_codec_list_destroy (codec_prefs);

  g_object_unref (s);
 out:
  cleanup_simple_conference (dat);
}
GST_END_TEST;


static void
_bus_message_element (GstBus *bus, GstMessage *message,
    struct SimpleTestConference *dat)
{
  GList *codecs = NULL;
  GList *item = NULL;
  FsCodec *codec = NULL;
  gboolean ready;
  const GstStructure *s = gst_message_get_structure (message);
  FsParticipant *p = NULL;
  FsStream *stream = NULL;
  const gchar config[] = "asildksahkjewafrefenbwqgiufewaiufhwqiu"
    "enfiuewfkdnwqiucnwiufenciuawndiunfucnweciuqfiucina";
  GError *error = NULL;

  if (!gst_structure_has_name (s, "farsight-codecs-ready"))
    return;

  g_object_get (dat->session, "codecs-ready", &ready, NULL);

  fail_unless (ready, "Got ready bus message, but codecs aren't ready yet");

  g_object_get (dat->session, "negotiated-codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    codec = item->data;
    if (!g_ascii_strcasecmp ("vorbis", codec->encoding_name))
      break;
  }

  fail_if (item == NULL, "Could not find Vorbis in local codecs");

  for (item = codec->optional_params; item; item = g_list_next (item))
  {
    FsCodecParameter *param = item->data;

    if (!g_ascii_strcasecmp (param->name, "configuration"))
      break;
  }

  fail_if (item == NULL, "The session claims to have found all configuration"
      " params, but Vorbis does not have the \"configuration\" parameter");

  fs_codec_list_destroy (codecs);

  p = fs_conference_new_participant (FS_CONFERENCE (dat->conference), "name",
      NULL);

  fail_if (p == NULL, "Could not add participant to conference");

  stream = fs_session_new_stream (dat->session, p, FS_DIRECTION_BOTH,
      "rawudp", 0, NULL, NULL);

  fail_if (stream == NULL, "Could not create new stream");

  codec = fs_codec_new (105, "VORBIS", FS_MEDIA_TYPE_AUDIO, 44100);

  fs_codec_add_optional_parameter (codec, "delivery-method", "inline");
  fs_codec_add_optional_parameter (codec, "configuration", config);

  codecs = g_list_prepend (NULL, codec);

  if (!fs_stream_set_remote_codecs (stream, codecs, &error))
  {
    if (error)
      fail ("Could not set vorbis as remote codec on the stream: %s",
          error->message);
    else
      fail ("Could not set vorbis as remote codec on the stream"
          " WITHOUT SETTING THE GError");
  }

  fs_codec_list_destroy (codecs);

  g_object_get (dat->session, "negotiated-codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    codec = item->data;
    if (!g_ascii_strcasecmp ("vorbis", codec->encoding_name))
      break;
  }

  fail_if (item == NULL, "Could not find Vorbis in negotiated codecs after"
      " negotiation");

  for (item = codec->optional_params; item; item = g_list_next (item))
  {
    FsCodecParameter *param = item->data;

    if (!g_ascii_strcasecmp (param->name, "configuration"))
      break;
  }

  g_object_get (dat->session, "codecs-ready", &ready, NULL);

  fail_if (item == NULL, "The configuration parameter is no longer in the"
      " vorbis codec after the negotiation");
  fs_codec_list_destroy (codecs);



  g_object_get (stream, "negotiated-codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    codec = item->data;
    if (!g_ascii_strcasecmp ("vorbis", codec->encoding_name))
      break;
  }

  fail_if (item == NULL, "Could not find Vorbis in negotiated codecs"
      " on the stream");

  for (item = codec->optional_params; item; item = g_list_next (item))
  {
    FsCodecParameter *param = item->data;

    if (!g_ascii_strcasecmp (param->name, "configuration"))
    {
      fail_if (strcmp (param->value, config),
          "The value of the configuration param on the stream in not what it"
          " was set to");
      break;
    }
  }

  fail_if (item == NULL, "The configuration parameter is not in the stream"
      "codec list after the negotiation");
  fs_codec_list_destroy (codecs);

  g_object_unref (p);
  g_object_unref (stream);

  g_main_loop_quit (loop);
}

GST_START_TEST (test_rtpcodecs_config_data)
{
  struct SimpleTestConference *dat = NULL;
  GList *codecs = NULL, *item = NULL;
  gboolean ready;
  GError *error = NULL;
  GstBus *bus = NULL;

  loop = g_main_loop_new (NULL, FALSE);

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");

  codecs = g_list_prepend (NULL, fs_codec_new (FS_CODEC_ID_ANY, "VORBIS",
          FS_MEDIA_TYPE_AUDIO, 44100));

  fail_unless (fs_session_set_local_codecs_config (dat->session, codecs,
          &error),
      "Unable to set local codecs config: %s",
      error ? error->message : "UNKNOWN");

  fs_codec_list_destroy (codecs);

  g_object_get (dat->session, "local-codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (!g_ascii_strcasecmp ("vorbis", codec->encoding_name))
      break;

  }
  fs_codec_list_destroy (codecs);

  if (!item)
  {
    g_warning ("Could not find Vorbis encoder/decoder/payloader/depayloaders,"
        " so we are skipping the config-data test");
    goto out;
  }

  g_object_get (dat->session, "codecs-ready", &ready, NULL);

  fail_if (ready, "Codecs are ready before the pipeline is playing, it does not"
      " try to detect vorbis codec data");

  setup_fakesrc (dat);

  bus = gst_pipeline_get_bus (GST_PIPELINE (dat->pipeline));

  gst_bus_add_signal_watch (bus);

  g_signal_connect (bus, "message::element", G_CALLBACK (_bus_message_element),
      dat);

  fail_if (gst_element_set_state (dat->pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  g_main_loop_run (loop);

  gst_bus_remove_signal_watch (bus);

  gst_object_unref (bus);

  fail_if (gst_element_set_state (dat->pipeline, GST_STATE_NULL) ==
      GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to null");

 out:
  g_main_loop_unref (loop);
  cleanup_simple_conference (dat);
}
GST_END_TEST;


static Suite *
fsrtpcodecs_suite (void)
{
  Suite *s = suite_create ("fsrtpcodecs");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;


  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);


  tc_chain = tcase_create ("fsrtpcodecs_local_codecs_config");
  tcase_add_test (tc_chain, test_rtpcodecs_local_codecs_config);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_two_way_negotiation");
  tcase_add_test (tc_chain, test_rtpcodecs_two_way_negotiation);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_invalid_remote_codecs");
  tcase_add_test (tc_chain, test_rtpcodecs_invalid_remote_codecs);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_reserved_pt");
  tcase_add_test (tc_chain, test_rtpcodecs_reserved_pt);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_config_data");
  tcase_add_test (tc_chain, test_rtpcodecs_config_data);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (fsrtpcodecs);

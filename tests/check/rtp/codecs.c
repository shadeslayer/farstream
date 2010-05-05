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
_notify_codecs (GObject *object, GParamSpec *param, gpointer user_data)
{
  guint *value = user_data;
  *value = 1;
}

GST_START_TEST (test_rtpcodecs_codec_preferences)
{
  struct SimpleTestConference *dat = NULL;
  GList *orig_codecs = NULL, *codecs = NULL, *codecs2 = NULL, *item = NULL;
  gint has0 = FALSE, has8 = FALSE;
  gboolean local_codecs_notified = FALSE;
  GError *error = NULL;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");

  g_object_get (dat->session, "codecs", &orig_codecs, NULL);

  fail_unless (fs_session_set_codec_preferences (dat->session, orig_codecs,
          &error), "Could not set local codecs as codec preferences");

  g_object_get (dat->session, "codecs", &codecs, NULL);

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

  g_signal_connect (dat->session, "notify::codecs",
      G_CALLBACK (_notify_codecs), &local_codecs_notified);

  fail_unless (
      fs_session_set_codec_preferences (dat->session, codecs, &error),
      "Could not set codec preferences");
  fail_unless (error == NULL, "Setting the local codecs preferences failed,"
      " but the error is still there");

  fail_unless (local_codecs_notified == TRUE, "Not notified of codec changed");
  local_codecs_notified = FALSE;

  g_object_get (dat->session, "codec-preferences", &codecs2, NULL);

  fail_unless (g_list_length (codecs2) == 2,
      "Returned list from codec-preferences is wrong length");

  fail_unless (fs_codec_are_equal (codecs->data, codecs2->data),
      "codec-preferences first element wrong");
  fail_unless (fs_codec_are_equal (codecs->next->data, codecs2->next->data),
      "codec-preferences second element wrong");

  fs_codec_list_destroy (codecs);
  fs_codec_list_destroy (codecs2);

  g_object_get (dat->session, "codecs", &codecs, NULL);

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

  fail_unless (fs_session_set_codec_preferences (dat->session, NULL, &error),
      "Could not set codec-preferences");
  fail_if (error, "Error set while function succeeded?");
  fail_unless (local_codecs_notified, "We were not notified of the change"
      " in codecs");

  g_object_get (dat->session, "codecs", &codecs, NULL);

  fail_unless (fs_codec_list_are_equal (codecs, orig_codecs),
      "Resetting codec-preferences failed, codec lists are not equal");

  fs_codec_list_destroy (orig_codecs);

  for (item = codecs;
       item;
       item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    codec->id = FS_CODEC_ID_DISABLE;
  }

  codecs = g_list_prepend (codecs, fs_codec_new (116, "reserve-pt",
          FS_MEDIA_TYPE_AUDIO, 0));

  fail_if (fs_session_set_codec_preferences (dat->session, codecs,
          &error),
      "Disabling all codecs did not fail");
  fail_unless (error != NULL, "The error is not set");
  fail_unless (error->domain == FS_ERROR,
      "Domain is not FS_ERROR");
  fail_unless (error->code == FS_ERROR_NO_CODECS_LEFT,
      "The error code is %d, not FS_ERROR_NO_CODECS_LEFT");

  g_clear_error (&error);

  fs_codec_list_destroy (codecs);

  cleanup_simple_conference (dat);
}
GST_END_TEST;

static gboolean session_codecs_notified = FALSE;
static gboolean stream_remote_codecs_notified = FALSE;
static gboolean stream_nego_codecs_notified = FALSE;

static void
_codecs_notify (GObject *object, GParamSpec *paramspec,
    gpointer user_data)
{
  gboolean *notified_marker = user_data;
  *notified_marker = TRUE;
}


GST_START_TEST (test_rtpcodecs_two_way_negotiation)
{
  struct SimpleTestConference *dat = NULL;
  struct SimpleTestStream *st = NULL;
  GList *codecs = NULL, *codecs2 = NULL;
  GError *error = NULL;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");
  st = simple_conference_add_stream (dat, dat, "rawudp", 0, NULL);

  session_codecs_notified = FALSE;
  stream_remote_codecs_notified = FALSE;
  stream_nego_codecs_notified = FALSE;

  g_signal_connect (dat->session, "notify::codecs",
      G_CALLBACK (_codecs_notify), &session_codecs_notified);
  g_signal_connect (st->stream, "notify::remote-codecs",
      G_CALLBACK (_codecs_notify), &stream_remote_codecs_notified);
  g_signal_connect (st->stream, "notify::negotiated-codecs",
      G_CALLBACK (_codecs_notify), &stream_nego_codecs_notified);

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

  fail_if (session_codecs_notified);
  fail_if (stream_remote_codecs_notified);
  fail_if (stream_nego_codecs_notified);

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

  fail_unless (session_codecs_notified);
  fail_unless (stream_remote_codecs_notified);
  fail_unless (stream_nego_codecs_notified);

  g_object_get (dat->session, "codecs", &codecs2, NULL);
  fail_unless (g_list_length (codecs2) == 1, "Too many negotiated codecs");
  fail_unless (fs_codec_are_equal (codecs->data, codecs2->data),
      "Negotiated codec does not match remote codec");
  fs_codec_list_destroy (codecs2);

  session_codecs_notified = FALSE;
  stream_remote_codecs_notified = FALSE;
  stream_nego_codecs_notified = FALSE;

  fail_unless (fs_stream_set_remote_codecs (st->stream, codecs, &error),
      "Could not re-set remote PCMU codec");

  fail_if (session_codecs_notified);
  fail_if (stream_nego_codecs_notified);
  fail_if (stream_remote_codecs_notified);

  fs_codec_list_destroy (codecs);
  codecs = NULL;

  session_codecs_notified = FALSE;
  stream_remote_codecs_notified = FALSE;
  stream_nego_codecs_notified = FALSE;

  codecs = g_list_append (codecs,
      fs_codec_new (
          118,
          "PCMU",
          FS_MEDIA_TYPE_AUDIO,
          8000));

  fail_unless (fs_stream_set_remote_codecs (st->stream, codecs, &error),
      "Could not set remote PCMU codec with Pt 118");

  fail_unless (session_codecs_notified);
  fail_unless (stream_nego_codecs_notified);
  fail_unless (stream_remote_codecs_notified);

  g_object_get (dat->session, "codecs", &codecs2, NULL);
  fail_unless (g_list_length (codecs2) == 1, "Too many negotiated codecs");
  fail_unless (fs_codec_are_equal (codecs->data, codecs2->data),
      "Negotiated codec does not match remote codec");
  fs_codec_list_destroy (codecs2);
  codecs2 = NULL;

  session_codecs_notified = FALSE;
  stream_remote_codecs_notified = FALSE;
  stream_nego_codecs_notified = FALSE;

  fail_unless (fs_stream_set_remote_codecs (st->stream, codecs, &error),
      "Could not re-set remote PCMU codec");

  fail_if (session_codecs_notified);
  fail_if (stream_remote_codecs_notified);
  fail_if (stream_nego_codecs_notified);

  fs_codec_list_destroy (codecs);
  codecs = NULL;

  codecs = g_list_append (NULL,
      fs_codec_new (
          0,
          "PCMU",
          FS_MEDIA_TYPE_AUDIO,
          0));

  session_codecs_notified = FALSE;
  stream_remote_codecs_notified = FALSE;
  stream_nego_codecs_notified = FALSE;
  fail_unless (fs_stream_set_remote_codecs (st->stream, codecs, &error),
      "Could not set remote PCMU codec with clock rate 0");
  g_clear_error (&error);

  fail_unless (session_codecs_notified);
  fail_unless (stream_remote_codecs_notified);
  fail_unless (stream_nego_codecs_notified);

  ((FsCodec*)codecs->data)->clock_rate = 8000;

  g_object_get (dat->session, "codecs", &codecs2, NULL);
  fail_unless (g_list_length (codecs2) == 1, "Too many negotiated codecs");
  fail_unless (fs_codec_are_equal (codecs->data, codecs2->data),
      "Negotiated codec does not match remote codec");
  fs_codec_list_destroy (codecs2);

  fs_codec_list_destroy (codecs);
  codecs = NULL;

  codecs = g_list_append (codecs,
      fs_codec_new (
          0,
          "PCMU",
          FS_MEDIA_TYPE_AUDIO,
          0));

  fail_unless (fs_stream_set_remote_codecs (st->stream, codecs, &error),
      "Could not set remote PCMU codec with unknown clock-rate");

  g_object_get (dat->session, "codecs", &codecs2, NULL);
  fail_unless (g_list_length (codecs2) == 1, "Too many negotiated codecs");
  ((FsCodec*)(codecs->data))->clock_rate = 8000;
  fail_unless (fs_codec_are_equal (codecs->data, codecs2->data),
      "Negotiated codec does not match remote codec");
  fs_codec_list_destroy (codecs2);
  codecs2 = NULL;

  fs_codec_list_destroy (codecs);
  codecs = NULL;

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
  st = simple_conference_add_stream (dat, dat, "rawudp", 0, NULL);

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

  g_object_get (dat->session, "codecs", &codecs, NULL);
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
    GST_WARNING ("Could not find a dynamically allocated codec,"
        " skipping testing of the payload-type reservation mecanism");
    goto out;
  }

  codec_prefs = g_list_prepend (NULL, fs_codec_new (id, "reserve-pt",
                                               FS_MEDIA_TYPE_AUDIO, 0));

  fail_unless (fs_session_set_codec_preferences (dat->session, codec_prefs,
          NULL), "Could not set codec preferences");

  g_object_get (dat->session, "codecs", &codecs, NULL);
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

  g_object_get (dat->session, "codecs", &codecs, NULL);

  fail_unless (fs_stream_set_remote_codecs (s, codecs, NULL),
               "Could not set local codecs as remote codecs");

  fs_codec_list_destroy (codecs);

  g_object_get (dat->session, "codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == id)
      break;
  }
  fs_codec_list_destroy (codecs);

  fail_if (item == NULL, "There is no pt %u in the negotiated codecs, "
      "but there was one in the local codecs", id);

  fail_unless (fs_session_set_codec_preferences (dat->session, codec_prefs,
          NULL), "Could not set codec preferences after set_remote_codecs");

  g_object_get (dat->session, "codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == id)
      break;
  }
  fail_if (item == NULL, "Codec preference was not overriden by remote codecs,"
      " could not find codec with id %d", id);
  fs_codec_list_destroy (codecs);


  fail_unless (fs_session_set_codec_preferences (dat->session, codec_prefs,
          NULL), "Could not re-set codec-preferences after set_remote_codecs");

  g_object_get (dat->session, "codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (codec->id == id)
      break;
  }
  fail_if (item == NULL, "Codec preference was not overriden by remote codecs,"
     " could not find codec with id %d", id);
  fs_codec_list_destroy (codecs);

  fs_codec_list_destroy (codec_prefs);

  g_object_unref (s);
 out:
  cleanup_simple_conference (dat);
}
GST_END_TEST;

static FsCodec *
check_vorbis_and_configuration (const gchar *text, GList *codecs,
    const gchar *config)
{
  GList *item = NULL;
  FsCodec *codec = NULL;

  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    codec = item->data;
    if (!g_ascii_strcasecmp ("vorbis", codec->encoding_name))
      break;
  }

  fail_if (item == NULL, "%s: Could not find Vorbis", text);

  for (item = codec->optional_params; item; item = g_list_next (item))
  {
    FsCodecParameter *param = item->data;

    if (!g_ascii_strcasecmp (param->name, "configuration"))
    {
      if (config)
        fail_if (strcmp (param->value, config),
            "%s: The value of the configuration param is not"
            " what it was set to", text);
      break;
    }
  }

  fail_if (item == NULL, "%s: The configuration parameter is not there", text);

  return codec;
}


struct ConfigDataTest {
  struct SimpleTestConference *dat;
  FsParticipant *participant;
  FsStream *stream;
  const gchar *config;
};

static void
_bus_message_element (GstBus *bus, GstMessage *message,
    struct ConfigDataTest *cd)
{
  GList *codecs = NULL;
  GList *codecs2 = NULL;
  FsCodec *codec = NULL;
  GList *item1, *item2;
  gboolean ready;
  const GstStructure *s = gst_message_get_structure (message);
  FsParticipant *p2 = NULL;
  FsStream *stream2 = NULL;
  const gchar config[] = "asildksahkjewafrefenbwqgiufewaiufhwqiu"
    "enfiuewfkdnwqiucnwiufenciuawndiunfucnweciuqfiucina";
  const gchar config2[] = "sadsajdsakdjlksajdsajldsaldjsalkjdl";
  GError *error = NULL;
  gchar *discovered_config = NULL;
  FsCodecParameter *param;
  guint vorbis_id;

  if (!gst_structure_has_name (s, "farsight-codecs-changed"))
    return;

  g_object_get (cd->dat->session, "codecs-ready", &ready, NULL);

  if (!ready)
    return;

  g_object_get (cd->dat->session, "codecs", &codecs, NULL);
  codec = check_vorbis_and_configuration ("codecs before negotiation", codecs,
      NULL);
  vorbis_id = codec->id;

  param = fs_codec_get_optional_parameter (codec, "configuration", NULL);
  discovered_config = g_strdup (param->value);

  g_object_get (cd->dat->session, "codecs-without-config", &codecs2, NULL);
  fail_if (codecs2 == NULL, "Could not get codecs without config");
  for (item1 = codecs, item2 = codecs2;
       item1 && item2;
       item1 = g_list_next (item1), item2 = g_list_next (item2))
  {
    FsCodec *codec1 = item1->data;
    FsCodec *codec2 = item2->data;

    if (fs_codec_are_equal (codec1, codec2))
      continue;

    fail_unless (codec1->id == codec2->id &&
        !strcmp (codec1->encoding_name, codec2->encoding_name) &&
        codec1->media_type == codec2->media_type &&
        codec1->clock_rate == codec2->clock_rate &&
        codec1->channels == codec2->channels, "Codec from codec with and "
        "without are not equal outside of their optional params");

    fail_if (fs_codec_get_optional_parameter (codec2, "configuration", NULL),
        "Found the configuration inside a codec without config");
  }

  fail_unless (item1 == NULL && item2 == NULL, "Codecs with config and without"
      " config are not the same length");

  fs_codec_list_destroy (codecs2);
  fs_codec_list_destroy (codecs);

  if (cd->config)
  {
    g_object_get (cd->stream, "negotiated-codecs", &codecs, NULL);
    check_vorbis_and_configuration ("stream codecs before negotiation",
        codecs, cd->config);
    fs_codec_list_destroy (codecs);
  }


  codec = fs_codec_new (vorbis_id,  "VORBIS", FS_MEDIA_TYPE_AUDIO, 44100);
  fs_codec_add_optional_parameter (codec, "configuration", config);
  codecs = g_list_prepend (NULL, codec);

  if (!fs_stream_set_remote_codecs (cd->stream, codecs, &error))
  {
    if (error)
      fail ("Could not set vorbis as remote codec on the stream: %s",
          error->message);
    else
      fail ("Could not set vorbis as remote codec on the stream"
          " WITHOUT SETTING THE GError");
  }

  fs_codec_list_destroy (codecs);


  g_object_get (cd->dat->session, "codecs-ready", &ready, NULL);
  fail_unless (ready, "Codecs became unready after setting new remote codecs");

  g_object_get (cd->dat->session, "codecs", &codecs, NULL);
  check_vorbis_and_configuration ("session codecs after negotiation",
      codecs, discovered_config);
  fs_codec_list_destroy (codecs);

  g_object_get (cd->stream, "negotiated-codecs", &codecs, NULL);
  check_vorbis_and_configuration ("stream codecs after negotiation",
      codecs, config);
  fs_codec_list_destroy (codecs);

  p2 = fs_conference_new_participant (FS_CONFERENCE (cd->dat->conference),
      "name2", &error);
  if (!p2)
    fail ("Could not add second participant to conference %s", error->message);

  stream2 = fs_session_new_stream (cd->dat->session, p2, FS_DIRECTION_BOTH,
      "rawudp", 0, NULL, NULL);

  fail_if (stream2 == NULL, "Could not second create new stream");


  codec = fs_codec_new (vorbis_id, "VORBIS", FS_MEDIA_TYPE_AUDIO, 44100);
  fs_codec_add_optional_parameter (codec, "configuration", config2);
  codecs = g_list_prepend (NULL, codec);

  if (!fs_stream_set_remote_codecs (stream2, codecs, &error))
  {
    if (error)
      fail ("Could not set vorbis as remote codec on the stream: %s",
          error->message);
    else
      fail ("Could not set vorbis as remote codec on the stream"
          " WITHOUT SETTING THE GError");
  }
  fs_codec_list_destroy (codecs);



  g_object_get (cd->dat->session, "codecs-ready", &ready, NULL);
  fail_unless (ready, "Codecs became unready after setting new remote codecs");

  g_object_get (cd->dat->session, "codecs", &codecs, NULL);
  check_vorbis_and_configuration ("session codecs after renegotiation",
      codecs, discovered_config);
  fs_codec_list_destroy (codecs);

  g_object_get (cd->stream, "negotiated-codecs", &codecs, NULL);
  check_vorbis_and_configuration ("stream codecs after renegotiation",
      codecs, config);
  fs_codec_list_destroy (codecs);

  g_object_get (stream2, "negotiated-codecs", &codecs, NULL);
  check_vorbis_and_configuration ("stream2 codecs after renegotiation",
      codecs, config2);
  fs_codec_list_destroy (codecs);


  g_object_unref (p2);
  g_object_unref (stream2);

  g_free (discovered_config);

  g_main_loop_quit (loop);
}

static void
run_test_rtpcodecs_config_data (gboolean preset_remotes)
{
  struct ConfigDataTest cd;
  GList *codecs = NULL, *item = NULL;
  gboolean ready;
  GError *error = NULL;
  GstBus *bus = NULL;
  const gchar config[] = "lksajdoiwqjfd2ohqfpiuwqjofqiufhqfqw";

  memset (&cd, 0, sizeof(cd));

  loop = g_main_loop_new (NULL, FALSE);

  cd.dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");


  cd.participant = fs_conference_new_participant (
      FS_CONFERENCE (cd.dat->conference), "name", NULL);

  fail_if (cd.participant == NULL, "Could not add participant to conference");

  cd.stream = fs_session_new_stream (cd.dat->session, cd.participant,
      FS_DIRECTION_BOTH, "rawudp", 0, NULL, NULL);

  fail_if (cd.stream == NULL, "Could not create new stream");


  codecs = g_list_prepend (NULL, fs_codec_new (FS_CODEC_ID_ANY, "VORBIS",
          FS_MEDIA_TYPE_AUDIO, 44100));

  fail_unless (fs_session_set_codec_preferences (cd.dat->session, codecs,
          &error),
      "Unable to set codec preferences: %s",
      error ? error->message : "UNKNOWN");

  fs_codec_list_destroy (codecs);

  g_object_get (cd.dat->session, "codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;
    if (!g_ascii_strcasecmp ("vorbis", codec->encoding_name))
      break;

  }
  fs_codec_list_destroy (codecs);

  if (!item)
  {
    GST_WARNING ("Could not find Vorbis encoder/decoder/payloader/depayloaders,"
        " so we are skipping the config-data test");
    goto out;
  }


  g_object_get (cd.dat->session, "codecs-ready", &ready, NULL);

  fail_if (ready, "Codecs are ready before the pipeline is playing, it does not"
      " try to detect vorbis codec data");


  if (preset_remotes)
  {
    FsCodec *codec = NULL;

    cd.config = config;
    codec = fs_codec_new (105, "VORBIS", FS_MEDIA_TYPE_AUDIO, 44100);
    fs_codec_add_optional_parameter (codec, "configuration", config);
    codecs = g_list_prepend (NULL, codec);

    if (!fs_stream_set_remote_codecs (cd.stream, codecs, &error))
    {
      if (error)
        fail ("Could not set vorbis as remote codec on the stream: %s",
            error->message);
      else
        fail ("Could not set vorbis as remote codec on the stream"
            " WITHOUT SETTING THE GError");
    }

    fs_codec_list_destroy (codecs);
  }


  g_object_get (cd.dat->session, "codecs-ready", &ready, NULL);

  fail_if (ready, "Codecs are ready before the pipeline is playing, it does not"
      " try to detect vorbis codec data");

  setup_fakesrc (cd.dat);

  bus = gst_pipeline_get_bus (GST_PIPELINE (cd.dat->pipeline));

  gst_bus_add_signal_watch (bus);

  g_signal_connect (bus, "message::element", G_CALLBACK (_bus_message_element),
      &cd);

  fail_if (gst_element_set_state (cd.dat->pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  g_main_loop_run (loop);

  gst_bus_remove_signal_watch (bus);

  gst_object_unref (bus);

  fail_if (gst_element_set_state (cd.dat->pipeline, GST_STATE_NULL) ==
      GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to null");

 out:
  g_main_loop_unref (loop);

  g_object_unref (cd.participant);
  g_object_unref (cd.stream);

  cleanup_simple_conference (cd.dat);
}

GST_START_TEST (test_rtpcodecs_config_data)
{
  run_test_rtpcodecs_config_data (FALSE);
}
GST_END_TEST;

GST_START_TEST (test_rtpcodecs_preset_config_data)
{
  run_test_rtpcodecs_config_data (TRUE);
}
GST_END_TEST;


static void
profile_test (const gchar *send_profile, const gchar *recv_profile,
    gboolean is_valid)
{
  GstElement *conf;
  FsSession *session;
  GList *codecs;
  FsCodec *base_codec = fs_codec_new (120, "PCMA", FS_MEDIA_TYPE_AUDIO,
      8000);
  FsCodec *pref_codec = fs_codec_copy (base_codec);
  GList *prefs = g_list_append (NULL, pref_codec);
  GList *item;

  if (send_profile)
    fs_codec_add_optional_parameter (pref_codec, "farsight-send-profile",
        send_profile);
  if (recv_profile)
    fs_codec_add_optional_parameter (pref_codec, "farsight-recv-profile",
        recv_profile);

  conf = gst_element_factory_make ("fsrtpconference", NULL);
  fail_if (conf == NULL, "Could not make fsrtpconference");

  session = fs_conference_new_session (FS_CONFERENCE (conf),
      FS_MEDIA_TYPE_AUDIO, NULL);
  fail_if (session == NULL, "Could not make new session");

  fail_unless (fs_session_set_codec_preferences (session, prefs, NULL),
      "Could not set codec preferences");

  g_object_get (session, "codecs", &codecs, NULL);

  for (item = codecs; item; item = g_list_next (item))
    if (fs_codec_are_equal ((FsCodec *)item->data, base_codec))
      break;

  if (is_valid)
    fail_if (item == NULL,
        "Codec profile should be valid, but fails (%s) (%s)",
        send_profile, recv_profile);
  else
    fail_if (item != NULL,
        "Codec profile should be invalid, but succeeds (%s) (%s)",
        send_profile, recv_profile);

  fs_codec_list_destroy (codecs);

  g_object_unref (session);
  gst_object_unref (conf);

  fs_codec_list_destroy (prefs);
  fs_codec_destroy (base_codec);
}

GST_START_TEST (test_rtpcodecs_profile)
{
  /* basic */
  profile_test (
      "audioconvert ! audioresample ! audioconvert ! alawenc ! rtppcmapay",
      "rtppcmadepay ! alawdec",
      TRUE);

  /* double send src */
  profile_test (
      "audioconvert ! audioresample ! audioconvert ! tee name=t ! alawenc ! rtppcmapay t. ! alawenc ! rtppcmapay",
      "rtppcmadepay ! alawdec",
      TRUE);

  /* double recv src */
  profile_test (
      "audioconvert ! audioresample ! audioconvert ! alawenc ! rtppcmapay",
      "rtppcmadepay ! alawdec ! tee name=t ! identity t. ! identity ",
      FALSE);

  /* no sink */
  profile_test (
      "audioconvert ! audioresample ! audioconvert ! alawenc ! rtppcmapay",
      "rtppcmadepay ! alawdec ! fakesink",
      FALSE);

  /* no src */
  profile_test (
      "audiotestsrc ! audioconvert ! audioresample ! audioconvert ! alawenc ! rtppcmapay",
      "rtppcmadepay ! alawdec",
      FALSE);

  /* double send sink */
  profile_test (
      "adder name=a ! audioconvert ! audioresample ! audioconvert ! alawenc ! rtppcmapay identity ! a. identity !a.",
      "rtppcmadepay ! alawdec",
      FALSE);

  /* double recv pipeline */
  profile_test (
      "audioconvert ! audioresample ! audioconvert ! alawenc ! rtppcmapay",
      "rtppcmadepay ! alawdec rtppcmadepay ! identity",
      FALSE);

  /* sendonly profile */
  profile_test (
      "audioconvert ! audioresample ! audioconvert ! alawenc ! rtppcmapay",
      NULL,
      FALSE);

  /* recvonly profile */
  profile_test (
      NULL,
      "rtppcmadepay ! alawdec",
      TRUE);
}
GST_END_TEST;


GST_START_TEST (test_rtpcodecs_dynamic_pt)
{
  struct SimpleTestConference *dat = NULL;
  GList *codecs = NULL, *item = NULL;
  GList *codec_prefs = NULL;
  FsCodec *codec1 = NULL, *codec2 = NULL;
  FsCodec *tmpcodec;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");

  g_object_get (dat->session, "codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *codec = item->data;

    if (codec->id >= 96)
    {
      if (!codec1)
      {
        codec1 = fs_codec_copy (codec);
      }
      else
      {
        codec2 = fs_codec_copy (codec);
        break;
      }
    }
  }
  fs_codec_list_destroy (codecs);

  if (!codec1 || !codec2)
  {
    GST_WARNING ("Could not find two dynamically allocated codec,"
        "skipping testing of the payload-type dynamic number preferences");
    goto out;
  }

  tmpcodec = fs_codec_copy (codec2);
  tmpcodec->id = codec1->id;

  codec_prefs = g_list_prepend (NULL, tmpcodec);

  fail_unless (fs_session_set_codec_preferences (dat->session, codec_prefs,
          NULL), "Could not set codec preferences");

  g_object_get (dat->session, "codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    if (fs_codec_are_equal (item->data, tmpcodec))
      break;
  }
  fs_codec_list_destroy (codecs);

  fs_codec_list_destroy (codec_prefs);

  fail_if (item == NULL, "Could not force codec id");

 out:
  fs_codec_destroy (codec1);
  fs_codec_destroy (codec2);
  cleanup_simple_conference (dat);

}
GST_END_TEST;

GST_START_TEST (test_rtpcodecs_ptime)
{
  struct SimpleTestConference *dat = NULL;
  GList *codecs = NULL, *item = NULL;
  FsCodec *codec = NULL;
  FsCodec *prefcodec = NULL;
  GError *error = NULL;
  FsParticipant *participant;
  FsStream *stream;
  GstBus *bus;
  GstMessage *message;

  dat = setup_simple_conference (1, "fsrtpconference", "bob@127.0.0.1");

  g_object_get (dat->session, "codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *tmpcodec = item->data;

    if (tmpcodec->id == 0 || tmpcodec->id == 8)
    {
      if (!prefcodec)
      {
        prefcodec = fs_codec_copy (tmpcodec);
        break;
      }
    }
  }
  fs_codec_list_destroy (codecs);

  fail_unless (
      fs_codec_get_optional_parameter (prefcodec, "ptime", NULL) == NULL);
  fail_unless (
      fs_codec_get_optional_parameter (prefcodec, "maxptime", NULL) == NULL);

  codec = fs_codec_copy (prefcodec);
  fs_codec_add_optional_parameter (codec, "ptime", "10");
  fs_codec_add_optional_parameter (codec, "maxptime", "20");
  codecs = g_list_append (NULL, codec);
  fail_unless (fs_session_set_codec_preferences (dat->session, codecs, &error));
  fail_unless (error == NULL);
  fs_codec_list_destroy (codecs);
  codecs = NULL;

  g_object_get (dat->session, "current-send-codec", &codec, NULL);
  fail_unless (codec == NULL);

  g_object_get (dat->session, "codecs", &codecs, NULL);
  codec = codecs->data;
  fail_unless (codec->id == prefcodec->id);
  fail_unless (
      fs_codec_get_optional_parameter (codec, "ptime", "10") != NULL);
  fail_unless (
      fs_codec_get_optional_parameter (codec, "maxptime", "20") != NULL);
  fs_codec_list_destroy (codecs);

  participant = fs_conference_new_participant (
      FS_CONFERENCE (dat->conference), "name", NULL);
  fail_if (participant == NULL, "Could not add participant to conference");

  stream = fs_session_new_stream (dat->session, participant,
      FS_DIRECTION_BOTH, "rawudp", 0, NULL, NULL);
  fail_if (stream == NULL, "Could not add stream to session");

  codecs = g_list_append (NULL, fs_codec_copy (prefcodec));
  fail_unless (fs_stream_set_remote_codecs (stream, codecs, &error));
  fail_unless (error == NULL);

  g_object_get (dat->session, "codecs", &codecs, NULL);
  fail_unless (g_list_length (codecs) == 1);
  codec = codecs->data;
  fail_unless (codec->id == prefcodec->id);
  fail_unless (
      fs_codec_get_optional_parameter (codec, "ptime", "10") != NULL);
  fail_unless (
      fs_codec_get_optional_parameter (codec, "maxptime", "20") != NULL);
  fs_codec_list_destroy (codecs);

  fail_if (gst_element_set_state (dat->pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);
  dat->started = TRUE;

  setup_fakesrc (dat);

  bus = gst_pipeline_get_bus (GST_PIPELINE (dat->pipeline));

  codec = NULL;
  while ((message = gst_bus_poll (bus, GST_MESSAGE_ELEMENT, 3 * GST_SECOND)))
  {
    const GstStructure *s = gst_message_get_structure (message);

    if (gst_structure_has_name (s, "farsight-send-codec-changed"))
    {
      const GValue *val;
      val = gst_structure_get_value (s, "codec");
      codec = g_value_get_boxed (val);
      fail_unless (
          fs_codec_get_optional_parameter (prefcodec, "ptime", NULL) == NULL);
      fail_unless (
          fs_codec_get_optional_parameter (prefcodec, "maxptime", NULL) == NULL);
      gst_message_unref (message);
      break;
    }
    gst_message_unref (message);
  }
  g_assert (codec != NULL);

  codec = fs_codec_copy (prefcodec);
  fs_codec_add_optional_parameter (codec, "ptime", "30");
  fs_codec_add_optional_parameter (codec, "maxptime", "40");
  codecs = g_list_append (NULL, codec);
  fail_unless (fs_stream_set_remote_codecs (stream, codecs, &error));
  fail_unless (error == NULL);

  fail_if (gst_element_set_state (dat->pipeline, GST_STATE_NULL) !=
      GST_STATE_CHANGE_SUCCESS);

  g_object_unref (stream);
  g_object_unref (participant);
  cleanup_simple_conference (dat);
}
GST_END_TEST;

static void
setup_codec_tests (struct SimpleTestConference **dat,
    FsParticipant **participant, FsMediaType mediatype)
{
  *dat = setup_simple_conference_full (1, "fsrtpconference", "bob@127.0.0.1",
      mediatype);

  *participant = fs_conference_new_participant (
      FS_CONFERENCE ((*dat)->conference), "name", NULL);
  fail_if (participant == NULL, "Could not add participant to conference");
}

static void
cleanup_codec_tests (struct SimpleTestConference *dat,
    FsParticipant *participant)
{
  g_object_unref (participant);
  cleanup_simple_conference (dat);
}

static void
test_one_telephone_event_codec (FsSession *session, FsStream *stream,
    FsCodec *prefcodec, FsCodec *incodec, FsCodec *outcodec)
{
  GList *codecs = NULL;
  FsCodec *codec = NULL;
  GError *error = NULL;

  codecs = g_list_append (NULL, fs_codec_copy (prefcodec));
  codecs = g_list_append (codecs, incodec);
  fail_unless (fs_stream_set_remote_codecs (stream, codecs, &error));
  fail_unless (error == NULL);
  fs_codec_list_destroy (codecs);

  g_object_get (session, "codecs", &codecs, NULL);
  if (outcodec)
  {
    fail_unless (g_list_length (codecs) == 2);
    codec = codecs->data;
    fail_unless (codec->id == prefcodec->id);
    codec = codecs->next->data;
    fail_unless (fs_codec_are_equal (codec, outcodec));
    fs_codec_destroy (outcodec);
  }
  else
  {
    fail_unless (g_list_length (codecs) == 1);
  }

  fs_codec_list_destroy (codecs);
}

GST_START_TEST (test_rtpcodecs_telephone_event_nego)
{
  struct SimpleTestConference *dat = NULL;
  GList *codecs = NULL, *item = NULL;
  FsCodec *codec = NULL;
  FsCodec *outcodec = NULL;
  FsCodec *prefcodec = NULL;
  FsParticipant *participant;
  FsStream *stream;
  gboolean has_telephone_event_codec = FALSE;

  setup_codec_tests (&dat, &participant, FS_MEDIA_TYPE_AUDIO);
  stream = fs_session_new_stream (dat->session, participant,
      FS_DIRECTION_BOTH, "rawudp", 0, NULL, NULL);
  fail_if (stream == NULL, "Could not add stream to session");

  g_object_get (dat->session, "codecs", &codecs, NULL);
  for (item = g_list_first (codecs); item; item = g_list_next (item))
  {
    FsCodec *tmpcodec = item->data;

    if (tmpcodec->id == 0 || tmpcodec->id == 8)
    {
      if (!prefcodec)
      {
        prefcodec = fs_codec_copy (tmpcodec);
      }
    } else if (!strcmp (tmpcodec->encoding_name, "telephone-event")) {
      fail_unless (
          fs_codec_get_optional_parameter (tmpcodec, "events", "0-15") != NULL);
      has_telephone_event_codec = TRUE;
    }
  }
  fs_codec_list_destroy (codecs);

  if (!has_telephone_event_codec) {
    g_debug ("telephone-event elements not detected, skipping test");
    return;
  }

  codec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "events", "0-15");
  outcodec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "events", "0-15");
  test_one_telephone_event_codec (dat->session, stream, prefcodec, codec,
      outcodec);

  codec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "events", "0,2-15");
  outcodec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "events", "0,2-15");
  test_one_telephone_event_codec (dat->session, stream, prefcodec, codec,
      outcodec);

  codec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "events", "0,2-15");
  outcodec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "events", "0,2-15");
  test_one_telephone_event_codec (dat->session, stream, prefcodec, codec,
      outcodec);

  codec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "events", "2");
  outcodec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "events", "2");
  test_one_telephone_event_codec (dat->session, stream, prefcodec, codec,
      outcodec);

  codec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "events", "2-3");
  outcodec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "events", "2-3");
  test_one_telephone_event_codec (dat->session, stream, prefcodec, codec,
      outcodec);

  codec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "events", "0,10-26,32");
  outcodec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "events", "0,10-15");
  test_one_telephone_event_codec (dat->session, stream, prefcodec, codec,
      outcodec);

  codec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "events", "0,10");
  outcodec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "events", "0,10");
  test_one_telephone_event_codec (dat->session, stream, prefcodec, codec,
      outcodec);

  codec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "events", "0,2-15-2");
  test_one_telephone_event_codec (dat->session, stream, prefcodec, codec,
      NULL);

  codec = fs_codec_new (100, "telephone-event", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "events", "0,,3");
  test_one_telephone_event_codec (dat->session, stream, prefcodec, codec,
      NULL);

  fs_codec_destroy (prefcodec);
  g_object_unref (stream);
  cleanup_codec_tests (dat, participant);
}
GST_END_TEST;

#define test_one_codec(session, part, prefcodec, outprefcodec, incodec, \
    outcodec)                                                           \
  test_one_codec_internal (G_STRLOC, session, part, prefcodec,        \
      outprefcodec, incodec, outcodec)

static void
test_one_codec_internal (const gchar *addr,
    FsSession *session, FsParticipant *participant,
    FsCodec *prefcodec, FsCodec *outprefcodec,
    FsCodec *incodec, FsCodec *outcodec)
{
  GList *codecs = NULL;
  FsCodec *codec = NULL;
  GError *error = NULL;
  FsStream *stream;

  stream = fs_session_new_stream (session, participant,
      FS_DIRECTION_BOTH, "rawudp", 0, NULL, NULL);
  fail_if (stream == NULL, "Could not add stream to session");

  codecs = g_list_append (NULL, prefcodec);
  fail_unless (fs_session_set_codec_preferences (session, codecs, &error),
      "%s: Could not set codec preferences", addr);
  fail_unless (error == NULL, "%s: Non-NULL error from codec prefs", addr);
  g_list_free (codecs);

  if (outprefcodec)
  {
    FsCodec *copy;

    g_object_get (session, "codecs", &codecs, NULL);
    codec = codecs->data;
    copy = fs_codec_copy (outprefcodec);
    copy->id = codec->id;
    fail_unless (fs_codec_are_equal (codec, copy),
        "%s: Codec prefs didn't give expected results: " FS_CODEC_FORMAT
        " (expected: " FS_CODEC_FORMAT ")", addr, FS_CODEC_ARGS (codec),
        FS_CODEC_ARGS (copy));
    fs_codec_destroy (copy);
    fs_codec_list_destroy (codecs);
  }

  codecs = g_list_append (NULL, incodec);
  if (outcodec)
  {
    fail_unless (fs_stream_set_remote_codecs (stream, codecs, &error),
        "%s: Could not set remote codecs", addr);
    fail_unless (error == NULL, "%s: Non-NULL error from codec prefs", addr);
  }
  else
  {
    fail_if (fs_stream_set_remote_codecs (stream, codecs, &error),
        "%s: Could set unacceptable remote codecs", addr);
    fail_unless (error != NULL,
        "%s: Unacceptable remote codecs didnt give out a GError", addr);
    g_clear_error (&error);
  }
  fs_codec_list_destroy (codecs);

  if (outcodec)
  {
    g_object_get (session, "codecs", &codecs, NULL);
    fail_unless (g_list_length (codecs) == 1,
        "%s: Negotiation gives more than one codec", addr);
    codec = codecs->data;
    fail_unless (fs_codec_are_equal (codec, outcodec),
        "%s: Negotiation doesn't give the expected codec: " FS_CODEC_FORMAT
        " (expected: " FS_CODEC_FORMAT ")", addr, FS_CODEC_ARGS (codec),
        FS_CODEC_ARGS (outcodec));
    fs_codec_list_destroy (codecs);
    fs_codec_destroy (outcodec);
  }

  g_object_unref (stream);
}


GST_START_TEST (test_rtpcodecs_nego_ilbc)
{
  struct SimpleTestConference *dat = NULL;
  FsCodec *codec = NULL;
  FsCodec *outcodec = NULL;
  FsCodec *prefcodec = NULL;
  FsCodec *outprefcodec = NULL;
  FsParticipant *participant;

  setup_codec_tests (&dat, &participant, FS_MEDIA_TYPE_AUDIO);

  /* First we test with  mode=20 in the prefs */

  outprefcodec = fs_codec_new (FS_CODEC_ID_ANY, "ILBC", FS_MEDIA_TYPE_AUDIO,
      8000);
  fs_codec_add_optional_parameter (outprefcodec, "mode", "20");

  prefcodec = fs_codec_copy (outprefcodec);
  fs_codec_add_optional_parameter (prefcodec, "farsight-recv-profile",
      "rtpilbcdepay ! identity");
  fs_codec_add_optional_parameter (prefcodec, "farsight-send-profile",
      "identity ! rtpilbcpay");

  codec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  outcodec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "mode", "30");
  outcodec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "mode", "30");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "mode", "20");
  outcodec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "mode", "20");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  /* Second we test with  mode=30 in the prefs */

  fs_codec_remove_optional_parameter (prefcodec,
      fs_codec_get_optional_parameter (prefcodec, "mode", NULL));
  fs_codec_remove_optional_parameter (outprefcodec,
      fs_codec_get_optional_parameter (outprefcodec, "mode", NULL));
  fs_codec_add_optional_parameter (prefcodec, "mode", "30");
  fs_codec_add_optional_parameter (outprefcodec, "mode", "30");

  codec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  outcodec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "mode", "30");
  outcodec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "mode", "30");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "mode", "20");
  outcodec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "mode", "30");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  /* third with test with no mode in the prefs */
  fs_codec_remove_optional_parameter (prefcodec,
      fs_codec_get_optional_parameter (prefcodec, "mode", NULL));
  fs_codec_remove_optional_parameter (outprefcodec,
      fs_codec_get_optional_parameter (outprefcodec, "mode", NULL));

  codec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  outcodec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec, codec,
      outcodec);

  codec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "mode", "30");
  outcodec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec, codec,
      outcodec);

  codec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "mode", "20");
  outcodec = fs_codec_new (100, "ILBC", FS_MEDIA_TYPE_AUDIO, 8000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec, codec,
      outcodec);

  fs_codec_destroy (outprefcodec);
  fs_codec_destroy (prefcodec);
  cleanup_codec_tests (dat, participant);
}
GST_END_TEST;


GST_START_TEST (test_rtpcodecs_nego_g729)
{
  struct SimpleTestConference *dat = NULL;
  FsCodec *codec = NULL;
  FsCodec *outcodec = NULL;
  FsCodec *prefcodec = NULL;
  FsCodec *outprefcodec = NULL;
  FsParticipant *participant;

  setup_codec_tests (&dat, &participant, FS_MEDIA_TYPE_AUDIO);


  outprefcodec = fs_codec_new (FS_CODEC_ID_ANY, "G729", FS_MEDIA_TYPE_AUDIO,
      8000);

  prefcodec = fs_codec_copy (outprefcodec);
  fs_codec_add_optional_parameter (prefcodec, "farsight-recv-profile",
      "rtpg729depay ! identity");
  fs_codec_add_optional_parameter (prefcodec, "farsight-send-profile",
      "identity ! rtpg729pay");

  codec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  outcodec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  /* Lets try adding other misc params */

  codec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "p1", "v1");
  outcodec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "p1", "v1");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  fs_codec_add_optional_parameter (prefcodec, "p2", "v2");
  fs_codec_add_optional_parameter (outprefcodec, "p2", "v2");
  codec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  outcodec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "p2", "v2");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "p2", "v2-2");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, NULL);

  fs_codec_remove_optional_parameter (prefcodec,
      fs_codec_get_optional_parameter (prefcodec, "p2", NULL));
  fs_codec_remove_optional_parameter (outprefcodec,
      fs_codec_get_optional_parameter (outprefcodec, "p2", NULL));

  /* Now test annexb= */

  codec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "annexb", "yes");
  outcodec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  codec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "annexb", "no");
  outcodec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "annexb", "no");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  fs_codec_add_optional_parameter (prefcodec, "annexb", "no");
  fs_codec_add_optional_parameter (outprefcodec, "annexb", "no");


  codec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  outcodec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "annexb", "no");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "annexb", "yes");
  outcodec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "annexb", "no");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (codec, "annexb", "no");
  outcodec = fs_codec_new (18, "G729", FS_MEDIA_TYPE_AUDIO, 8000);
  fs_codec_add_optional_parameter (outcodec, "annexb", "no");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  fs_codec_destroy (outprefcodec);
  fs_codec_destroy (prefcodec);
  cleanup_codec_tests (dat, participant);
}
GST_END_TEST;


GST_START_TEST (test_rtpcodecs_nego_h261)
{
  struct SimpleTestConference *dat = NULL;
  FsCodec *codec = NULL;
  FsCodec *outcodec = NULL;
  FsCodec *prefcodec = NULL;
  FsCodec *outprefcodec = NULL;
  FsParticipant *participant;

  setup_codec_tests (&dat, &participant, FS_MEDIA_TYPE_VIDEO);

  outprefcodec = fs_codec_new (FS_CODEC_ID_ANY, "H261", FS_MEDIA_TYPE_VIDEO,
      90000);
  prefcodec = fs_codec_copy (outprefcodec);
  fs_codec_add_optional_parameter (prefcodec, "farsight-recv-profile",
      "identity");
  fs_codec_add_optional_parameter (prefcodec, "farsight-send-profile",
      "identity");

  codec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  outcodec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  codec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "cif", "3");
  fs_codec_add_optional_parameter (codec, "qcif", "2");
  fs_codec_add_optional_parameter (codec, "d", "1");
  outcodec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "qcif", "2");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  fs_codec_add_optional_parameter (prefcodec, "d", "1");
  fs_codec_add_optional_parameter (outprefcodec, "d", "1");

  codec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "cif", "3");
  fs_codec_add_optional_parameter (codec, "qcif", "2");
  fs_codec_add_optional_parameter (codec, "d", "1");
  outcodec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "qcif", "2");
  fs_codec_add_optional_parameter (outcodec, "d", "1");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  fs_codec_add_optional_parameter (prefcodec, "cif", "3");
  fs_codec_add_optional_parameter (prefcodec, "qcif", "2");
  fs_codec_add_optional_parameter (outprefcodec, "cif", "3");
  fs_codec_add_optional_parameter (outprefcodec, "qcif", "2");


  codec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  outcodec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "qcif", "2");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  codec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "cif", "4");
  fs_codec_add_optional_parameter (codec, "qcif", "1");
  outcodec = fs_codec_new (31, "H261", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "cif", "4");
  fs_codec_add_optional_parameter (outcodec, "qcif", "2");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  fs_codec_destroy (outprefcodec);
  fs_codec_destroy (prefcodec);
  cleanup_codec_tests (dat, participant);
}
GST_END_TEST;


GST_START_TEST (test_rtpcodecs_nego_h263_1998)
{
  struct SimpleTestConference *dat = NULL;
  FsCodec *codec = NULL;
  FsCodec *outcodec = NULL;
  FsCodec *prefcodec = NULL;
  FsCodec *outprefcodec = NULL;
  FsParticipant *participant;

  setup_codec_tests (&dat, &participant, FS_MEDIA_TYPE_VIDEO);

  outprefcodec = fs_codec_new (FS_CODEC_ID_ANY, "H263-1998",
      FS_MEDIA_TYPE_VIDEO, 90000);
  prefcodec = fs_codec_copy (outprefcodec);
  fs_codec_add_optional_parameter (prefcodec, "farsight-recv-profile",
      "identity");
  fs_codec_add_optional_parameter (prefcodec, "farsight-send-profile",
      "identity");

  codec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  outcodec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "sqcif", "3");
  fs_codec_add_optional_parameter (codec, "qcif", "3");
  fs_codec_add_optional_parameter (codec, "cif", "3");
  fs_codec_add_optional_parameter (codec, "cif4", "3");
  fs_codec_add_optional_parameter (codec, "cif16", "3");
  fs_codec_add_optional_parameter (codec, "custom", "3,3,4");
  outcodec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "sqcif", "3");
  fs_codec_add_optional_parameter (outcodec, "qcif", "3");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "cif16", "3");
  fs_codec_add_optional_parameter (codec, "f", "1");
  fs_codec_add_optional_parameter (codec, "i", "1");
  fs_codec_add_optional_parameter (codec, "j", "1");
  fs_codec_add_optional_parameter (codec, "k", "1");
  fs_codec_add_optional_parameter (codec, "n", "1");
  fs_codec_add_optional_parameter (codec, "p", "1,2,3");
  fs_codec_add_optional_parameter (codec, "t", "1");
  fs_codec_add_optional_parameter (codec, "bpp", "1");
  fs_codec_add_optional_parameter (codec, "hrd", "1");
  fs_codec_add_optional_parameter (codec, "interlace", "1");
  fs_codec_add_optional_parameter (codec, "cpcf", "1,2,3,4,5,6,7,8");
  fs_codec_add_optional_parameter (codec, "par", "1,2");
  outcodec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "bpp", "1");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  fs_codec_add_optional_parameter (prefcodec, "sqcif", "3");
  fs_codec_add_optional_parameter (prefcodec, "qcif", "3");
  fs_codec_add_optional_parameter (prefcodec, "cif", "3");
  fs_codec_add_optional_parameter (prefcodec, "cif4", "3");
  fs_codec_add_optional_parameter (prefcodec, "cif16", "3");
  fs_codec_add_optional_parameter (prefcodec, "custom", "3,3,4");
  fs_codec_add_optional_parameter (outprefcodec, "sqcif", "3");
  fs_codec_add_optional_parameter (outprefcodec, "qcif", "3");
  fs_codec_add_optional_parameter (outprefcodec, "cif", "3");
  fs_codec_add_optional_parameter (outprefcodec, "cif4", "3");
  fs_codec_add_optional_parameter (outprefcodec, "cif16", "3");
  fs_codec_add_optional_parameter (outprefcodec, "custom", "3,3,4");


  codec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  outcodec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "sqcif", "3");
  fs_codec_add_optional_parameter (outcodec, "qcif", "3");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "sqcif", "5");
  fs_codec_add_optional_parameter (codec, "qcif", "5");
  fs_codec_add_optional_parameter (codec, "cif4", "5");
  fs_codec_add_optional_parameter (codec, "cif16", "2");
  fs_codec_add_optional_parameter (codec, "custom", "3,3,5");
  outcodec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "sqcif", "5");
  fs_codec_add_optional_parameter (outcodec, "qcif", "5");
  fs_codec_add_optional_parameter (outcodec, "cif4", "5");
  fs_codec_add_optional_parameter (outcodec, "cif16", "3");
  fs_codec_add_optional_parameter (outcodec, "custom", "3,3,5");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  fs_codec_add_optional_parameter (prefcodec, "f", "1");
  fs_codec_add_optional_parameter (prefcodec, "i", "1");
  fs_codec_add_optional_parameter (prefcodec, "j", "1");
  fs_codec_add_optional_parameter (prefcodec, "k", "1");
  fs_codec_add_optional_parameter (prefcodec, "n", "1");
  fs_codec_add_optional_parameter (prefcodec, "p", "1,2,3");
  fs_codec_add_optional_parameter (prefcodec, "t", "1");
  fs_codec_add_optional_parameter (prefcodec, "bpp", "1");
  fs_codec_add_optional_parameter (prefcodec, "hrd", "1");
  fs_codec_add_optional_parameter (prefcodec, "interlace", "1");
  fs_codec_add_optional_parameter (prefcodec, "cpcf", "1,2,3,4,5,6,7,8");
  fs_codec_add_optional_parameter (prefcodec, "par", "1,2");

  fs_codec_add_optional_parameter (outprefcodec, "f", "1");
  fs_codec_add_optional_parameter (outprefcodec, "i", "1");
  fs_codec_add_optional_parameter (outprefcodec, "j", "1");
  fs_codec_add_optional_parameter (outprefcodec, "k", "1");
  fs_codec_add_optional_parameter (outprefcodec, "n", "1");
  fs_codec_add_optional_parameter (outprefcodec, "p", "1,2,3");
  fs_codec_add_optional_parameter (outprefcodec, "t", "1");
  fs_codec_add_optional_parameter (outprefcodec, "bpp", "1");
  fs_codec_add_optional_parameter (outprefcodec, "hrd", "1");
  fs_codec_add_optional_parameter (outprefcodec, "interlace", "1");
  fs_codec_add_optional_parameter (outprefcodec, "cpcf", "1,2,3,4,5,6,7,8");
  fs_codec_add_optional_parameter (outprefcodec, "par", "1,2");


  codec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "cif16", "4");
  fs_codec_add_optional_parameter (codec, "cif", "5");
  fs_codec_add_optional_parameter (codec, "f", "1");
  fs_codec_add_optional_parameter (codec, "i", "1");
  fs_codec_add_optional_parameter (codec, "j", "1");
  fs_codec_add_optional_parameter (codec, "k", "1");
  fs_codec_add_optional_parameter (codec, "n", "1");
  fs_codec_add_optional_parameter (codec, "p", "1,2,3");
  fs_codec_add_optional_parameter (codec, "t", "1");
  fs_codec_add_optional_parameter (codec, "bpp", "1");
  fs_codec_add_optional_parameter (codec, "hrd", "1");
  fs_codec_add_optional_parameter (codec, "interlace", "1");
  fs_codec_add_optional_parameter (codec, "cpcf", "1,2,13,14,15,16,17,18");
  fs_codec_add_optional_parameter (codec, "par", "1,2");
  outcodec = fs_codec_new (96, "H263-1998", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "cif16", "4");
  fs_codec_add_optional_parameter (outcodec, "cif", "5");
  fs_codec_add_optional_parameter (outcodec, "f", "1");
  fs_codec_add_optional_parameter (outcodec, "i", "1");
  fs_codec_add_optional_parameter (outcodec, "j", "1");
  fs_codec_add_optional_parameter (outcodec, "k", "1");
  fs_codec_add_optional_parameter (outcodec, "n", "1");
  fs_codec_add_optional_parameter (outcodec, "p", "1,2,3");
  fs_codec_add_optional_parameter (outcodec, "t", "1");
  fs_codec_add_optional_parameter (outcodec, "bpp", "1");
  fs_codec_add_optional_parameter (outcodec, "hrd", "1");
  fs_codec_add_optional_parameter (outcodec, "interlace", "1");
  fs_codec_add_optional_parameter (outcodec, "cpcf", "1,2,13,14,15,16,17,18");
  fs_codec_add_optional_parameter (outcodec, "par", "1,2");
  fs_codec_add_optional_parameter (outcodec, "sqcif", "3");
  fs_codec_add_optional_parameter (outcodec, "qcif", "3");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  fs_codec_destroy (outprefcodec);
  fs_codec_destroy (prefcodec);
  cleanup_codec_tests (dat, participant);
}
GST_END_TEST;



GST_START_TEST (test_rtpcodecs_nego_h263_2000)
{
  struct SimpleTestConference *dat = NULL;
  FsCodec *codec = NULL;
  FsCodec *outcodec = NULL;
  FsCodec *prefcodec = NULL;
  FsCodec *outprefcodec = NULL;
  FsParticipant *participant;

  setup_codec_tests (&dat, &participant, FS_MEDIA_TYPE_VIDEO);

  outprefcodec = fs_codec_new (FS_CODEC_ID_ANY, "H263-2000",
      FS_MEDIA_TYPE_VIDEO, 90000);
  prefcodec = fs_codec_copy (outprefcodec);
  fs_codec_add_optional_parameter (prefcodec, "farsight-recv-profile",
      "identity");
  fs_codec_add_optional_parameter (prefcodec, "farsight-send-profile",
      "identity");

  codec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  outcodec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "profile", "3");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, NULL);

  codec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "profile", "0");
  fs_codec_add_optional_parameter (codec, "level", "50");
  outcodec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "profile", "0");
  fs_codec_add_optional_parameter (outcodec, "level", "0");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  fs_codec_add_optional_parameter (prefcodec, "profile", "3");
  fs_codec_add_optional_parameter (prefcodec, "level", "50");
  fs_codec_add_optional_parameter (outprefcodec, "profile", "3");
  fs_codec_add_optional_parameter (outprefcodec, "level", "50");

  codec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, NULL);

  codec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "profile", "3");
  fs_codec_add_optional_parameter (codec, "level", "30");
  outcodec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "profile", "3");
  fs_codec_add_optional_parameter (outcodec, "level", "30");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  fs_codec_remove_optional_parameter (prefcodec,
      fs_codec_get_optional_parameter (prefcodec, "profile", NULL));
  fs_codec_remove_optional_parameter (outprefcodec,
      fs_codec_get_optional_parameter (outprefcodec, "profile", NULL));
  fs_codec_remove_optional_parameter (prefcodec,
      fs_codec_get_optional_parameter (prefcodec, "level", NULL));
  fs_codec_remove_optional_parameter (outprefcodec,
      fs_codec_get_optional_parameter (outprefcodec, "level", NULL));

  codec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "sqcif", "3");
  fs_codec_add_optional_parameter (codec, "qcif", "3");
  fs_codec_add_optional_parameter (codec, "cif", "3");
  fs_codec_add_optional_parameter (codec, "cif4", "3");
  fs_codec_add_optional_parameter (codec, "cif16", "3");
  fs_codec_add_optional_parameter (codec, "custom", "3,3,4");
  outcodec = fs_codec_new (96, "H263-2000", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "sqcif", "3");
  fs_codec_add_optional_parameter (outcodec, "qcif", "3");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  fs_codec_destroy (outprefcodec);
  fs_codec_destroy (prefcodec);
  cleanup_codec_tests (dat, participant);
}
GST_END_TEST;

GST_START_TEST (test_rtpcodecs_nego_h264)
{
  struct SimpleTestConference *dat = NULL;
  FsCodec *codec = NULL;
  FsCodec *outcodec = NULL;
  FsCodec *prefcodec = NULL;
  FsCodec *outprefcodec = NULL;
  FsParticipant *participant;

  setup_codec_tests (&dat, &participant, FS_MEDIA_TYPE_VIDEO);

  outprefcodec = fs_codec_new (FS_CODEC_ID_ANY, "H264",
      FS_MEDIA_TYPE_VIDEO, 90000);
  prefcodec = fs_codec_copy (outprefcodec);
  fs_codec_add_optional_parameter (prefcodec, "farsight-recv-profile",
      "identity");
  fs_codec_add_optional_parameter (prefcodec, "farsight-send-profile",
      "identity");

  codec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  outcodec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "profile-level-id",
      "42A01E");
  outcodec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "deint-buf-cap", "2");
  fs_codec_add_optional_parameter (codec, "max-rcmd-nalu-size", "2");
  outcodec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "deint-buf-cap", "2");
  fs_codec_add_optional_parameter (outcodec, "max-rcmd-nalu-size", "2");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  fs_codec_add_optional_parameter (prefcodec, "profile-level-id", "42E015");
  fs_codec_add_optional_parameter (outprefcodec, "profile-level-id", "42E015");

  codec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  outcodec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "profile-level-id", "42E015");
  outcodec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "profile-level-id", "42E015");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "profile-level-id", "42E010");
  outcodec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "profile-level-id", "42E010");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "profile-level-id", "43E010");
  outcodec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  codec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "profile-level-id", "420014");
  outcodec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "profile-level-id", "42E014");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);


  codec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (codec, "profile-level-id", "42E015");
  fs_codec_add_optional_parameter (codec, "max-mbps", "1234");
  fs_codec_add_optional_parameter (codec, "max-fs", "1234");
  fs_codec_add_optional_parameter (codec, "max-cpb", "!234");
  fs_codec_add_optional_parameter (codec, "max-dpb", "1234");
  fs_codec_add_optional_parameter (codec, "max-br", "1234");
  fs_codec_add_optional_parameter (codec, "sprop-parameter-sets", "12dsakd");
  outcodec = fs_codec_new (96, "H264", FS_MEDIA_TYPE_VIDEO, 90000);
  fs_codec_add_optional_parameter (outcodec, "profile-level-id", "42E015");
  fs_codec_add_optional_parameter (outcodec, "max-mbps", "1234");
  fs_codec_add_optional_parameter (outcodec, "max-fs", "1234");
  fs_codec_add_optional_parameter (outcodec, "max-cpb", "!234");
  fs_codec_add_optional_parameter (outcodec, "max-dpb", "1234");
  fs_codec_add_optional_parameter (outcodec, "max-br", "1234");
  fs_codec_add_optional_parameter (outcodec, "profile-level-id", "42E015");
  test_one_codec (dat->session, participant, prefcodec, outprefcodec,
      codec, outcodec);

  fs_codec_destroy (outprefcodec);
  fs_codec_destroy (prefcodec);
  cleanup_codec_tests (dat, participant);
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


  tc_chain = tcase_create ("fsrtpcodecs_codec_preferences");
  tcase_add_test (tc_chain, test_rtpcodecs_codec_preferences);
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

  tc_chain = tcase_create ("fsrtpcodecs_preset_config_data");
  tcase_add_test (tc_chain, test_rtpcodecs_preset_config_data);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_test_codec_profile");
  tcase_add_test (tc_chain, test_rtpcodecs_profile);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_dynamic_pt");
  tcase_add_test (tc_chain, test_rtpcodecs_dynamic_pt);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_ptime");
  tcase_add_test (tc_chain, test_rtpcodecs_ptime);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_telephone_event-nego");
  tcase_add_test (tc_chain, test_rtpcodecs_telephone_event_nego);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_nego_ilbc");
  tcase_add_test (tc_chain, test_rtpcodecs_nego_ilbc);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_nego_g729");
  tcase_add_test (tc_chain, test_rtpcodecs_nego_g729);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_nego_h261");
  tcase_add_test (tc_chain, test_rtpcodecs_nego_h261);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_nego_h263_1998");
  tcase_add_test (tc_chain, test_rtpcodecs_nego_h263_1998);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_nego_h263_2000");
  tcase_add_test (tc_chain, test_rtpcodecs_nego_h263_2000);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("fsrtpcodecs_nego_h264");
  tcase_add_test (tc_chain, test_rtpcodecs_nego_h264);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (fsrtpcodecs);

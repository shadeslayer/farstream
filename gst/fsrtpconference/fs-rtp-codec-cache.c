/*
 * Farsight2 - Farsight RTP Discovered Codecs cache
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-codec-cache.c - A Farsight RTP Codec Caching gobject
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

#include "fs-rtp-codec-cache.h"

#include <string.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <gst/farsight/fs-conference-iface.h>

#include "fs-rtp-conference.h"


/* Because of annoying CRTs */
#if defined (_MSC_VER) && _MSC_VER >= 1400
# include <io.h>
# define open _open
# define close _close
# define read _read
# define write _write
# define stat _stat
# define STAT_TYPE struct _stat
#else
# define STAT_TYPE struct stat
#endif

#define GST_CAT_DEFAULT fsrtpconference_disco

static gboolean codecs_cache_valid (gchar *cache_path) {
  time_t cache_ts = 0;
  time_t registry_ts = 0;
  STAT_TYPE cache_stat;
  STAT_TYPE registry_stat;
  gchar *registry_xml_path;
  gchar *registry_bin_path;

  registry_xml_path = g_strdup (g_getenv ("GST_REGISTRY"));
  if (registry_xml_path == NULL) {
    registry_bin_path = g_build_filename (g_get_home_dir (),
        ".gstreamer-" GST_MAJORMINOR, "registry." HOST_CPU ".bin", NULL);
    registry_xml_path = g_build_filename (g_get_home_dir (),
        ".gstreamer-" GST_MAJORMINOR, "registry." HOST_CPU ".xml", NULL);
  } else {
    registry_bin_path = g_strdup (registry_xml_path);
  }

  if (stat (registry_xml_path, &registry_stat) == 0) {
    registry_ts = registry_stat.st_mtime;
  }

  if (stat (registry_bin_path, &registry_stat) == 0) {
    if (registry_ts < registry_stat.st_mtime) {
      registry_ts = registry_stat.st_mtime;
    }
  }

  if (stat (cache_path, &cache_stat) == 0) {
    cache_ts = cache_stat.st_mtime;
  }

  g_free (registry_bin_path);
  g_free (registry_xml_path);

  return (registry_ts != 0 && cache_ts > registry_ts);
}

static gchar *
get_codecs_cache_path (FsMediaType media_type) {
  gchar *cache_path;

  if (media_type == FS_MEDIA_TYPE_AUDIO) {
    cache_path = g_strdup (g_getenv ("FS_AUDIO_CODECS_CACHE"));
    if (cache_path == NULL) {
      cache_path = g_build_filename (g_get_user_cache_dir (), "farsight",
          "codecs.audio." HOST_CPU ".cache", NULL);
    }
  } else if (media_type == FS_MEDIA_TYPE_VIDEO) {
    cache_path = g_strdup (g_getenv ("FS_VIDEO_CODECS_CACHE"));
    if (cache_path == NULL) {
      cache_path = g_build_filename (g_get_user_cache_dir (), "farsight",
          "codecs.video." HOST_CPU ".cache", NULL);
    }
  } else {
    GST_ERROR ("Unknown media type %d for cache loading", media_type);
    return NULL;
  }

  return cache_path;
}


static gboolean
read_codec_blueprint_uint (gchar **in, gsize *size, guint *val) {
  if (*size < sizeof (guint))
    return FALSE;

  memcpy (val, *in, sizeof(guint));
  *in += sizeof (guint);
  *size -= sizeof (guint);
  return TRUE;
}

static gboolean
read_codec_blueprint_int (gchar **in, gsize *size, gint *val) {
  if (*size < sizeof (gint))
    return FALSE;

  memcpy (val, *in, sizeof(gint));
  *in += sizeof (gint);
  *size -= sizeof (gint);
  return TRUE;
}

static gboolean
read_codec_blueprint_string (gchar **in, gsize *size, gchar **str) {
  gint str_length;

  if (!read_codec_blueprint_int (in, size, &str_length))
    return FALSE;

  if (*size < str_length)
    return FALSE;

  *str = g_new0 (gchar, str_length +1);
  memcpy (*str, *in, str_length);
  *in += str_length;
  *size -= str_length;

  return TRUE;
}


#define READ_CHECK(x) if (!x) goto error;

static CodecBlueprint *
load_codec_blueprint (FsMediaType media_type, gchar **in, gsize *size) {
  CodecBlueprint *codec_blueprint = g_slice_new0 (CodecBlueprint);
  gchar *tmp;
  gint tmp_size;
  int i;

  codec_blueprint->codec = g_slice_new0 (FsCodec);
  codec_blueprint->codec->media_type = media_type;

  READ_CHECK (read_codec_blueprint_int
      (in, size, &(codec_blueprint->codec->id)));
  READ_CHECK (read_codec_blueprint_string
      (in, size, &(codec_blueprint->codec->encoding_name)));
  READ_CHECK (read_codec_blueprint_uint
      (in, size, &(codec_blueprint->codec->clock_rate)));
  READ_CHECK (read_codec_blueprint_uint
      (in, size, &(codec_blueprint->codec->channels)));


  READ_CHECK (read_codec_blueprint_int (in, size, &tmp_size));
  for (i = 0; i < tmp_size; i++) {
    gchar *name, *value;
    READ_CHECK (read_codec_blueprint_string (in, size, &(name)));
    READ_CHECK (read_codec_blueprint_string (in, size, &(value)));
    fs_codec_add_optional_parameter (codec_blueprint->codec, name, value);
    g_free (name);
    g_free (value);
  }

  READ_CHECK (read_codec_blueprint_string (in, size, &tmp));
  codec_blueprint->media_caps = gst_caps_from_string (tmp);
  g_free (tmp);

  READ_CHECK (read_codec_blueprint_string (in, size, &tmp));
  codec_blueprint->rtp_caps = gst_caps_from_string (tmp);
  g_free (tmp);

  READ_CHECK (read_codec_blueprint_int (in, size, &tmp_size));
  for (i = 0; i < tmp_size; i++) {
    int j, tmp_size2;
    GList *tmplist = NULL;

    READ_CHECK (read_codec_blueprint_int (in, size, &tmp_size2));
    for (j = 0; j < tmp_size2; j++) {
      GstElementFactory *fact = NULL;
      READ_CHECK (read_codec_blueprint_string (in, size, &(tmp)));
      fact = gst_element_factory_find (tmp);
      g_free (tmp);
      if (!fact)
        goto error;
      tmplist = g_list_append (tmplist, fact);
    }
    codec_blueprint->send_pipeline_factory =
      g_list_append (codec_blueprint->send_pipeline_factory, tmplist);
  }

  READ_CHECK (read_codec_blueprint_int (in, size, &tmp_size));
  for (i = 0; i < tmp_size; i++) {
    int j, tmp_size2;
    GList *tmplist = NULL;

    READ_CHECK (read_codec_blueprint_int (in, size, &tmp_size2));
    for (j = 0; j < tmp_size2; j++) {
      GstElementFactory *fact = NULL;
      READ_CHECK (read_codec_blueprint_string (in, size, &(tmp)));
      fact = gst_element_factory_find (tmp);
      g_free (tmp);
      if (!fact)
        goto error;
      tmplist = g_list_append (tmplist, fact);
    }
    codec_blueprint->receive_pipeline_factory =
      g_list_append (codec_blueprint->receive_pipeline_factory, tmplist);
  }

  GST_DEBUG ("adding codec %s with pt %d, send_pipeline %p, receive_pipeline %p",
      codec_blueprint->codec->encoding_name, codec_blueprint->codec->id,
      codec_blueprint->send_pipeline_factory,
      codec_blueprint->receive_pipeline_factory);

  return codec_blueprint;

 error:
  codec_blueprint_destroy (codec_blueprint);

  return NULL;
}


/**
 * load_codecs_cache
 * @media_type: a #FsMediaType
 *
 * Will load the codecs blueprints from the cache.
 *
 * Returns: TRUE if successful, FALSE if error, or cache outdated
 *
 */
GList *
load_codecs_cache (FsMediaType media_type)
{
  GMappedFile *mapped = NULL;
  gchar *contents = NULL;
  gchar *in = NULL;
  gsize size;
  GError *err = NULL;
  GList *blueprints = NULL;

  gchar magic[8] = {0};
  gchar magic_media = '?';
  gint num_blueprints;
  gchar *cache_path;
  int i;


  if (media_type == FS_MEDIA_TYPE_AUDIO) {
    magic_media = 'A';
  } else if (media_type == FS_MEDIA_TYPE_VIDEO) {
    magic_media = 'V';
  } else {
    GST_ERROR ("Invalid media type %d", media_type);
    return NULL;
  }

  cache_path = get_codecs_cache_path (media_type);

  if (!cache_path)
    return NULL;

  if (!codecs_cache_valid (cache_path)) {
    GST_DEBUG ("Codecs cache %s is outdated or does not exist", cache_path);
    g_free (cache_path);
    return NULL;
  }

  GST_DEBUG ("Loading codecs cache %s", cache_path);

  mapped = g_mapped_file_new (cache_path, FALSE, &err);
  if (mapped == NULL) {
    GST_DEBUG ("Unable to mmap file %s : %s", cache_path,
      err ? err->message: "unknown error");
    g_clear_error (&err);

    if (!g_file_get_contents (cache_path, &contents, &size, NULL))
      goto error;
  } else {
    if ((contents = g_mapped_file_get_contents (mapped)) == NULL) {
      GST_WARNING ("Can't load file %s : %s", cache_path, g_strerror (errno));
      goto error;
    }
    /* check length for header */
    size = g_mapped_file_get_length (mapped);
  }

  /* in is a cursor pointer on the file contents */

  in = contents;

  if (size < sizeof (magic)) {
    GST_WARNING ("Cache file corrupt");
    goto error;
  }

  memcpy (magic, in, sizeof (magic));
  in += sizeof (magic);
  size -= sizeof (magic);

  if (magic[0] != 'F' ||
      magic[1] != 'S' ||
      magic[2] != magic_media ||
      magic[3] != 'C' ||
      magic[4] != '1' ||   /* This is the version number */
      magic[5] != '1') {
    GST_WARNING ("Cache file has incorrect magic header. File corrupted");
    goto error;
  }

  if (size < sizeof (gint)) {
    GST_WARNING ("Cache file corrupt (size: %"G_GSIZE_FORMAT" < sizeof (int))",
        size);
    goto error;
  }

  memcpy (&num_blueprints, in, sizeof(gint));
  in += sizeof (gint);
  size -= sizeof (gint);

  if (num_blueprints > 50)
  {
    GST_WARNING ("Impossible number of blueprints in cache %d, ignoring",
        num_blueprints);
    goto error;
  }

  for (i = 0; i < num_blueprints; i++) {
    CodecBlueprint *blueprint = load_codec_blueprint (media_type, &in, &size);
    if (!blueprint) {
      GST_WARNING ("Can not load all of the blueprints, cache corrupted");

      if (blueprints) {
        g_list_foreach (blueprints, (GFunc) codec_blueprint_destroy, NULL);
        g_list_free (blueprints);
        blueprints = NULL;
      }

      goto error;
    }
    blueprints = g_list_append (blueprints, blueprint);
  }

 error:
  if (mapped) {
#if GLIB_CHECK_VERSION(2,22,0)
    g_mapped_file_unref (mapped);
#else
    g_mapped_file_free (mapped);
#endif
  } else {
    g_free (contents);
  }
  g_free (cache_path);
  return blueprints;
}

#define WRITE_CHECK(x) if (!x) return FALSE;

static gboolean
write_codec_blueprint_int (int fd, gint val) {
  return write (fd, &val, sizeof (gint)) == sizeof (gint);
}

static gboolean
write_codec_blueprint_string (int fd, const gchar *str) {
  gint size;

  size = strlen (str);
  WRITE_CHECK (write_codec_blueprint_int (fd, size));
  return write (fd, str, size) == size;
}

static gboolean
save_codec_blueprint (int fd, CodecBlueprint *codec_blueprint) {
  gchar *caps;
  const gchar *factory_name;
  GList *walk;
  gint size;

  WRITE_CHECK (write_codec_blueprint_int
      (fd, codec_blueprint->codec->id));
  WRITE_CHECK (write_codec_blueprint_string
      (fd, codec_blueprint->codec->encoding_name));
  WRITE_CHECK (write_codec_blueprint_int
      (fd, codec_blueprint->codec->clock_rate));
  WRITE_CHECK (write_codec_blueprint_int
      (fd, codec_blueprint->codec->channels));

  size = g_list_length (codec_blueprint->codec->optional_params);
  WRITE_CHECK (write_codec_blueprint_int (fd, size));
  for (walk = codec_blueprint->codec->optional_params; walk;
       walk = g_list_next (walk)) {
    FsCodecParameter *param = walk->data;
    WRITE_CHECK (write_codec_blueprint_string (fd, param->name));
    WRITE_CHECK (write_codec_blueprint_string (fd, param->value));
  }

  caps = gst_caps_to_string (codec_blueprint->media_caps);
  WRITE_CHECK (write_codec_blueprint_string (fd, caps));
  g_free (caps);

  caps = gst_caps_to_string (codec_blueprint->rtp_caps);
  WRITE_CHECK (write_codec_blueprint_string (fd, caps));
  g_free (caps);

  walk = codec_blueprint->send_pipeline_factory;
  size = g_list_length (walk);
  if (write (fd, &size, sizeof (gint)) != sizeof (gint))
    return FALSE;

  for (; walk; walk = g_list_next (walk)) {
    GList *walk2 = walk->data;
    size = g_list_length (walk2);
    if (write (fd, &size, sizeof (gint)) != sizeof (gint))
      return FALSE;
    for (; walk2; walk2 = g_list_next (walk2)) {
      GstElementFactory *fact = walk2->data;
      factory_name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (fact));
      WRITE_CHECK (write_codec_blueprint_string (fd, factory_name));
    }
  }

  walk = codec_blueprint->receive_pipeline_factory;
  size = g_list_length (walk);
  if (write (fd, &size, sizeof (gint)) != sizeof (gint))
    return FALSE;

  for (; walk; walk = g_list_next (walk)) {
    GList *walk2 = walk->data;
    size = g_list_length (walk2);
    if (write (fd, &size, sizeof (gint)) != sizeof (gint))
      return FALSE;
    for (; walk2; walk2 = g_list_next (walk2)) {
      GstElementFactory *fact = walk2->data;
      factory_name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (fact));
      WRITE_CHECK (write_codec_blueprint_string (fd, factory_name));
    }
  }

  return TRUE;
}


gboolean
save_codecs_cache (FsMediaType media_type, GList *blueprints)
{
  gchar *cache_path;
  GList *item;
  gchar *tmp_path;
  int fd;
  int size;
  gchar magic[8] = {0};

  cache_path = get_codecs_cache_path (media_type);
  if (!cache_path)
    return FALSE;


  GST_DEBUG ("Saving codecs cache to %s", cache_path);

  tmp_path = g_strconcat (cache_path, ".tmpXXXXXX", NULL);
  fd = g_mkstemp (tmp_path);
  if (fd == -1) {
    gchar *dir;

    /* oops, I bet the directory doesn't exist */
    dir = g_path_get_dirname (cache_path);
    g_mkdir_with_parents (dir, 0777);
    g_free (dir);

    /* the previous g_mkstemp call overwrote the XXXXXX placeholder ... */
    g_free (tmp_path);
    tmp_path = g_strconcat (cache_path, ".tmpXXXXXX", NULL);
    fd = g_mkstemp (tmp_path);

    if (fd == -1) {
      GST_DEBUG ("Unable to save codecs cache. g_mkstemp () failed: %s",
          g_strerror (errno));
      g_free (tmp_path);
      g_free (cache_path);
      return FALSE;
    }
  }

  magic[0] = 'F';
  magic[1] = 'S';
  magic[2] = '?';
  magic[3] = 'C';

  if (media_type == FS_MEDIA_TYPE_AUDIO) {
    magic[2] = 'A';
  } else if (media_type == FS_MEDIA_TYPE_VIDEO) {
    magic[2] = 'V';
  }

  /* version of the binary format */
  magic[4] = '1';
  magic[5] = '1';

  if (write (fd, magic, 8) != 8)
    return FALSE;


  size = g_list_length (blueprints);
  if (write (fd, &size, sizeof (gint)) != sizeof (gint))
    return FALSE;


  for (item = g_list_first (blueprints);
       item;
       item = g_list_next (item)) {
    CodecBlueprint *codec_blueprint = item->data;
    if (!save_codec_blueprint (fd, codec_blueprint)) {
      GST_WARNING ("Unable to save codec cache");
      close (fd);
      g_free (tmp_path);
      g_free (cache_path);
      return FALSE;
    }
  }


  if (close (fd) < 0) {
    GST_DEBUG ("Can't close codecs cache file : %s", g_strerror (errno));
      g_free (tmp_path);
      g_free (cache_path);
      return FALSE;
  }

  if (g_file_test (tmp_path, G_FILE_TEST_EXISTS)) {
#ifdef WIN32
    remove (cache_path);
#endif
    rename (tmp_path, cache_path);
  }

  g_free (tmp_path);
  g_free (cache_path);
  GST_DEBUG ("Wrote binary codecs cache");
  return TRUE;
}

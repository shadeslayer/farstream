/*
 * Farsight2 - Utility functions
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-session.h - A Farsight Session gobject (base implementation)
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


/**
 * SECTION:fs-utils
 * @short_description: Various utility functions
 *
 * This file contains various utility functions for farsight
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "fs-utils.h"

struct FsElementAddedData {
  gint refcount;
  FsElementAddedCallback callback;
  gpointer user_data;
  GstElement *head;
};


static gpointer _element_added_callback (GstBin *parent, GstElement *element,
    gpointer user_data);


static struct FsElementAddedData *
element_added_data_new (FsElementAddedCallback callback, gpointer user_data,
                        GstElement *head)
{
  struct FsElementAddedData *data =
    g_new (struct FsElementAddedData, 1);

  data->refcount = 0;
  data->callback = callback;
  data->user_data = user_data;
  data->head = head;

  return data;
}

static void
element_added_data_inc (struct FsElementAddedData *data)
{
  g_atomic_int_inc (&data->refcount);
}


static void
element_added_data_dec (struct FsElementAddedData *data)
{
  if (g_atomic_int_dec_and_test (&data->refcount))
  {
    g_free (data);
  }
}


static void
_bin_unparented_cb (GstObject *object, GstObject *parent, gpointer user_data)
{
  GstIterator *iter = NULL;
  gboolean done;

  /* Return if there was no handler connected */
  if (g_signal_handlers_disconnect_by_func(object, _element_added_callback,
          user_data) == 0)
    return;

  iter = gst_bin_iterate_elements (GST_BIN (object));

  done = FALSE;
  while (!done)
  {
    gpointer item;

    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
        {
        if (GST_IS_BIN (item))
          _bin_unparented_cb (GST_OBJECT (item), object, user_data);
        }
        break;
      case GST_ITERATOR_RESYNC:
        // We don't rollback anything, we just ignore already processed ones
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        g_error ("Wrong parameters were given?");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }

  gst_iterator_free (iter);
}

static gpointer
_element_added_callback (GstBin *parent, GstElement *element,
    gpointer user_data)
{
  struct FsElementAddedData *data = user_data;

  if (GST_IS_BIN (element)) {
    GstIterator *iter = NULL;
    gboolean done;

    element_added_data_inc (data);
    g_object_weak_ref (G_OBJECT (element), (GWeakNotify) element_added_data_dec,
        user_data);
    g_signal_connect (element, "element-added",
        G_CALLBACK (_element_added_callback), user_data);

    if (data->head != element)
      g_signal_connect (element, "parent-unset",
          G_CALLBACK (_bin_unparented_cb), user_data);

    iter = gst_bin_iterate_elements (GST_BIN (element));

    done = FALSE;
    while (!done)
    {
      gpointer item = NULL;

      switch (gst_iterator_next (iter, &item)) {
       case GST_ITERATOR_OK:
         /* We make sure the callback has not already been added */
         if (g_signal_handler_find (item,
                 G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                 0, 0, NULL, /* id, detail, closure */
                 _element_added_callback, user_data) == 0)
           _element_added_callback (GST_BIN_CAST (element), item, user_data);
         gst_object_unref (item);
         break;
       case GST_ITERATOR_RESYNC:
         // We don't rollback anything, we just ignore already processed ones
         gst_iterator_resync (iter);
         break;
       case GST_ITERATOR_ERROR:
         g_error ("Wrong parameters were given?");
         done = TRUE;
         break;
       case GST_ITERATOR_DONE:
         done = TRUE;
         break;
     }
    }

    gst_iterator_free (iter);
  }

  data->callback (parent, element, data->user_data);

  return data;
}

/**
 * fs_utils_add_recursive_element_added_notification:
 * @element: A #GstElement
 * @callback: the function to be called when a new element is added
 * @user_data: data that will be passed to the callback
 *
 * The callback will be called on the element and every sub-element if its a
 * bin and this will be done recursively. The function will also be called on
 * any element added in the future to the bin. The callback may be called more
 * than once and should be thread safe (elements may be added from the streaming
 * threads).
 *
 * Returns: a handle that can be used when calling
 *  #fs_utils_remove_recursive_element_added_notification, or NULL if there was
 *  an error
 */

gpointer
fs_utils_add_recursive_element_added_notification (GstElement *element,
    FsElementAddedCallback callback,
    gpointer user_data)
{
  g_assert (callback);

  return _element_added_callback (NULL, element,
      element_added_data_new (callback, user_data, element));
}

/**
 * fs_utils_remove_recursive_element_added_notification:
 * @bin: a #GstBin on which fs_utils_add_recursive_element_added_notification
 *   has been called
 * @handle: the handle returned by
 *     #fs_utils_add_recursive_element_added_notification
 *
 * This function will remove the callback added by
 * #fs_utils_add_recursive_element_added_notification
 *
 * Returns: TRUE if the notification could be removed, FALSE otherwise
 */
gboolean
fs_utils_remove_recursive_element_added_notification (GstElement *element,
    gpointer handle)
{
  struct FsElementAddedData *data = handle;

  if (g_signal_handler_find (element,
          G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
          0, 0, NULL, /* id, detail, closure */
          _element_added_callback, data) != 0)
  {
    g_assert (data->head == element);
    _bin_unparented_cb (GST_OBJECT (data->head), NULL, data);
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

#if 1
# define DEBUG(...) do {} while (0)
#else
# define DEBUG g_debug
#endif

static void
_bin_added_from_keyfile (GstBin *bin, GstElement *element, gpointer user_data)
{
  GKeyFile *keyfile = user_data;
  GstElementFactory *factory = NULL;
  const gchar *name;
  gchar **keys;
  gint i;

  factory = gst_element_get_factory (element);

  g_assert (factory);

  name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory));

  if (!name)
    return;


  if (!g_key_file_has_group (keyfile, name))
    return;


  DEBUG ("Found config for %s", name);
  keys = g_key_file_get_keys (keyfile, name, NULL, NULL);

  for (i = 0; keys[i]; i++)
  {
    GParamSpec *param_spec;
    GValue key_value = { 0 };
    GValue prop_value = { 0 };

    gchar *str_key_value;
    gboolean bool_key_value;
    gint int_key_value;
    gdouble double_key_value;
    glong long_key_value;
    gulong ulong_key_value;

    DEBUG ("getting %s", keys[i]);
    param_spec = g_object_class_find_property
      (G_OBJECT_GET_CLASS(element), keys[i]);

    /* If the paremeter does not exist, or is one of those,
     * then lets skip it
     * TODO: What if we want to pass GstCaps as strings?
     */
    if (!param_spec ||
        g_type_is_a (param_spec->value_type, G_TYPE_OBJECT) ||
        g_type_is_a (param_spec->value_type, GST_TYPE_MINI_OBJECT) ||
        g_type_is_a (param_spec->value_type, G_TYPE_INTERFACE) ||
        g_type_is_a (param_spec->value_type, G_TYPE_BOXED) ||
        g_type_is_a (param_spec->value_type, G_TYPE_GTYPE) ||
        g_type_is_a (param_spec->value_type, G_TYPE_POINTER))
    {
      continue;
    }

    g_value_init (&prop_value, param_spec->value_type);

    switch (param_spec->value_type)
    {
      case G_TYPE_STRING:
        str_key_value = g_key_file_get_value (keyfile, name,
            keys[i], NULL);
        g_value_init (&key_value, G_TYPE_STRING);
        g_value_set_string (&key_value, str_key_value);
        DEBUG ("%s is a string: %s", keys[i], str_key_value);
        g_free (str_key_value);
        break;
      case G_TYPE_BOOLEAN:
        bool_key_value = g_key_file_get_boolean (keyfile, name,
            keys[i], NULL);
        g_value_init (&key_value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&key_value, bool_key_value);
        DEBUG ("%s is a boolean: %d", keys[i], bool_key_value);
        break;
      case G_TYPE_UINT64:
      case G_TYPE_INT64:
      case G_TYPE_DOUBLE:
        /* FIXME it seems get_double is only in 2.12, so for now get a
         * string and convert it to double */
#if GLIB_CHECK_VERSION(2,12,0)
        double_key_value = g_key_file_get_double (keyfile, name,
            keys[i], NULL);
#else
        str_key_value = g_key_file_get_value (keyfile, name, keys[i],
            NULL);
        double_key_value = g_strtod(str_key_value, NULL);
#endif
        g_value_init (&key_value, G_TYPE_DOUBLE);
        g_value_set_double (&key_value, double_key_value);
        DEBUG ("%s is a uint64", keys[i]);
        DEBUG ("%s is a int64", keys[i]);
        DEBUG ("%s is a double: %f", keys[i], double_key_value);
        break;
      case G_TYPE_ULONG:
        str_key_value = g_key_file_get_value (keyfile, name, keys[i],
            NULL);
        ulong_key_value = strtoul(str_key_value, NULL, 10);
        g_value_init (&key_value, G_TYPE_ULONG);
        g_value_set_ulong (&key_value, ulong_key_value);
        DEBUG ("%s is a ulong: %lu", keys[i], ulong_key_value);
        break;
      case G_TYPE_LONG:
        str_key_value = g_key_file_get_value (keyfile, name, keys[i],
            NULL);
        long_key_value = strtol(str_key_value, NULL, 10);
        g_value_init (&key_value, G_TYPE_LONG);
        g_value_set_long (&key_value, long_key_value);
        DEBUG ("%s is a long: %ld", keys[i], long_key_value);
        break;
      case G_TYPE_INT:
      case G_TYPE_UINT:
      case G_TYPE_ENUM:
      default:
        int_key_value = g_key_file_get_integer (keyfile, name,
            keys[i], NULL);
        g_value_init (&key_value, G_TYPE_INT);
        g_value_set_int (&key_value, int_key_value);
        DEBUG ("%s is a int: %d", keys[i], int_key_value);
        DEBUG ("%s is a uint", keys[i]);
        DEBUG ("%s is an enum", keys[i]);
        DEBUG ("%s is something else, attempting to int conv", keys[i]);
        break;
    }

    if (!g_value_transform (&key_value, &prop_value))
    {
      DEBUG ("Could not transform gvalue pair");
      continue;
    }

    DEBUG ("Setting %s to on %s", keys[i], name);
    g_object_set_property (G_OBJECT(element), keys[i], &prop_value);
  }

  g_strfreev(keys);
}

/**
 * fs_utils_add_recursive_element_setter_from_keyfile:
 * @element: a #GstElement
 * @keyfile: a #GKeyFile
 *
 * Using a keyfile where the groups are the element's type and the key=value
 * are the property and its value, this function will set the properties on the
 * element passed and its subelements.
 *
 * Returns: a handle that can be used for
 *  fs_utils_remove_recursive_element_added_notification(), or NULL if there is
 *  an error
 */
gpointer
fs_utils_add_recursive_element_setter_from_keyfile (GstElement *element,
    GKeyFile *keyfile)
{
  return fs_utils_add_recursive_element_added_notification (element,
      _bin_added_from_keyfile,
      keyfile);
}

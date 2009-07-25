/*
 * Farsight2 - Recursive element addition notifier
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
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
 * SECTION:fs-element-added-notifier
 * @short_description: Recursive element addition notifier
 *
 * This object can be attach to any #GstBin and will emit a the
 * #FsElementAddedNotifier::element-added signal for every element inside the
 * #GstBin or any sub-bin and any element added in the future to the bin or
 * its sub-bins. There is also a utility method to have it used to
 * set the properties of elements based on a GKeyfile.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>

#include "fs-element-added-notifier.h"
#include "fs-marshal.h"


/* Signals */
enum
{
  ELEMENT_ADDED,
  LAST_SIGNAL
};

#define FS_ELEMENT_ADDED_NOTIFIER_GET_PRIVATE(o)                     \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_ELEMENT_ADDED_NOTIFIER, \
      FsElementAddedNotifierPrivate))

struct _FsElementAddedNotifierPrivate {
  GList *keyfiles;
};

static void _element_added_callback (GstBin *parent, GstElement *element,
    gpointer user_data);

static void fs_element_added_notifier_finalize (GObject *object);


G_DEFINE_TYPE(FsElementAddedNotifier, fs_element_added_notifier, G_TYPE_OBJECT);

static guint signals[LAST_SIGNAL] = { 0 };

static void
fs_element_added_notifier_class_init (FsElementAddedNotifierClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fs_element_added_notifier_finalize;

   /**
   * FsElementAddedNotifier::element-added:
   * @self: #FsElementAddedNotifier that emitted the signal
   * @bin: The #GstBin to which this object was added
   * @element: The #GstElement that was added
   *
   * This signal is emitted when an element is added to a #GstBin that was added
   * to this object or one of its sub-bins.
   * Be careful, there is no guarantee that this will be emitted on your
   * main thread, it will be emitted in the thread that added the element.
   * The bin may be %NULL if this is the top-level bin.
   */
  signals[ELEMENT_ADDED] = g_signal_new ("element-added",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      _fs_marshal_VOID__OBJECT_OBJECT,
      G_TYPE_NONE, 2, GST_TYPE_BIN, GST_TYPE_ELEMENT);

  g_type_class_add_private (klass, sizeof (FsElementAddedNotifierPrivate));
}


static void
fs_element_added_notifier_init (FsElementAddedNotifier *notifier)
{
  notifier->priv = FS_ELEMENT_ADDED_NOTIFIER_GET_PRIVATE(notifier);
}



static void
fs_element_added_notifier_finalize (GObject *object)
{
  FsElementAddedNotifier *self = FS_ELEMENT_ADDED_NOTIFIER (object);

  g_list_foreach (self->priv->keyfiles, (GFunc) g_key_file_free, NULL);
  g_list_free (self->priv->keyfiles);
  self->priv->keyfiles = NULL;
}

/**
 * fs_element_added_notifier_new:
 *
 * Creates a new #FsElementAddedNotifier object
 *
 * Returns: the newly-created #FsElementAddedNotifier
 */

FsElementAddedNotifier *
fs_element_added_notifier_new (void)
{
  return (FsElementAddedNotifier *)
    g_object_new (FS_TYPE_ELEMENT_ADDED_NOTIFIER, NULL);
}

/**
 * fs_element_added_notifier_add:
 * @notifier: a #FsElementAddedNotifier
 * @bin: A #GstBin to watch to added elements
 *
 * Add a #GstBin to on which the #FsElementAddedNotifier::element-added signal
 * will be called on every element and sub-element present and added in the
 * future.
 */

void
fs_element_added_notifier_add (FsElementAddedNotifier *notifier,
    GstBin *bin)
{
  g_return_if_fail (notifier && FS_IS_ELEMENT_ADDED_NOTIFIER (notifier));
  g_return_if_fail (bin && GST_IS_BIN (bin));

  _element_added_callback (NULL, GST_ELEMENT_CAST (bin), notifier);
}


static void
_bin_unparented_cb (GstObject *object, GstObject *parent, gpointer user_data)
{
  GstIterator *iter = NULL;
  FsElementAddedNotifier *notifier = FS_ELEMENT_ADDED_NOTIFIER (user_data);
  gboolean done;

  /* Return if there was no handler connected */
  if (g_signal_handlers_disconnect_by_func (object, _element_added_callback,
          user_data) == 0)
    return;

  iter = gst_bin_iterate_elements (GST_BIN (object));

  done = FALSE;
  while (!done)
  {
    gpointer item;

    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
        if (GST_IS_BIN (item))
          _bin_unparented_cb (GST_OBJECT (item), object, user_data);
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

  g_object_weak_unref (G_OBJECT (object), (GWeakNotify) g_object_unref,
      notifier);
  g_object_unref (notifier);
}


/**
 * fs_element_added_notifier_remove:
 * @notifier: a #FsElementAddedNotifier
 * @bin: A #GstBin to stop watching
 *
 * Stop watching the passed bin and its subbins.
 *
 * Returns: %TRUE if the #GstBin was being watched, %FALSE otherwise
 */

gboolean
fs_element_added_notifier_remove (FsElementAddedNotifier *notifier,
    GstBin *bin)
{
  g_return_val_if_fail (FS_IS_ELEMENT_ADDED_NOTIFIER (notifier), FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);

  if (g_signal_handler_find (bin,
          G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
          0, 0, NULL, /* id, detail, closure */
          _element_added_callback, notifier) != 0)
  {
    _bin_unparented_cb (GST_OBJECT (bin), NULL, notifier);
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
_bin_added_from_keyfile (FsElementAddedNotifier *notifier, GstBin *bin,
    GstElement *element, gpointer user_data)
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
        double_key_value = g_key_file_get_double (keyfile, name,
            keys[i], NULL);
        g_value_init (&key_value, G_TYPE_DOUBLE);
        g_value_set_double (&key_value, double_key_value);
        DEBUG ("%s is a uint64", keys[i]);
        DEBUG ("%s is a int64", keys[i]);
        DEBUG ("%s is a double: %f", keys[i], double_key_value);
        break;
      case G_TYPE_ULONG:
        str_key_value = g_key_file_get_value (keyfile, name, keys[i],
            NULL);
        ulong_key_value = strtoul (str_key_value, NULL, 10);
        g_value_init (&key_value, G_TYPE_ULONG);
        g_value_set_ulong (&key_value, ulong_key_value);
        DEBUG ("%s is a ulong: %lu", keys[i], ulong_key_value);
        break;
      case G_TYPE_LONG:
        str_key_value = g_key_file_get_value (keyfile, name, keys[i],
            NULL);
        long_key_value = strtol (str_key_value, NULL, 10);
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
    g_object_set_property (G_OBJECT (element), keys[i], &prop_value);
  }

  g_strfreev (keys);
}


/**
 * fs_element_added_notifier_set_properties_from_keyfile:
 * @notifier: a #FsElementAddedNotifier
 * @keyfile: a #GKeyFile
 *
 * Using a #GKeyFile where the groups are the element's type and the key=value
 * are the property and its value, this function will set the properties
 * on the elements added to this object after this function has been called.
 * It will take ownership of the GKeyFile structure.
 */
void
fs_element_added_notifier_set_properties_from_keyfile (
    FsElementAddedNotifier *notifier,
    GKeyFile *keyfile)
{
  g_return_if_fail (FS_IS_ELEMENT_ADDED_NOTIFIER (notifier));
  g_return_if_fail (keyfile);

  g_signal_connect (notifier, "element-added",
      G_CALLBACK (_bin_added_from_keyfile), keyfile);

  notifier->priv->keyfiles =
    g_list_prepend (notifier->priv->keyfiles, keyfile);
}


/**
 * fs_element_added_notifier_set_properties_from_file:
 * @notifier: a #FsElementAddedNotifier
 * @filename: The name of the keyfile to use
 * @error: location of a #GError, or %NULL if no error occured
 *
 * Same as fs_element_added_notifier_set_properties_from_keyfile() but using
 * the name of the file to load instead of the #GKeyFile directly.
 *
 * Returns: %TRUE if the file was successfully loaded, %FALSE otherwise
 */
gboolean
fs_element_added_notifier_set_properties_from_file (
    FsElementAddedNotifier *notifier,
    const gchar *filename,
    GError **error)
{
  GKeyFile *keyfile = g_key_file_new ();

  if (!g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, error))
  {
    g_key_file_free (keyfile);
    return FALSE;
  }

  fs_element_added_notifier_set_properties_from_keyfile(notifier, keyfile);

  return TRUE;
}

static void
_element_added_callback (GstBin *parent, GstElement *element,
    gpointer user_data)
{
  FsElementAddedNotifier *notifier = FS_ELEMENT_ADDED_NOTIFIER (user_data);

  if (GST_IS_BIN (element)) {
    GstIterator *iter = NULL;
    gboolean done;

    g_object_ref (notifier);
    g_object_weak_ref (G_OBJECT (element), (GWeakNotify) g_object_unref,
        notifier);
    g_signal_connect (element, "element-added",
        G_CALLBACK (_element_added_callback), notifier);

    if (parent)
      g_signal_connect (element, "parent-unset",
          G_CALLBACK (_bin_unparented_cb), notifier);

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
                 _element_added_callback, notifier) == 0)
           _element_added_callback (GST_BIN_CAST (element), item, notifier);
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

  g_signal_emit (notifier, signals[ELEMENT_ADDED], 0, parent, element);
}


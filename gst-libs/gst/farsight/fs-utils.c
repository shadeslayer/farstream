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
  GstBin *head;
};


static gpointer _element_added_callback (GstBin *parent, GstElement *element,
    gpointer user_data);


static struct FsElementAddedData *
element_added_data_new (FsElementAddedCallback callback, gpointer user_data,
                        GstBin *head)
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

    if (data->head != GST_BIN_CAST (element))
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
 * @bin: A #GstBin
 * @callback: the function to be called when a new element is added
 * @user_data: data that will be passed to the callback
 *
 * The callback will be called on every element currently inside the bin,
 * and this will be done recursively. The function will also be called on any
 * element added in the future to the bin. The callback may be called more than
 * once and should be thread safe (elements may be added from the streaming
 * threads).
 *
 * Returns: a handle that can be used when calling
 *  #fs_utils_remove_recursive_element_added_notification
 */

gpointer
fs_utils_add_recursive_element_added_notification (GstBin *bin,
    FsElementAddedCallback callback,
    gpointer user_data)
{
  g_assert (callback);
  g_assert (GST_IS_BIN (bin));

  return _element_added_callback (NULL, GST_ELEMENT_CAST (bin),
      element_added_data_new (callback, user_data, bin));
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
 */
void
fs_utils_remove_recursive_element_added_notification (GstBin *bin,
    gpointer handle)
{
  struct FsElementAddedData *data = handle;

 if (g_signal_handler_find (bin,
                 G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                 0, 0, NULL, /* id, detail, closure */
                 _element_added_callback, data) != 0)
 {
   g_assert (data->head == bin);
   _bin_unparented_cb (GST_OBJECT (data->head), NULL, data);
 }
}

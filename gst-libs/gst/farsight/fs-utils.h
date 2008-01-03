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

#ifndef __FS_UTILS_H__
#define __FS_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * FsElementAddedCallback:
 * @bin: The #GstBin to which the element was added, will be NULL if the element
 *   is the top-level bin
 * @element: The just-added #GstElement
 * @user_data: The user data passed by the user
 *
 * The callback used by #fs_utils_add_recursive_element_added_notification
 */

typedef void (*FsElementAddedCallback) (GstBin *bin,
    GstElement *element,
    gpointer user_data);

gpointer fs_utils_add_recursive_element_added_notification (GstElement *element,
    FsElementAddedCallback callback,
    gpointer user_data);

gboolean fs_utils_remove_recursive_element_added_notification (
    GstElement *element,
    gpointer handle);


gpointer fs_utils_set_options_from_keyfile_on_bin (GstElement *element,
    GKeyFile *keyfile);

G_END_DECLS

#endif /* __FS_UTILS_H__ */

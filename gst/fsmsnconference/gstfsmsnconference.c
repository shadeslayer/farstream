/*
 * Farsight2 - Farsight MSN Conference Implementation
 *
 * Copyright 2008 Richard Spiers <richard.spiers@gmail.com>
 * Copyright 2007 Nokia Corp.
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * gstfsmsnconference.c - MSN implementation for Farsight Conference Gstreamer
 *                        Elements
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

#include <gst/gst.h>

#include "fs-msn-conference.h"

static gboolean plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "fsmsnconference",
                               GST_RANK_NONE, FS_TYPE_MSN_CONFERENCE);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "fsmsnconference",
  "Farsight MSN Conference plugin",
  plugin_init,
  VERSION,
  "LGPL",
  "Farsight",
  "http://farsight.freedesktop.org/"
)

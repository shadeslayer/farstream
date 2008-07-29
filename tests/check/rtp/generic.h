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


#ifndef __GENERIC_H__
#define __GENERIC_H__

#include <gst/gst.h>
#include <gst/farsight/fs-conference-iface.h>

struct SimpleTestConference {
  gint id;
  gchar *cname;

  GstElement *pipeline;
  GstElement *conference;
  FsSession *session;
  GstElement *fakesrc;

  gboolean started;

  GList *streams;
};


struct SimpleTestStream {
  struct SimpleTestConference *dat;
  struct SimpleTestConference *target;

  FsParticipant *participant;
  FsStream *stream;

  gint buffer_count;

  GCallback handoff_handler;

  gint flags;
};

struct SimpleTestConference *setup_simple_conference (
    gint id,
    gchar *conference_elem,
    gchar *cname);

struct SimpleTestStream *simple_conference_add_stream (
    struct SimpleTestConference *dat,
    struct SimpleTestConference *target,
    guint st_param_count,
    GParameter *st_params);

void setup_fakesrc (struct SimpleTestConference *dat);

void cleanup_simple_conference (struct SimpleTestConference *dat);


#endif /* __GENERIC_H__ */

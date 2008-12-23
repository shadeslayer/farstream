/*
 * Farsight2 - Farsight RTP Special Source
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-special-source.h - A Farsight RTP Special Source gobject
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


#ifndef __FS_RTP_SPECIAL_SOURCE_H__
#define __FS_RTP_SPECIAL_SOURCE_H__

#include <gst/gst.h>

#include <gst/farsight/fs-session.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_RTP_SPECIAL_SOURCE \
  (fs_rtp_special_source_get_type ())
#define FS_RTP_SPECIAL_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_RTP_SPECIAL_SOURCE, \
      FsRtpSpecialSource))
#define FS_RTP_SPECIAL_SOURCE_CLASS(klass) \
 (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_RTP_SPECIAL_SOURCE, \
     FsRtpSpecialSourceClass))
#define FS_IS_RTP_SPECIAL_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_RTP_SPECIAL_SOURCE))
#define FS_IS_RTP_SPECIAL_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_RTP_SPECIAL_SOURCE))
#define FS_RTP_SPECIAL_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_RTP_SPECIAL_SOURCE,   \
    FsRtpSpecialSourceClass))
#define FS_RTP_SPECIAL_SOURCE_CAST(obj) ((FsRtpSpecialSource*) (obj))

typedef struct _FsRtpSpecialSource FsRtpSpecialSource;
typedef struct _FsRtpSpecialSourceClass FsRtpSpecialSourceClass;
typedef struct _FsRtpSpecialSourcePrivate FsRtpSpecialSourcePrivate;

/**
 * FsRtpSpecialSourceClass:
 * @build: The method builds the source #GstElement from the list of negotiated
 *   codecs and selected codecs, it returns %NULL on error
 * @want_source: Returns %TRUE if a source of this type should be created
 *  according to the selected codec and the negotiated codecs
 * @add_blueprint: Adds #CodecBlueprint structs to the list if the proper
 *  elements are installed, the result should always be the same if the elements
 *  installed don't change. It must fill the #CodecBlueprint completely except
 *  for the send_pipeline_factory field. If no blueprints are installed by this
 *  class, this method is not required.
 *
 * Class structure for #FsRtpSpecialSource, the build() and want_source()
 * methods are required.
 */

struct _FsRtpSpecialSourceClass
{
  GObjectClass parent_class;

  /* Object methods */

  GstElement* (*build) (FsRtpSpecialSource *source,
      GList *negotiated_codecs,
      FsCodec *selected_codec);

   /* Class methods */
  gboolean (*want_source) (FsRtpSpecialSourceClass *klass,
      GList *negotiated_codecs,
      FsCodec *selected_codec);

  GList* (*add_blueprint) (FsRtpSpecialSourceClass *klass,
      GList *blueprints);
};

/**
 * FsRtpSpecialSource:
 * @order: a number between 0 and 100 that defines in which order the sources
 * will be traversed in order to send events to them.
 */
struct _FsRtpSpecialSource
{
  GObject parent;

  /* This is set by subclasses init or build methods and is used to know
   * in which order to try events on.
   */
  guint order;

  FsRtpSpecialSourcePrivate *priv;
};

GType fs_rtp_special_source_get_type (void);

void
fs_rtp_special_sources_remove (
    GList **current_extra_sources,
    GList **negotiated_codecs,
    GMutex *mutex,
    FsCodec *send_codec,
    GstElement *bin,
    GstElement *rtpmuxer);

GList *
fs_rtp_special_sources_create (
    GList *current_extra_sources,
    GList *negotiated_codecs,
    FsCodec *send_codec,
    GstElement *bin,
    GstElement *rtpmuxer);

GList *
fs_rtp_special_sources_destroy (GList *current_extra_sources);

GList *
fs_rtp_special_sources_add_blueprints (GList *blueprints);

gboolean
fs_rtp_special_sources_start_telephony_event (GList *current_extra_sources,
      guint8 event,
      guint8 volume,
      FsDTMFMethod method);

gboolean
fs_rtp_special_sources_stop_telephony_event (GList *current_extra_sources,
    FsDTMFMethod method);

G_END_DECLS

#endif /* __FS_RTP_SPECIAL_SOURCE_H__ */

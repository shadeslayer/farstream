/*
 * Farsight2 - Farsight Stream Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-stream-transmitter.h - A Farsight Stream Transmitter (base implementation)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __FS_STREAM_TRANSMITTER_H__
#define __FS_STREAM_TRANSMITTER_H__

#include <glib.h>
#include <glib-object.h>

#include <gst/farsight/fs-candidate.h>

G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_STREAM_TRANSMITTER \
  (fs_stream_transmitter_get_type())
#define FS_STREAM_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_STREAM_TRANSMITTER, \
                              FsStreamTransmitter))
#define FS_STREAM_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_STREAM_TRANSMITTER, \
                           FsStreamTransmitterClass))
#define FS_IS_STREAM_TRANSMITTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_STREAM_TRANSMITTER))
#define FS_IS_STREAM_TRANSMITTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_STREAM_TRANSMITTER))
#define FS_STREAM_TRANSMITTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_STREAM_TRANSMITTER, \
                              FsStreamTransmitterClass))
#define FS_STREAM_TRANSMITTER_CAST(obj) ((FsStreamTransmitter *) (obj))

typedef struct _FsStreamTransmitter FsStreamTransmitter;
typedef struct _FsStreamTransmitterClass FsStreamTransmitterClass;
typedef struct _FsStreamTransmitterPrivate FsStreamTransmitterPrivate;

/**
 * FsStreamTransmitterClass:
 * @parent_class: Our parent
 * @add_remote_candidate: Sets the remote candidate
 *
 * You must override the add_remote_candidate in a subclass
 */

struct _FsStreamTransmitterClass
{
  GObjectClass parent_class;

  /*virtual functions */
  gboolean (*add_remote_candidate) (FsStreamTransmitter *streamtransmitter,
                                    FsCandidate *candidate, GError **error);

  void (*remote_candidates_added) (FsStreamTransmitter *streamtransmitter);

  gboolean (*select_candidate_pair) (FsStreamTransmitter *streamtransmitter,
                                     gchar *lfoundation, gchar *rfoundation,
                                     GError **error);

  /*< private >*/
  gpointer _padding[8];
};

/**
 * FsStreamTransmitter:
 *
 * All members are private, access them using methods and properties
 */
struct _FsStreamTransmitter
{
  GObject parent;

  /*< private >*/
  FsStreamTransmitterPrivate *priv;
  gpointer _padding[8];
};

GType fs_stream_transmitter_get_type (void);

gboolean fs_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter,
    FsCandidate *candidate, GError **error);

void fs_stream_transmitter_remote_candidates_added (
    FsStreamTransmitter *streamtransmitter);

gboolean fs_stream_transmitter_select_candidate_pair (
    FsStreamTransmitter *streamtransmitter, gchar *lfoundation,
    gchar *rfoundation, GError **error);


G_END_DECLS

#endif /* __FS_STREAM_TRANSMITTER_H__ */

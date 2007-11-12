/*
 * Farsight2 - Farsight Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-stream.h - A Farsight Stream (base implementation)
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

#ifndef __FS_STREAM_H__
#define __FS_STREAM_H__

#include <glib.h>
#include <glib-object.h>

#include <gst/farsight/fs-candidate.h>
#include <gst/farsight/fs-codec.h>

G_BEGIN_DECLS

#define FS_TYPE_STREAM_DIRECTION (fs_stream_direction_get_type ())

/**
 * FsStreamDirection:
 * @FS_DIRECTION_NONE: No direction specified
 * @FS_DIRECTION_BOTH: Send and receive
 * @FS_DIRECTION_SEND: Send only
 * @FS_DIRECTION_RECV: Receive only
 *
 * An enum for specifying the direction of a stream
 *
 */
typedef enum
{
  FS_DIRECTION_NONE,
  FS_DIRECTION_BOTH,
  FS_DIRECTION_SEND,
  FS_DIRECTION_RECV,
} FsStreamDirection;

/* TYPE MACROS */
#define FS_TYPE_STREAM \
  (fs_stream_get_type())
#define FS_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_STREAM, FsStream))
#define FS_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_STREAM, FsStreamClass))
#define FS_IS_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_STREAM))
#define FS_IS_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_STREAM))
#define FS_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_STREAM, FsStreamClass))
#define FS_STREAM_CAST(obj) ((FsStream *) (obj))

typedef struct _FsStream FsStream;
typedef struct _FsStreamClass FsStreamClass;
typedef struct _FsStreamPrivate FsStreamPrivate;


/**
 * FsStreamError:
 * @FS_STREAM_ERROR_CONSTRUCTION: Error constructing some of the sub-elements
 * @FS_STREAM_ERROR_INVALID_ARGUMENTS: Invalid arguments to the function
 *
 * This is the enum of error numbers that will come either on the "error" signal
 * or from the Gst Bus.
 */

typedef enum {
  FS_STREAM_ERROR_CONSTRUCTION,
  FS_STREAM_ERROR_INVALID_ARGUMENTS
} FsStreamError;

/**
 * FS_STREAM_ERROR:
 *
 * This quark is used to denote errors coming from the #FsStream object
 */

#define FS_STREAM_ERROR (fs_stream_error_quark ())

GQuark fs_stream_error_quark (void);

/**
 * FsStreamClass:
 * @parent_class: Our parent
 * @add_remote_candidate: Adds a remote candidate
 * @preload_recv_codec: Set which codec to prelaod
 * @set_remote_codecs: Sets the list of remote codecs
 *
 * You must override add_remote_candidate in a subclass.
 * If you have to negotiate codecs, then you must override set_remote_codecs too
 */

struct _FsStreamClass
{
  GObjectClass parent_class;

  /*virtual functions */
  gboolean (*add_remote_candidate) (FsStream *stream,
                                    FsCandidate *candidate,
                                    GError **error);

  void (*remote_candidates_added) (FsStream *stream);

  gboolean (*select_candidate_pair) (FsStream *stream, gchar *lfoundation,
                                     gchar *rfoundation, GError **error);

  gboolean (*preload_recv_codec) (FsStream *stream, FsCodec *codec,
                                  GError **error);

  gboolean (*set_remote_codecs) (FsStream *stream,
                                 GList *remote_codecs, GError **error);

  /*< private >*/
  gpointer _padding[8];
};

/**
 * FsStream:
 *
 * All members are private, access them using methods and properties
 */
struct _FsStream
{
  GObject parent;

  /*< private >*/

  FsStreamPrivate *priv;

  gpointer _padding[8];
};

GType fs_stream_get_type (void);

gboolean fs_stream_add_remote_candidate (FsStream *stream,
                                         FsCandidate *candidate,
                                         GError **error);

void fs_stream_remote_candidates_added (FsStream *stream);

gboolean fs_stream_select_candidate_pair (FsStream *stream, gchar *lfoundation,
                                          gchar *rfoundation, GError **error);

gboolean fs_stream_preload_recv_codec (FsStream *stream, FsCodec *codec,
                                       GError **error);

gboolean fs_stream_set_remote_codecs (FsStream *stream,
                                      GList *remote_codecs, GError **error);

void fs_stream_emit_error (FsStream *stream, gint error_no,
                           gchar *error_msg, gchar *debug_msg);

G_END_DECLS

#endif /* __FS_STREAM_H__ */

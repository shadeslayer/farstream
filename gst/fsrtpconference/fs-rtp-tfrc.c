/*
 * Farsight2 - Farsight RTP Rate Control
 *
 * Copyright 2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2010 Nokia Corp.
 *
 * fs-rtp-tfrc.c - Rate control for Farsight RTP sessions
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

#include "fs-rtp-tfrc.h"


struct _FsRtpTfrcPrivate
{
  guint unused;
};

G_DEFINE_TYPE (FsRtpTfrc, fs_rtp_tfrc, GST_TYPE_OBJECT);

#define FS_RTP_TFRC_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_TFRC,  \
      FsRtpTfrcPrivate))

static void fs_rtp_tfrc_dispose (GObject *object);
static void fs_rtp_tfrc_finalize (GObject *object);


static void
fs_rtp_tfrc_class_init (FsRtpTfrcClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = fs_rtp_tfrc_dispose;
  gobject_class->finalize = fs_rtp_tfrc_finalize;

  g_type_class_add_private (klass, sizeof (FsRtpTfrcPrivate));
}


static void
fs_rtp_tfrc_init (FsRtpTfrc *self)
{
  /* member init */
  self->priv = FS_RTP_TFRC_GET_PRIVATE (self);
}


static void
fs_rtp_tfrc_dispose (GObject *object)
{
}

static void
fs_rtp_tfrc_finalize (GObject *object)
{
}


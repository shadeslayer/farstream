/*
 * Farsight2 - Farsight libnice Transmitter agent object
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-nice-agent.h - A Farsight libnice transmitter agent object
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

#ifndef __FS_NICE_AGENT_H__
#define __FS_NICE_AGENT_H__

#include <glib-object.h>
#include <gst/farsight/fs-plugin.h>


G_BEGIN_DECLS

/* TYPE MACROS */
#define FS_TYPE_NICE_AGENT \
  (fs_nice_agent_get_type ())
#define FS_NICE_AGENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), FS_TYPE_NICE_AGENT, \
    FsNiceAgent))
#define FS_NICE_AGENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), FS_TYPE_NICE_AGENT, \
    FsNiceAgentClass))
#define FS_IS_NICE_AGENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), FS_TYPE_NICE_AGENT))
#define FS_IS_NICE_AGENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), FS_TYPE_NICE_AGENT))
#define FS_NICE_AGENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), FS_TYPE_NICE_AGENT, \
    FsNiceAgentClass))
#define FS_NICE_AGENT_CAST(obj) ((FsNiceAgent *) (obj))

typedef struct _FsNiceAgent FsNiceAgent;
typedef struct _FsNiceAgentClass FsNiceAgentClass;
typedef struct _FsNiceAgentPrivate FsNiceAgentPrivate;

/**
 * FsNiceAgentClass:
 * @parent_class: Our parent
 *
 * The class structure
 */

struct _FsNiceAgentClass
{
  GObjectClass parent_class;
};

/**
 * FsNiceAgent:
 * @agent: The underlying nice agent
 *
 */
struct _FsNiceAgent
{
  GObject parent;

  NiceAgent *agent;

  /*< private >*/
  FsNiceAgentPrivate *priv;
};


GType fs_nice_agent_get_type (void);

FsNiceAgent *fs_nice_agent_new (guint compatibility_mode,
    GList *preferred_local_candidates,
    GError **error);


GType
fs_nice_agent_register_type (FsPlugin *module);

G_END_DECLS

#endif /* __FS_NICE_AGENT_H__ */

/*
 * arch-tag: Header for Rhythmbox first-time druid
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <gtk/gtk.h>

#include "rhythmdb.h"

#ifndef __RB_DRUID_H
#define __RB_DRUID_H

G_BEGIN_DECLS

#define RB_TYPE_DRUID         (rb_druid_get_type ())
#define RB_DRUID(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DRUID, RBDruid))
#define RB_DRUID_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DRUID, RBDruidClass))
#define RB_IS_DRUID(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DRUID))
#define RB_IS_DRUID_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DRUID))
#define RB_DRUID_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DRUID, RBDruidClass))

typedef struct RBDruidPrivate RBDruidPrivate;

typedef struct
{
	GtkDialog parent;

	RBDruidPrivate *priv;
} RBDruid;

typedef struct
{
	GtkDialogClass parent_class;
} RBDruidClass;

GType			rb_druid_get_type	(void);

RBDruid *		rb_druid_new		(RhythmDB *db);

G_END_DECLS

#endif /* __RB_DRUID_H */
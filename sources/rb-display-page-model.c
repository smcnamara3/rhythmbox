/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "rb-display-page-group.h"
#include "rb-display-page-model.h"
#include "rb-tree-dnd.h"
#include "rb-debug.h"
#include "rb-marshal.h"
#include "rb-playlist-source.h"
#include "rb-auto-playlist-source.h"
#include "rb-static-playlist-source.h"

/**
 * SECTION:rb-display-page-model
 * @short_description: models backing the display page tree
 *
 * The #RBDisplayPageTree widget is backed by a #GtkTreeStore containing
 * the sources and a set of attributes used to structure and display
 * them, and a #GtkTreeModelFilter that hides sources with the
 * visibility property set to FALSE.  This class implements the filter
 * model and also creates the actual model.
 *
 * The display page model supports drag and drop in a variety of formats.
 * The simplest of these are text/uri-list and application/x-rhythmbox-entry,
 * which convey URIs and IDs of existing database entries.  When dragged
 * to an existing source, these just add the URIs or entries to the target
 * source.  When dragged to an empty space in the tree widget, this results
 * in the creation of a static playlist.
 *
 * text/x-rhythmbox-artist, text/x-rhythmbox-album, and text/x-rhythmbox-genre
 * are used when dragging items from the library browser.  When dragged to
 * the display page tree, these result in the creation of a new auto playlist with
 * the dragged items as criteria.
 */

enum
{
	DROP_RECEIVED,
	LAST_SIGNAL
};

static guint rb_display_page_model_signals[LAST_SIGNAL] = { 0 };

enum {
	TARGET_PROPERTY,
	TARGET_SOURCE,
	TARGET_URIS,
	TARGET_ENTRIES,
	TARGET_DELETE
};

static const GtkTargetEntry dnd_targets[] = {
	{ "text/x-rhythmbox-album", 0, TARGET_PROPERTY },
	{ "text/x-rhythmbox-artist", 0, TARGET_PROPERTY },
	{ "text/x-rhythmbox-genre", 0, TARGET_PROPERTY },
	{ "application/x-rhythmbox-source", 0, TARGET_SOURCE },
	{ "application/x-rhythmbox-entry", 0, TARGET_ENTRIES },
	{ "text/uri-list", 0, TARGET_URIS },
	{ "application/x-delete-me", 0, TARGET_DELETE }
};

static GtkTargetList *drag_target_list = NULL;

static void rb_display_page_model_drag_dest_init (RbTreeDragDestIface *iface);
static void rb_display_page_model_drag_source_init (RbTreeDragSourceIface *iface);

G_DEFINE_TYPE_EXTENDED (RBDisplayPageModel,
                        rb_display_page_model,
                        GTK_TYPE_TREE_MODEL_FILTER,
                        0,
                        G_IMPLEMENT_INTERFACE (RB_TYPE_TREE_DRAG_SOURCE,
                                               rb_display_page_model_drag_source_init)
                        G_IMPLEMENT_INTERFACE (RB_TYPE_TREE_DRAG_DEST,
                                               rb_display_page_model_drag_dest_init));

static gboolean
rb_display_page_model_drag_data_received (RbTreeDragDest *drag_dest,
					  GtkTreePath *dest,
					  GtkTreeViewDropPosition pos,
					  GtkSelectionData *selection_data)
{
	RBDisplayPageModel *model;
	GdkAtom type;

	g_return_val_if_fail (RB_IS_DISPLAY_PAGE_MODEL (drag_dest), FALSE);
	model = RB_DISPLAY_PAGE_MODEL (drag_dest);
	type = gtk_selection_data_get_data_type (selection_data);

	if (type == gdk_atom_intern ("text/uri-list", TRUE) ||
	    type == gdk_atom_intern ("application/x-rhythmbox-entry", TRUE)) {
		GtkTreeIter iter;
		RBDisplayPage *target = NULL;

		rb_debug ("text/uri-list or application/x-rhythmbox-entry drag data received");

		if (dest != NULL && gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, dest)) {
			gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
					    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &target, -1);
		}

		g_signal_emit (G_OBJECT (model), rb_display_page_model_signals[DROP_RECEIVED],
			       0, target, pos, selection_data);

		if (target != NULL) {
			g_object_unref (target);
		}

		return TRUE;
	}

        /* if artist, album or genre, only allow new playlists */
        if (type == gdk_atom_intern ("text/x-rhythmbox-album", TRUE) ||
            type == gdk_atom_intern ("text/x-rhythmbox-artist", TRUE) ||
            type == gdk_atom_intern ("text/x-rhythmbox-genre", TRUE)) {
                rb_debug ("text/x-rhythmbox-(album|artist|genre) drag data received");
                g_signal_emit (G_OBJECT (model), rb_display_page_model_signals[DROP_RECEIVED],
                               0, NULL, pos, selection_data);
                return TRUE;
        }

	if (type == gdk_atom_intern ("application/x-rhythmbox-source", TRUE)) {
		/* don't support dnd of sources */
		return FALSE;
	}

	return FALSE;
}

static gboolean
rb_display_page_model_row_drop_possible (RbTreeDragDest *drag_dest,
					 GtkTreePath *dest,
					 GtkTreeViewDropPosition pos,
					 GtkSelectionData *selection_data)
{
	RBDisplayPageModel *model;

	rb_debug ("row drop possible");
	g_return_val_if_fail (RB_IS_DISPLAY_PAGE_MODEL (drag_dest), FALSE);

	model = RB_DISPLAY_PAGE_MODEL (drag_dest);

	if (!dest)
		return TRUE;

	/* Call the superclass method */
	return gtk_tree_drag_dest_row_drop_possible (GTK_TREE_DRAG_DEST (GTK_TREE_STORE (model)),
						     dest, selection_data);
}

static gboolean
path_is_droppable (RBDisplayPageModel *model,
		   GtkTreePath *dest)
{
	GtkTreeIter iter;
	gboolean res;

	res = FALSE;

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, dest)) {
		RBDisplayPage *page;

		gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
				    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page, -1);

		if (page != NULL) {
			if (RB_IS_SOURCE (page)) {
				res = rb_source_can_paste (RB_SOURCE (page));
			}
			g_object_unref (page);
		}
	}

	return res;
}

static gboolean
rb_display_page_model_row_drop_position (RbTreeDragDest   *drag_dest,
					 GtkTreePath       *dest_path,
					 GList *targets,
					 GtkTreeViewDropPosition *pos)
{
	GtkTreeModel *model = GTK_TREE_MODEL (drag_dest);

	if (g_list_find (targets, gdk_atom_intern ("application/x-rhythmbox-source", TRUE)) && dest_path) {
		rb_debug ("application/x-rhythmbox-source type");
		return FALSE;
	}

	if (g_list_find (targets, gdk_atom_intern ("text/uri-list", TRUE)) ||
	    g_list_find (targets, gdk_atom_intern ("application/x-rhythmbox-entry", TRUE))) {
		rb_debug ("text/uri-list or application/x-rhythmbox-entry type");
		if (dest_path && !path_is_droppable (RB_DISPLAY_PAGE_MODEL (model), dest_path))
			return FALSE;

		*pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
		return TRUE;
	}

	if ((g_list_find (targets, gdk_atom_intern ("text/x-rhythmbox-artist", TRUE))
	     || g_list_find (targets, gdk_atom_intern ("text/x-rhythmbox-album", TRUE))
	     || g_list_find (targets, gdk_atom_intern ("text/x-rhythmbox-genre", TRUE)))
	    && !g_list_find (targets, gdk_atom_intern ("application/x-rhythmbox-source", TRUE))) {
		rb_debug ("genre, album, or artist type");
		*pos = GTK_TREE_VIEW_DROP_AFTER;
		return TRUE;
	}

	return FALSE;
}

static GdkAtom
rb_display_page_model_get_drag_target (RbTreeDragDest *drag_dest,
				       GtkWidget *widget,
				       GdkDragContext *context,
				       GtkTreePath *path,
				       GtkTargetList *target_list)
{
	if (g_list_find (gdk_drag_context_list_targets (context),
	    gdk_atom_intern ("application/x-rhythmbox-source", TRUE))) {
		/* always accept rb source path if offered */
		return gdk_atom_intern ("application/x-rhythmbox-source", TRUE);
	}

	if (path) {
		/* only accept text/uri-list or application/x-rhythmbox-entry drops into existing sources */
		GdkAtom entry_atom;

		entry_atom = gdk_atom_intern ("application/x-rhythmbox-entry", FALSE);
		if (g_list_find (gdk_drag_context_list_targets (context), entry_atom))
			return entry_atom;

		return gdk_atom_intern ("text/uri-list", FALSE);
	}

	return gtk_drag_dest_find_target (widget, context, target_list);
}

static gboolean
rb_display_page_model_row_draggable (RbTreeDragSource *drag_source, GList *path_list)
{
	return FALSE;
}

static gboolean
rb_display_page_model_drag_data_get (RbTreeDragSource *drag_source,
				     GList *path_list,
				     GtkSelectionData *selection_data)
{
	char *path_str;
	GtkTreePath *path;
	GdkAtom selection_data_target;
	guint target;

	selection_data_target = gtk_selection_data_get_target (selection_data);
	path = gtk_tree_row_reference_get_path (path_list->data);
	if (path == NULL)
		return FALSE;

	if (!gtk_target_list_find (drag_target_list,
				   selection_data_target,
				   &target)) {
		return FALSE;
	}

	switch (target) {
	case TARGET_SOURCE:
		rb_debug ("getting drag data as rb display page path");
		path_str = gtk_tree_path_to_string (path);
		gtk_selection_data_set (selection_data,
					selection_data_target,
					8, (guchar *) path_str,
					strlen (path_str));
		g_free (path_str);
		gtk_tree_path_free (path);
		return TRUE;
	case TARGET_URIS:
	case TARGET_ENTRIES:
	{
		RBDisplayPage *page;
		RhythmDBQueryModel *query_model;
		GtkTreeIter iter;
		GString *data;
		gboolean first = TRUE;

		rb_debug ("getting drag data as uri list");
		if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (drag_source), &iter, path))
			return FALSE;

		data = g_string_new ("");
		gtk_tree_model_get (GTK_TREE_MODEL (drag_source),
				    &iter,
				    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
				    -1);
		if (RB_IS_SOURCE (page) == FALSE) {
			g_object_unref (page);
			return FALSE;
		}
		g_object_get (page, "query-model", &query_model, NULL);
		g_object_unref (page);

		if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (query_model), &iter)) {
			g_object_unref (query_model);
			return FALSE;
		}

		do {
			RhythmDBEntry *entry;

			if (first) {
				g_string_append(data, "\r\n");
				first = FALSE;
			}

			entry = rhythmdb_query_model_iter_to_entry (query_model, &iter);
			if (target == TARGET_URIS) {
				g_string_append (data, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
			} else {
				g_string_append_printf (data,
							"%lu",
							rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_ENTRY_ID));
			}

			rhythmdb_entry_unref (entry);

		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (query_model), &iter));

		g_object_unref (query_model);

		gtk_selection_data_set (selection_data,
					selection_data_target,
					8, (guchar *) data->str,
					data->len);

		g_string_free (data, TRUE);
		return TRUE;
	}
	default:
		/* unsupported target */
		return FALSE;
	}
}

static gboolean
rb_display_page_model_drag_data_delete (RbTreeDragSource *drag_source,
					GList *paths)
{
	return TRUE;
}

typedef struct _DisplayPageIter {
	RBDisplayPage *page;
	GtkTreeIter iter;
	gboolean found;
} DisplayPageIter;

static gboolean
match_page_to_iter (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, DisplayPageIter *dpi)
{
	RBDisplayPage *target = NULL;

	gtk_tree_model_get (model, iter, RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &target, -1);

	if (target == dpi->page) {
		dpi->iter = *iter;
		dpi->found = TRUE;
	}

	if (target != NULL) {
		g_object_unref (target);
	}

	return dpi->found;
}

static gboolean
find_in_real_model (RBDisplayPageModel *page_model, RBDisplayPage *page, GtkTreeIter *iter)
{
	GtkTreeModel *model;
	DisplayPageIter dpi = {0, };
	dpi.page = page;

	model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (page_model));
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) match_page_to_iter, &dpi);
	if (dpi.found) {
		*iter = dpi.iter;
		return TRUE;
	} else {
		return FALSE;
	}
}

static int
compare_rows (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, RBDisplayPageModel *page_model)
{
	RBDisplayPage *a_page;
	RBDisplayPage *b_page;
	char *a_name;
	char *b_name;
	int ret;

	gtk_tree_model_get (model, a, RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &a_page, -1);
	gtk_tree_model_get (model, b, RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &b_page, -1);

	g_object_get (a_page, "name", &a_name, NULL);
	g_object_get (b_page, "name", &b_name, NULL);

	if (RB_IS_DISPLAY_PAGE_GROUP (a_page) && RB_IS_DISPLAY_PAGE_GROUP (b_page)) {
		RBDisplayPageGroupCategory a_cat;
		RBDisplayPageGroupCategory b_cat;
		g_object_get (a_page, "category", &a_cat, NULL);
		g_object_get (b_page, "category", &b_cat, NULL);
		if (a_cat < b_cat) {
			ret = -1;
		} else if (a_cat > b_cat) {
			ret = 1;
		} else {
			ret = g_utf8_collate (a_name, b_name);
		}
	} else {
		/* walk up the tree until we find the group, then get its category
		 * to figure out how to sort the pages
		 */
		GtkTreeIter walk_iter;
		GtkTreeIter group_iter;
		RBDisplayPage *group_page;
		RBDisplayPageGroupCategory category;

		walk_iter = *a;
		do {
			group_iter = walk_iter;
		} while (gtk_tree_model_iter_parent (model, &walk_iter, &group_iter));
		gtk_tree_model_get (model, &group_iter, RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &group_page, -1);
		g_object_get (group_page, "category", &category, NULL);
		g_object_unref (group_page);

		/* sort mostly by name */
		switch (category) {
		case RB_DISPLAY_PAGE_GROUP_CATEGORY_FIXED:
			/* fixed sources go in order of appearance */
			ret = -1;
			break;
		case RB_DISPLAY_PAGE_GROUP_CATEGORY_PERSISTENT:
			/* sort auto and static playlists separately */
			if (RB_IS_AUTO_PLAYLIST_SOURCE (a_page)
			    && RB_IS_AUTO_PLAYLIST_SOURCE (b_page)) {
				ret = g_utf8_collate (a_name, b_name);
			} else if (RB_IS_STATIC_PLAYLIST_SOURCE (a_page)
				   && RB_IS_STATIC_PLAYLIST_SOURCE (b_page)) {
				ret = g_utf8_collate (a_name, b_name);
			} else if (RB_IS_AUTO_PLAYLIST_SOURCE (a_page)) {
				ret = -1;
			} else {
				ret = 1;
			}

			break;
		case RB_DISPLAY_PAGE_GROUP_CATEGORY_REMOVABLE:
		case RB_DISPLAY_PAGE_GROUP_CATEGORY_TRANSIENT:
			ret = g_utf8_collate (a_name, b_name);
			break;
		default:
			g_assert_not_reached ();
			break;
		}
	}

	g_object_unref (a_page);
	g_object_unref (b_page);
	g_free (a_name);
	g_free (b_name);

	return ret;
}

static gboolean
rb_display_page_model_is_row_visible (GtkTreeModel *model,
				      GtkTreeIter *iter,
				      RBDisplayPageModel *page_model)
{
	RBDisplayPage *page = NULL;
	gboolean visibility = FALSE;

	gtk_tree_model_get (model, iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (page != NULL) {
		g_object_get (page, "visibility", &visibility, NULL);
		g_object_unref (page);
	}

	return visibility;
}

static void
update_group_visibility_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, RBDisplayPageModel *page_model)
{
	RBDisplayPage *page;

	gtk_tree_model_get (model, iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (RB_IS_DISPLAY_PAGE_GROUP (page)) {
		g_object_set (page, "visibility", gtk_tree_model_iter_has_child (model, iter), NULL);
	}
	g_object_unref (page);
}


/**
 * rb_display_page_model_set_dnd_targets:
 * @page_model: the #RBDisplayPageModel
 * @treeview: the sourcel ist #GtkTreeView
 *
 * Sets up the drag and drop targets for the display page tree.
 */
void
rb_display_page_model_set_dnd_targets (RBDisplayPageModel *display_page_model,
				       GtkTreeView *treeview)
{
	int n_targets = G_N_ELEMENTS (dnd_targets);

	rb_tree_dnd_add_drag_dest_support (treeview,
					   (RB_TREE_DEST_EMPTY_VIEW_DROP | RB_TREE_DEST_SELECT_ON_DRAG_TIMEOUT),
					   dnd_targets, n_targets,
					   GDK_ACTION_LINK);

	rb_tree_dnd_add_drag_source_support (treeview,
					     GDK_BUTTON1_MASK,
					     dnd_targets, n_targets,
					     GDK_ACTION_COPY);
}


static void
page_notify_cb (GObject *object,
		GParamSpec *pspec,
		RBDisplayPageModel *page_model)
{
	RBDisplayPage *page = RB_DISPLAY_PAGE (object);
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreePath *path;

	if (find_in_real_model (page_model, page, &iter) == FALSE) {
		return;
	}

	model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (page_model));
	path = gtk_tree_model_get_path (model, &iter);
	gtk_tree_model_row_changed (model, path, &iter);
	gtk_tree_path_free (path);
}


/**
 * rb_display_page_model_add_page:
 * @page_model: the #RBDisplayPageModel
 * @page: the #RBDisplayPage to add
 * @parent: the parent under which to add @page
 *
 * Adds a page to the model, either below a specified page (if it's a source or
 * something else) or at the top level (if it's a group)
 */
void
rb_display_page_model_add_page (RBDisplayPageModel *page_model, RBDisplayPage *page, RBDisplayPage *parent)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	char *name;

	g_return_if_fail (RB_IS_DISPLAY_PAGE_MODEL (page_model));
	g_return_if_fail (RB_IS_DISPLAY_PAGE (page));

	g_object_get (page, "name", &name, NULL);

	model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (page_model));
	if (parent != NULL) {
		GtkTreeIter parent_iter;

		rb_debug ("inserting source %s with parent %p", name, parent);
		g_assert (find_in_real_model (page_model, parent, &parent_iter));
		gtk_tree_store_append (GTK_TREE_STORE (model), &iter, &parent_iter);
	} else {
		rb_debug ("appending page %s with no parent", name);
		g_object_set (page, "visibility", FALSE, NULL);	/* hide until it has some content */
		gtk_tree_store_append (GTK_TREE_STORE (model), &iter, NULL);
	}
	g_free (name);

	gtk_tree_store_set (GTK_TREE_STORE (model), &iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PLAYING, FALSE,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, page,
			    -1);

	g_signal_connect_object (page, "notify::name", G_CALLBACK (page_notify_cb), page_model, 0);
	g_signal_connect_object (page, "notify::visibility", G_CALLBACK (page_notify_cb), page_model, 0);
	g_signal_connect_object (page, "notify::pixbuf", G_CALLBACK (page_notify_cb), page_model, 0);
}

/**
 * rb_display_page_model_remove_page:
 * @page_model: the #RBDisplayPageModel
 * @page: the #RBDisplayPage to remove
 *
 * Removes a page from the model.
 */
void
rb_display_page_model_remove_page (RBDisplayPageModel *page_model,
				   RBDisplayPage *page)
{
	GtkTreeIter iter;
	GtkTreeModel *model;

	g_assert (find_in_real_model (page_model, page, &iter));

	model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (page_model));

	gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);
	g_signal_handlers_disconnect_by_func (page, G_CALLBACK (page_notify_cb), page_model);
}



/**
 * rb_display_page_model_find_page:
 * @page_model: the #RBDisplayPageModel
 * @page: the #RBDisplayPage to find
 * @iter: returns a #GtkTreeIter for the page
 *
 * Finds a #GtkTreeIter for a specified page in the model.  This will only
 * find pages that are currently visible.  The returned #GtkTreeIter can be used
 * with the #RBDisplayPageModel.
 *
 * Return value: %TRUE if the page was found
 */
gboolean
rb_display_page_model_find_page (RBDisplayPageModel *page_model, RBDisplayPage *page, GtkTreeIter *iter)
{
	DisplayPageIter dpi = {0, };
	dpi.page = page;

	gtk_tree_model_foreach (GTK_TREE_MODEL (page_model), (GtkTreeModelForeachFunc) match_page_to_iter, &dpi);
	if (dpi.found) {
		*iter = dpi.iter;
		return TRUE;
	} else {
		return FALSE;
	}
}

static gboolean
set_playing_flag (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, RBDisplayPage *source)
{
	RBDisplayPage *page;
	gboolean old_playing;

	gtk_tree_model_get (model,
			    iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PLAYING, &old_playing,
			    -1);
	if (RB_IS_SOURCE (page)) {
		gboolean new_playing = (page == source);
		if (old_playing || new_playing) {
			gtk_tree_store_set (GTK_TREE_STORE (model),
					    iter,
					    RB_DISPLAY_PAGE_MODEL_COLUMN_PLAYING, new_playing,
					    -1);
		}
	}
	g_object_unref (page);

	return FALSE;
}

/**
 * rb_display_page_model_set_playing_source:
 * @page_model: the #RBDisplayPageModel
 * @source: the new playing #RBSource (as a #RBDisplayPage)
 *
 * Updates the model with the new playing source.
 */
void
rb_display_page_model_set_playing_source (RBDisplayPageModel *page_model, RBDisplayPage *source)
{
	GtkTreeModel *model;
	model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (page_model));
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) set_playing_flag, source);
}

/**
 * rb_display_page_model_new:
 *
 * This constructs both the GtkTreeStore holding the display page
 * data and the filter model that hides invisible pages.
 *
 * Return value: the #RBDisplayPageModel
 */
RBDisplayPageModel *
rb_display_page_model_new (void)
{
	RBDisplayPageModel *model;
	GtkTreeStore *store;
	GType *column_types = g_new (GType, RB_DISPLAY_PAGE_MODEL_N_COLUMNS);

	column_types[RB_DISPLAY_PAGE_MODEL_COLUMN_PLAYING] = G_TYPE_BOOLEAN;
	column_types[RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE] = RB_TYPE_DISPLAY_PAGE;
	store = gtk_tree_store_newv (RB_DISPLAY_PAGE_MODEL_N_COLUMNS,
				     column_types);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
					 RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE,
					 (GtkTreeIterCompareFunc) compare_rows,
					 NULL, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
					      RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE,
					      GTK_SORT_ASCENDING);

	model = RB_DISPLAY_PAGE_MODEL (g_object_new (RB_TYPE_DISPLAY_PAGE_MODEL,
						     "child-model", store,
						     "virtual-root", NULL,
						     NULL));

	/* hide groups when they're empty */
	g_signal_connect_object (store, "row-has-child-toggled", G_CALLBACK (update_group_visibility_cb), model, 0);
	g_object_unref (store);

	gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (model),
						(GtkTreeModelFilterVisibleFunc) rb_display_page_model_is_row_visible,
						model, NULL);

	g_free (column_types);

	return model;
}


static void
rb_display_page_model_init (RBDisplayPageModel *model)
{
}

static void
rb_display_page_model_finalize (GObject *object)
{
	RBDisplayPageModel *model;

	g_return_if_fail (RB_IS_DISPLAY_PAGE_MODEL (object));
	model = RB_DISPLAY_PAGE_MODEL (object);

	G_OBJECT_CLASS (rb_display_page_model_parent_class)->finalize (object);
}

static void
rb_display_page_model_drag_dest_init (RbTreeDragDestIface *iface)
{
	iface->rb_drag_data_received = rb_display_page_model_drag_data_received;
	iface->rb_row_drop_possible = rb_display_page_model_row_drop_possible;
	iface->rb_row_drop_position = rb_display_page_model_row_drop_position;
	iface->rb_get_drag_target = rb_display_page_model_get_drag_target;
}

static void
rb_display_page_model_drag_source_init (RbTreeDragSourceIface *iface)
{
	iface->rb_row_draggable = rb_display_page_model_row_draggable;
	iface->rb_drag_data_get = rb_display_page_model_drag_data_get;
	iface->rb_drag_data_delete = rb_display_page_model_drag_data_delete;
}

static void
rb_display_page_model_class_init (RBDisplayPageModelClass *klass)
{
	GObjectClass   *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = rb_display_page_model_finalize;

	/**
	 * RBDisplayPageModel::drop-received:
	 * @model: the #RBDisplayPageModel
	 * @target: the #RBSource receiving the drop
	 * @pos: the drop position
	 * @data: the drop data
	 *
	 * Emitted when a drag and drop operation to the display page tree completes.
	 */
	rb_display_page_model_signals[DROP_RECEIVED] =
		g_signal_new ("drop-received",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDisplayPageModelClass, drop_received),
			      NULL, NULL,
			      rb_marshal_VOID__OBJECT_INT_POINTER,
			      G_TYPE_NONE,
			      3,
			      RB_TYPE_DISPLAY_PAGE, G_TYPE_INT, G_TYPE_POINTER);

	if (!drag_target_list) {
		drag_target_list = gtk_target_list_new (dnd_targets, G_N_ELEMENTS (dnd_targets));
	}
}

/**
 * RBDisplayPageModelColumn:
 * @RB_DISPLAY_PAGE_MODEL_COLUMN_PLAYING: TRUE if the page is the playing source
 * @RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE: the #RBDisplayPage object
 * @RB_DISPLAY_PAGE_MODEL_N_COLUMNS: the number of columns
 *
 * Columns present in the display page model.
 */

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_display_page_model_column_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_DISPLAY_PAGE_MODEL_COLUMN_PLAYING, "playing"),
			ENUM_ENTRY (RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, "page"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBDisplayPageModelColumn", values);
	}

	return etype;
}
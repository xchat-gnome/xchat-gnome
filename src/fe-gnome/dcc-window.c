/*
 * dcc-window.c - GtkWindow subclass for managing file transfers
 *
 * Copyright (C) 2004-2007 xchat-gnome team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <config.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include "gui.h"
#include "dcc-window.h"
#include "util.h"
#include "../common/xchat.h"
#include "../common/xchatc.h"
#include "../common/dcc.h"

enum {
	DCC_COLUMN,
	INFO_COLUMN,
	ICON_COLUMN,
	DONE_COLUMN,
	DONE_LABEL_COLUMN,
	TIME_COLUMN,
	N_COLUMNS
};

static GtkWindowClass *parent_class;

static void
dcc_window_class_init (DccWindowClass *klass)
{
}

static void
transfer_selection_changed (GtkTreeSelection *selection, DccWindow *window)
{
	if (gtk_tree_selection_get_selected (selection, NULL, NULL)) {
		gtk_widget_set_sensitive (window->stop_button, TRUE);
	} else {
		gtk_widget_set_sensitive (window->stop_button, FALSE);
	}
}

static void
transfer_stop_clicked (GtkButton *button, DccWindow *window)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	struct DCC *dcc;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->transfer_list));
	if (gtk_tree_selection_get_selected (selection, &model, &iter) == FALSE) {
		return;
	}

	gtk_tree_model_get (model, &iter, DCC_COLUMN, &dcc, -1);
	gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
	dcc_abort (dcc->serv->server_session, dcc);
}

static void
dcc_window_init (DccWindow *window)
{
	gchar *path = locate_data_file ("dcc-window.glade");
	g_assert(path != NULL);

	GtkBuilder *xml = gtk_builder_new ();
	g_assert (gtk_builder_add_from_file ( xml, path, NULL) != 0);

	g_free (path);

#define GW(name) ((window->name) = GTK_WIDGET (gtk_builder_get_object (xml, #name)))
	GW(transfer_list);
	GW(stop_button);
	GW(toplevel);
#undef GW

	g_object_unref (xml);

	gtk_widget_set_sensitive (window->stop_button, FALSE);
	g_signal_connect (G_OBJECT (window->stop_button), "clicked", G_CALLBACK (transfer_stop_clicked), window);

	window->transfer_store = gtk_list_store_new (N_COLUMNS,
	                                             G_TYPE_POINTER,	/* DCC struct */
	                                             G_TYPE_STRING,	/* Info text */
	                                             G_TYPE_ICON,	/* File icon */
	                                             G_TYPE_INT,	/* % done */
	                                             G_TYPE_STRING,	/* % done label */
	                                             G_TYPE_STRING	/* time remaining */
	                                             );

	gtk_tree_view_set_model (GTK_TREE_VIEW (window->transfer_list), GTK_TREE_MODEL (window->transfer_store));

	gtk_container_set_border_width (GTK_CONTAINER (window), 12);
	gtk_widget_reparent (window->toplevel, GTK_WIDGET (window));


	gtk_window_set_default_size (GTK_WINDOW (window), 300, 400);
	gtk_window_set_title (GTK_WINDOW (window), _("File Transfers"));
	g_signal_connect (G_OBJECT (window), "key-press-event", G_CALLBACK (dialog_escape_key_handler_hide), NULL);

	window->progress_column = gtk_tree_view_column_new ();
	window->info_column = gtk_tree_view_column_new ();
	window->remaining_column = gtk_tree_view_column_new ();

	window->progress_cell = gtk_cell_renderer_progress_new ();
	gtk_tree_view_column_pack_start (window->progress_column, window->progress_cell, TRUE);
	gtk_tree_view_column_add_attribute (window->progress_column, window->progress_cell, "value", DONE_COLUMN);
	gtk_tree_view_column_add_attribute (window->progress_column, window->progress_cell, "text", DONE_LABEL_COLUMN);

	window->icon_cell = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (window->info_column, window->icon_cell, FALSE);
	gtk_tree_view_column_add_attribute (window->info_column, window->icon_cell, "gicon", ICON_COLUMN);

	window->info_cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (window->info_column, window->info_cell, TRUE);
	gtk_tree_view_column_add_attribute (window->info_column, window->info_cell, "markup", INFO_COLUMN);

	window->remaining_cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (window->remaining_column, window->remaining_cell, TRUE);
	gtk_tree_view_column_add_attribute (window->remaining_column, window->remaining_cell, "text", TIME_COLUMN);

	/* File completion percent */
	gtk_tree_view_column_set_title (window->progress_column, _("%"));
	gtk_tree_view_column_set_title (window->info_column, _("File"));
	gtk_tree_view_column_set_title (window->remaining_column, _("Remaining"));

	gtk_tree_view_column_set_min_width (window->progress_column, 75);

	gtk_tree_view_column_set_expand (window->progress_column, FALSE);
	gtk_tree_view_column_set_expand (window->info_column, TRUE);
	gtk_tree_view_column_set_expand (window->remaining_column, FALSE);

	gtk_tree_view_append_column (GTK_TREE_VIEW (window->transfer_list), window->progress_column);
	gtk_tree_view_append_column (GTK_TREE_VIEW (window->transfer_list), window->info_column);
	gtk_tree_view_append_column (GTK_TREE_VIEW (window->transfer_list), window->remaining_column);

	g_signal_connect (
		G_OBJECT (gtk_tree_view_get_selection (
			GTK_TREE_VIEW (window->transfer_list))),
		"changed",
		G_CALLBACK (transfer_selection_changed),
		window);
}

GType
dcc_window_get_type (void)
{
	static GType dcc_window_type = 0;
	if (!dcc_window_type) {
		static const GTypeInfo dcc_window_info = {
			sizeof (DccWindowClass),
			NULL, NULL,
			(GClassInitFunc) dcc_window_class_init,
			NULL, NULL,
			sizeof (DccWindow),
			0,
			(GInstanceInitFunc) dcc_window_init,
		};

		dcc_window_type = g_type_register_static (GTK_TYPE_WINDOW, "DccWindow", &dcc_window_info, 0);

		parent_class = g_type_class_ref (GTK_TYPE_WINDOW);
	}

	return dcc_window_type;
}

DccWindow *
dcc_window_new (void)
{
	DccWindow *window = g_object_new (dcc_window_get_type (), 0);
	if (window->toplevel == NULL) {
		g_object_unref (window);
		return NULL;
	}

	g_signal_connect (G_OBJECT (window), "delete-event", G_CALLBACK (gtk_widget_hide_on_delete), NULL);

	return window;
}

void
dcc_window_add (DccWindow *window, struct DCC *dcc)
{
	GtkTreeIter iter;
	gint done = (gint) ((((float)dcc->pos) / ((float) dcc->size)) * 100);
	gchar *done_text, *info_text;
	char *size, *pos;

	/* If this is a recieve and auto-accept isn't turned on, pop up a
	 * confirmation dialog */
	if ((dcc->type == TYPE_RECV) && (dcc->dccstat == STAT_QUEUED) && (prefs.autodccsend != 1)) {
		GtkWidget *dialog;
		gint response;

		dialog = gtk_message_dialog_new (GTK_WINDOW (window),
						 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_QUESTION,
						 GTK_BUTTONS_CANCEL,
						 _("Incoming File Transfer"));
		gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Accept"), GTK_RESPONSE_ACCEPT);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), _("%s is attempting to send you a file named \"%s\". Do you wish to accept the transfer?"), dcc->nick, dcc->file);

		response = gtk_dialog_run (GTK_DIALOG (dialog));
		if (response == GTK_RESPONSE_ACCEPT) {
			/* FIXME: resume? */
			dcc_get (dcc);

			/* pop up transfers window if it isn't already shown */
			gtk_window_present (GTK_WINDOW (window));
		} else {
			dcc_abort (dcc->serv->server_session, dcc);
		}
		gtk_widget_destroy (dialog);

		return;
	}

	done_text = g_strdup_printf ("%d %%", done);
	size = g_format_size (dcc->size);
	pos = g_format_size (dcc->pos);
	info_text = g_strdup_printf (_("<b>%s</b>\n<small>from %s</small>\n%s of %s"), dcc->file, dcc->nick, pos, size);
	g_free (size);
	g_free (pos);

	gtk_list_store_prepend (window->transfer_store, &iter);
	gtk_list_store_set (window->transfer_store, &iter,
	                    DCC_COLUMN, dcc,
	                    INFO_COLUMN, info_text,
	                    ICON_COLUMN, NULL,
	                    DONE_COLUMN, done,
	                    DONE_LABEL_COLUMN, done_text,
	                    TIME_COLUMN, _("starting"),
	                    -1);

	g_free (done_text);
	g_free (info_text);
}

void
dcc_window_update (DccWindow *window, struct DCC *dcc)
{
	GtkTreeIter iter;

	/* DCC chats cause crashes here, because they're not handled in this window. */
	if (dcc->type == 2) {
		return;
	}

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (window->transfer_store), &iter)) {
		do {
			gpointer ptr;
			gpointer icon;
			gtk_tree_model_get (GTK_TREE_MODEL (window->transfer_store), &iter, DCC_COLUMN, &ptr, ICON_COLUMN, &icon, -1);
			if (ptr == dcc) {
				gint done = (gint) ((((float)dcc->pos) / ((float) dcc->size)) * 100);
				gchar *done_text = g_strdup_printf ("%d %%", done);
				gchar *size = g_format_size (dcc->size);
				gchar *pos = g_format_size (dcc->pos);
				gchar *speed = g_format_size (dcc->cps);
				gchar *info_text;
				gchar *remaining_text;

				info_text = g_strdup_printf (_("<b>%s</b>\n<small>from %s</small>\n%s of %s at %s/s"), dcc->file, dcc->nick, pos, size, speed);

				g_free (size);
				g_free (pos);
				g_free (speed);

				if (dcc->dccstat == STAT_QUEUED) {
					remaining_text = g_strdup (_("queued"));
				} else if (dcc->dccstat == STAT_FAILED) {
					gchar *message;

					if (dcc->type == TYPE_SEND)
						message = g_strdup_printf (_("Transfer of %s to %s failed"), dcc->file, dcc->nick);
					else
						message = g_strdup_printf (_("Transfer of %s from %s failed"), dcc->file, dcc->nick);
					error_dialog (_("Transfer failed"), message);
					g_free (message);
					gtk_list_store_remove (window->transfer_store, &iter);
					return;
				} else if (dcc->dccstat == STAT_DONE) {
					gtk_list_store_remove (window->transfer_store, &iter);
					return;
				} else if (dcc->dccstat == STAT_ABORTED) {
					remaining_text = g_strdup (_("aborted"));
				} else {
					if (dcc->cps == 0) {
						remaining_text = g_strdup (_("stalled"));
					} else {
						int eta = (dcc->size - dcc->pos) / dcc->cps;
						if (eta > 3600)
							remaining_text = g_strdup_printf (_("%.2d:%.2d:%.2d"), eta / 3600, (eta / 60) % 60, eta % 60);
						else
							remaining_text = g_strdup_printf (_("%.2d:%.2d"), eta / 60, eta % 60);
					}
				}

				gtk_list_store_set (window->transfer_store, &iter,
				                    INFO_COLUMN, info_text,
				                    DONE_COLUMN, done,
				                    DONE_LABEL_COLUMN, done_text,
				                    TIME_COLUMN, remaining_text,
				                    -1);
				g_free (done_text);
				g_free (info_text);
				g_free (remaining_text);

				/* We put off rendering the icon until we get at least one update,
				 * to ensure that can determine the MIME type
				 */
				if (dcc->destfile != NULL && icon == NULL) {
					GFile *file;
					GFileInfo *info;
					GIcon *mime_icon, *emblemed_icon, *direction_icon;
					GEmblem *direction_emblem;

					/* If the file doesn't exist yet, don't do the icon */
					if (g_file_test (dcc->destfile, G_FILE_TEST_EXISTS) == FALSE) {
						return;
					}

					file = g_file_new_for_path (dcc->destfile);
					info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_ICON,
								  0, NULL, NULL);
					g_object_unref (file);

					if (!info) {
						return;
					}

					mime_icon = g_file_info_get_icon (info);

					/* Composite on an emblem which shows the direction of transfer */
					if (dcc->type == 1) {
						direction_icon = g_themed_icon_new ("go-down");
					} else {
						direction_icon = g_themed_icon_new ("go-up");
					}

					direction_emblem = g_emblem_new (direction_icon);
					emblemed_icon = g_emblemed_icon_new (mime_icon, direction_emblem);

					gtk_list_store_set (window->transfer_store, &iter, ICON_COLUMN, emblemed_icon, -1);

					g_object_unref (mime_icon);
					g_object_unref (emblemed_icon);
					g_object_unref (direction_emblem);
					g_object_unref (direction_icon);

					g_object_unref (info);
				}

				/// don't leak icon
				return;
			}
			// dont' leak icon
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (window->transfer_store), &iter));
	}

	/*
	 * If we're here, it means we didn't find the entry. Probably
	 * because we asked the user for confirmation. If we're actually
	 * transferring the file, add it now, then perform an update
	 */
	if (dcc->dccstat == 1) {
		dcc_window_add (window, dcc);
		dcc_window_update (window, dcc);
	}
}

void
dcc_window_remove (DccWindow *window, struct DCC *dcc)
{
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (window->transfer_store), &iter) == FALSE) {
		return;
	}

	do {
		gpointer ptr;
		gtk_tree_model_get (GTK_TREE_MODEL (window->transfer_store), &iter, DCC_COLUMN, &ptr, -1);
		if (ptr == dcc) {
			gtk_list_store_remove (window->transfer_store, &iter);
			break;
		}
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (window->transfer_store), &iter));
}

void
dcc_send_file (struct User *user)
{
	GtkWidget *dialog;
	GtkResponseType response;

	if (user->nick == NULL)
		return;

	dialog = gtk_file_chooser_dialog_new (_("Send File..."),
	                GTK_WINDOW (gui.main_window),
	                GTK_FILE_CHOOSER_ACTION_OPEN,
	                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
	                NULL);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_hide (dialog);

	if (response == GTK_RESPONSE_ACCEPT) {
		gchar *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		dcc_send (gui.current_session, user->nick, filename, 0, FALSE);
		g_free (filename);
	}

	gtk_widget_destroy (dialog);
}

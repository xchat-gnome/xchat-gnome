/*
 * connect-dialog.c - utilities for displaying the connect dialog
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

#include "connect-dialog.h"
#include "../common/servlist.h"
#include "../common/xchat.h"
#include "../common/xchatc.h"
#include "gui.h"
#include "navigation-tree.h"
#include "util.h"
#include <config.h>
#include <glib/gi18n.h>

static GtkDialogClass *parent_class;

static void connect_dialog_finalize(GObject *object)
{
        ConnectDialog *dialog = (ConnectDialog *)object;

        if (dialog->server_store) {
                g_object_unref(dialog->server_store);
                dialog->server_store = NULL;
        }

        ((GObjectClass *)parent_class)->finalize(object);
}

static void connect_dialog_class_init(ConnectDialogClass *klass)
{
        GObjectClass *object_class = (GObjectClass *)klass;
        object_class->finalize = connect_dialog_finalize;
}

static void selection_changed(GtkTreeSelection *select, ConnectDialog *dialog)
{
        if (gtk_tree_selection_get_selected(select, NULL, NULL)) {
                gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, TRUE);
        } else {
                gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, FALSE);
        }
}

static void dialog_response(ConnectDialog *dialog, gint response, gpointer data)
{
        if (response != GTK_RESPONSE_OK) {
                gtk_widget_destroy(GTK_WIDGET(dialog));
                return;
        }

        GtkTreeModel *model;
        GtkTreeIter iter;

        GtkTreeSelection *select = gtk_tree_view_get_selection(GTK_TREE_VIEW(dialog->server_list));
        if (gtk_tree_selection_get_selected(select, &model, &iter)) {
                session *s = NULL;
                ircnet *net;

                // If the currently selected server is not connected, use it
                // for the new session.
                if (gui.current_session && (gui.current_session->server == NULL ||
                                            gui.current_session->server->connected == FALSE)) {
                        s = gui.current_session;
                }

                // Make sure that this network isn't already connected.
                gtk_tree_model_get(model, &iter, 1, &net, -1);
                gboolean found = FALSE;
                for (GSList *i = sess_list; i; i = g_slist_next(i)) {
                        session *sess = (session *)(i->data);
                        if (sess->type == SESS_SERVER && sess->server != NULL &&
                            sess->server->network == net &&
                            (sess->server->connected || sess->server->connecting)) {
                                found = TRUE;
                                break;
                        }
                }

                gtk_widget_destroy(GTK_WIDGET(dialog));

                if (net->servlist == NULL) {
                        GtkWidget *warning_dialog = gtk_message_dialog_new(
                            NULL,
                            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                            GTK_MESSAGE_WARNING,
                            GTK_BUTTONS_CLOSE,
                            _("This network doesn't have a server defined."));
                        gtk_message_dialog_format_secondary_text(
                            GTK_MESSAGE_DIALOG(warning_dialog),
                            _("Please add at least one server to the %s network."),
                            net->name);
                        gtk_dialog_run(GTK_DIALOG(warning_dialog));
                        gtk_widget_destroy(warning_dialog);

                } else if (!found) {
                        servlist_connect(s, net, TRUE);
                }
        }
}

static void row_activated(GtkTreeView *widget, GtkTreePath *path, GtkTreeViewColumn *column,
                          ConnectDialog *dialog)
{
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(dialog->server_store), &iter, path)) {
                gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        }
}

static void connect_dialog_init(ConnectDialog *dialog)
{
        gchar *path = locate_data_file("connect-dialog.glade");
        g_assert(path != NULL);

        GtkBuilder *xml = gtk_builder_new();
        g_assert(gtk_builder_add_from_file(xml, path, NULL) != 0);

        g_free(path);

#define GW(name) ((dialog->name) = GTK_WIDGET(gtk_builder_get_object(xml, #name)))
        GW(toplevel);
        GW(server_list);
#undef GW

        g_object_unref(xml);

        dialog->server_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
        gtk_tree_view_set_model(GTK_TREE_VIEW(dialog->server_list),
                                GTK_TREE_MODEL(dialog->server_store));

        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column =
            gtk_tree_view_column_new_with_attributes("name", renderer, "text", 0, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(dialog->server_list), column);

        GtkWidget *button = gtk_button_new_with_mnemonic(_("C_onnect"));
        gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
        gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, GTK_RESPONSE_OK);
        gtk_container_set_border_width(GTK_CONTAINER(dialog), 6);

        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_widget_reparent(dialog->toplevel, content_area);

        g_signal_connect(G_OBJECT(dialog->server_list),
                         "row-activated",
                         G_CALLBACK(row_activated),
                         dialog);

        g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(dialog->server_list))),
                         "changed",
                         G_CALLBACK(selection_changed),
                         dialog);

        for (GSList *netlist = network_list; netlist; netlist = g_slist_next(netlist)) {
                GtkTreeIter iter;

                ircnet *net = netlist->data;
                gtk_list_store_append(dialog->server_store, &iter);
                gtk_list_store_set(dialog->server_store, &iter, 0, net->name, 1, net, -1);
        }

        gtk_window_set_default_size(GTK_WINDOW(dialog), 320, 240);
        gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
        gtk_window_set_title(GTK_WINDOW(dialog), _("Connect"));
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, FALSE);
        g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(dialog_response), NULL);
        g_signal_connect(G_OBJECT(dialog),
                         "key-press-event",
                         G_CALLBACK(dialog_escape_key_handler_destroy),
                         NULL);
}

GType connect_dialog_get_type(void)
{
        static GType connect_dialog_type = 0;
        if (!connect_dialog_type) {
                static const GTypeInfo connect_dialog_info = {
                        sizeof(ConnectDialogClass),
                        NULL,
                        NULL,
                        (GClassInitFunc)connect_dialog_class_init,
                        NULL,
                        NULL,
                        sizeof(ConnectDialog),
                        0,
                        (GInstanceInitFunc)connect_dialog_init,
                };
                connect_dialog_type = g_type_register_static(GTK_TYPE_DIALOG,
                                                             "ConnectDialog",
                                                             &connect_dialog_info,
                                                             0);

                parent_class = g_type_class_ref(GTK_TYPE_DIALOG);
        }

        return connect_dialog_type;
}

ConnectDialog *connect_dialog_new(void)
{
        return g_object_new(CONNECT_DIALOG_TYPE, 0);
}

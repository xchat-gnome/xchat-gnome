/*
 * setup-dialog.c - Initial setup dialog for first-time users
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

#include "setup-dialog.h"
#include "preferences.h"
#include "util.h"
#include <config.h>
#include <gconf/gconf-client.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <string.h>

static GtkWidget *real_entry = NULL;
static GtkWidget *nick_entry = NULL;
static GtkWidget *ok_button = NULL;
static gboolean done;

static void ok_clicked(GtkButton *button, gpointer data);
static void entry_changed(GtkEditable *entry, gpointer user_data);

void run_setup_dialog(void)
{
        gchar *path = locate_data_file("setup-dialog.glade");
        g_assert(path != NULL);

        GtkBuilder *xml = gtk_builder_new();
        g_assert(gtk_builder_add_from_file(xml, path, NULL) != 0);

        g_free(path);

        GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(xml, "setup window"));

        GtkSizeGroup *group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
        nick_entry = GTK_WIDGET(gtk_builder_get_object(xml, "nick name entry"));
        real_entry = GTK_WIDGET(gtk_builder_get_object(xml, "real name entry"));
        gtk_size_group_add_widget(group, nick_entry);
        gtk_size_group_add_widget(group, real_entry);
        g_signal_connect(G_OBJECT(nick_entry), "changed", G_CALLBACK(entry_changed), NULL);
        g_signal_connect(G_OBJECT(real_entry), "changed", G_CALLBACK(entry_changed), NULL);
        g_object_unref(group);

        gtk_entry_set_text(GTK_ENTRY(nick_entry), g_get_user_name());
        gtk_entry_set_text(GTK_ENTRY(real_entry), g_get_real_name());
        gtk_widget_grab_focus(nick_entry);

        ok_button = GTK_WIDGET(gtk_builder_get_object(xml, "ok button"));
        g_signal_connect(G_OBJECT(ok_button), "clicked", G_CALLBACK(ok_clicked), NULL);

        gtk_widget_show_all(window);
        done = FALSE;
        while (!done) {
                g_main_context_iteration(NULL, TRUE);
        }

        g_object_unref(xml);
        gtk_widget_destroy(window);
}

static void ok_clicked(GtkButton *button, gpointer data)
{
        const gchar *nick = gtk_entry_get_text(GTK_ENTRY(nick_entry));
        const gchar *real = gtk_entry_get_text(GTK_ENTRY(real_entry));

        GConfClient *client = gconf_client_get_default();

        gconf_client_set_string(client, "/apps/xchat/irc/nickname", nick, NULL);
        gconf_client_set_string(client, "/apps/xchat/irc/realname", real, NULL);

        gconf_client_set_string(client, "/apps/xchat/version", PACKAGE_VERSION, NULL);

        g_object_unref(client);

        /*
         * We set the alternative nicknames here, so that people editing the
         * config file can override them.
         */
        set_nickname(nick);

        done = TRUE;
}

static void entry_changed(GtkEditable *entry, gpointer user_data)
{
        const gchar *nick = gtk_entry_get_text(GTK_ENTRY(nick_entry));
        const gchar *real = gtk_entry_get_text(GTK_ENTRY(real_entry));

        if ((nick == NULL || strlen(nick) == 0) || (real == NULL || strlen(real) == 0)) {
                gtk_widget_set_sensitive(ok_button, FALSE);
        } else {
                gtk_widget_set_sensitive(ok_button, TRUE);
        }
}

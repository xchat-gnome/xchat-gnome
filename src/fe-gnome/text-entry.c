/*
 * textentry.c - Widget encapsulating the text entry
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
#include "text-entry.h"
#include "../common/outbound.h"
#include "../common/xchatc.h"
#include "conversation-panel.h"
#include "gui.h"
#include "palette.h"
#include "userlist.h"
#include <config.h>
#include <gconf/gconf-client.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <string.h>

static void text_entry_class_init(TextEntryClass *klass);
static void text_entry_init(TextEntry *entry);
static void text_entry_finalize(GObject *object);
static gboolean text_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data);
static void text_entry_activate(GtkWidget *widget, gpointer data);
static void text_entry_history_up(GtkEntry *entry);
static void text_entry_history_down(GtkEntry *entry);
static void text_entry_populate_popup(GtkEntry *entry, GtkMenu *menu, gpointer data);
static GtkWidget *get_color_icon(int c, GtkStyle *style);
static void color_code_activate(GtkMenuItem *item, gpointer data);

static GtkEntryClass *parent_class = NULL;
G_DEFINE_TYPE(TextEntry, text_entry, GTK_TYPE_ENTRY);

struct _TextEntryPriv {
        GHashTable *current_text;
        struct session *session;
};

static void text_entry_class_init(TextEntryClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

        parent_class = g_type_class_peek_parent(klass);

        gobject_class->finalize = text_entry_finalize;
}

static void text_entry_init(TextEntry *entry)
{
        g_signal_connect_after(G_OBJECT(entry),
                               "key_press_event",
                               G_CALLBACK(text_entry_key_press),
                               NULL);
        g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(text_entry_activate), NULL);
        g_signal_connect(G_OBJECT(entry),
                         "populate-popup",
                         G_CALLBACK(text_entry_populate_popup),
                         NULL);

        entry->priv = g_new0(TextEntryPriv, 1);

        /* Save the current input for each session */
        entry->priv->current_text = g_hash_table_new(NULL, NULL);

        gtk_widget_show(GTK_WIDGET(entry));
}

static void text_entry_finalize(GObject *object)
{
        TextEntry *entry;

        entry = TEXT_ENTRY(object);

        if (entry->priv) {
                g_free(entry->priv);
        }

        G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean text_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
        if (event->state & gtk_accelerator_get_default_mod_mask())
                return FALSE;

        switch (event->keyval) {
        case GDK_KEY_Down:
                text_entry_history_down(GTK_ENTRY(widget));
                return TRUE;
        case GDK_KEY_Up:
                text_entry_history_up(GTK_ENTRY(widget));
                return TRUE;
        case GDK_KEY_Tab:
                return TRUE;
        default:
                return FALSE;
        }
}

static void text_entry_activate(GtkWidget *widget, gpointer data)
{
        char *entry_text = g_strdup(gtk_entry_get_text(GTK_ENTRY(widget)));
        gtk_entry_set_text(GTK_ENTRY(widget), "");
        if (TEXT_ENTRY(widget)->priv->session != NULL) {
                handle_multiline(TEXT_ENTRY(widget)->priv->session,
                                 (char *)entry_text,
                                 TRUE,
                                 FALSE);
        }
        g_free(entry_text);
}

static void text_entry_history_up(GtkEntry *entry)
{
        TextEntry *text_entry;
        char *new_line;

        text_entry = TEXT_ENTRY(entry);
        if (text_entry->priv->session == NULL) {
                return;
        }

        new_line =
            history_up(&(text_entry->priv->session->history), (char *)gtk_entry_get_text(entry));
        if (new_line) {
                gtk_entry_set_text(entry, new_line);
                gtk_editable_set_position(GTK_EDITABLE(entry), -1);
        }
}

static void text_entry_history_down(GtkEntry *entry)
{
        TextEntry *text_entry;
        char *new_line;

        text_entry = TEXT_ENTRY(entry);
        if (text_entry->priv->session == NULL) {
                return;
        }

        new_line = history_down(&(text_entry->priv->session->history));
        if (new_line) {
                gtk_entry_set_text(entry, new_line);
                gtk_editable_set_position(GTK_EDITABLE(entry), -1);
        }
}

static void text_entry_populate_popup(GtkEntry *entry, GtkMenu *menu, gpointer data)
{
        GtkWidget *item;
        GtkWidget *submenu;

        item = gtk_menu_item_new_with_mnemonic(_("I_nsert Color Code"));
        gtk_widget_show(item);

        submenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_image_menu_item_new_with_label(_("Black"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(1, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(1));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Dark Blue"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(2, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(2));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Dark Green"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(3, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(3));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Red"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(4, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(4));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Brown"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(5, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(5));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Purple"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(6, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(6));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Orange"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(7, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(7));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Yellow"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(8, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(8));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Light Green"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(9, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(9));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Aqua"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(10, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(10));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Light Blue"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(11, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(11));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Blue"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(12, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(12));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Violet"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(13, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(13));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Grey"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(14, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(14));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("Light Grey"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(15, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(15));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
        item = gtk_image_menu_item_new_with_label(_("White"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      get_color_icon(0, gtk_widget_get_style(item)));
        g_signal_connect(G_OBJECT(item),
                         "activate",
                         G_CALLBACK(color_code_activate),
                         GINT_TO_POINTER(0));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);

        gtk_widget_show_all(submenu);
}

static GtkWidget *get_color_icon(int c, GtkStyle *style)
{
        GtkWidget *icon;
        GdkPixbuf *pixbuf;
        cairo_surface_t *canvas;
        cairo_t *cr;

        canvas = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
        cr = cairo_create(canvas);

        gdk_cairo_set_source_color(cr, &style->dark[GTK_STATE_NORMAL]);
        cairo_rectangle(cr, 0, 0, 16, 16);
        cairo_fill(cr);

        gdk_cairo_set_source_color(cr, &colors[c]);
        cairo_rectangle(cr, 1, 1, 14, 14);
        cairo_fill(cr);

        cairo_destroy(cr);

        pixbuf = gdk_pixbuf_get_from_surface(canvas, 0, 0, 16, 16);
        icon = gtk_image_new_from_pixbuf(pixbuf);

        cairo_surface_destroy(canvas);
        g_object_unref(pixbuf);

        return icon;
}

static void color_code_activate(GtkMenuItem *item, gpointer data)
{
        int color = GPOINTER_TO_INT(data);
        char *code = g_strdup_printf("\003%d", color);
        int position = gtk_editable_get_position(GTK_EDITABLE(gui.text_entry));
        gtk_editable_insert_text(GTK_EDITABLE(gui.text_entry), code, strlen(code), &position);
        gtk_editable_set_position(GTK_EDITABLE(gui.text_entry), position + strlen(code));
        g_free(code);
}

GtkWidget *text_entry_new(void)
{
        return GTK_WIDGET(g_object_new(text_entry_get_type(), NULL));
}

void text_entry_set_current(TextEntry *entry, struct session *sess)
{
        TextEntryPriv *priv = entry->priv;
        GtkWidget *widget = GTK_WIDGET(entry);
        GtkClipboard *clipboard;
        char *selection = NULL;
        char *text = NULL;
        int start, end;

        g_return_if_fail(gtk_widget_get_realized(widget));

        if (sess == priv->session) {
                return;
        }

        /* If the entry owns PRIMARY, setting the new text will clear PRIMARY;
         * so we need to re-set PRIMARY after setting the text.
         * See bug #345356 and bug #347067.
         */

        clipboard = gtk_widget_get_clipboard(widget, GDK_SELECTION_PRIMARY);
        g_assert(clipboard != NULL);

        if (gtk_clipboard_get_owner(clipboard) == G_OBJECT(entry) &&
            gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &start, &end)) {
                selection = gtk_editable_get_chars(GTK_EDITABLE(entry), start, end);
        }

        if (priv->session) {
                g_hash_table_replace(priv->current_text,
                                     priv->session,
                                     g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
        }

        priv->session = sess;

        if (priv->session) {
                text = g_hash_table_lookup(priv->current_text, priv->session);
        }

        gtk_entry_set_text(GTK_ENTRY(entry), text ? text : "");
        gtk_editable_set_position(GTK_EDITABLE(entry), -1);

        /* Restore the selection (note that it's not owned by us anymore!) */
        if (selection) {
                gtk_clipboard_set_text(clipboard, selection, strlen(selection));
                g_free(selection);
        }
}

void text_entry_remove_session(TextEntry *entry, struct session *sess)
{
        g_hash_table_remove(entry->priv->current_text, sess);
        if (sess == entry->priv->session) {
                gtk_entry_set_text(GTK_ENTRY(entry), "");
        }
}

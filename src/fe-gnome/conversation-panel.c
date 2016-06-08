/*
 * conversation-panel.c - Widget encapsulating the conversation panel
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
#include <string.h>

#include <gtk/gtk.h>
#ifdef WIN32
#include <gdk/gdkwin32.h>
#include <time.h>
#else
#include <gdk/gdkx.h>
#endif
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include "conversation-panel.h"
#include "gui.h"
#include "palette.h"
#include "userlist-gui.h"
#include "util.h"
#include "xtext.h"

#include "../common/fe.h"
#include "../common/outbound.h"
#include "../common/text.h"
#include "../common/url.h"
#include "../common/userlist.h"
#include "../common/util.h"
#include "../common/xchatc.h"

typedef struct _fe_lastlog_info fe_lastlog_info;

static void conversation_panel_class_init(ConversationPanelClass *klass);
static void conversation_panel_init(ConversationPanel *panel);
static void conversation_panel_finalize(GObject *object);
static void conversation_panel_realize(GtkWidget *widget);
static int conversation_panel_check_word(GtkWidget *xtext,
                                         char *word,
                                         int len);
static void conversation_panel_clicked_word(GtkWidget *xtext,
                                            char *word,
                                            GdkEventButton *event,
                                            ConversationPanel *panel);
static void conversation_panel_enter_word(GtkWidget *xtext,
                                          char *word,
                                          ConversationPanel *panel);
static void conversation_panel_leave_word(GtkWidget *xtext,
                                          char *word,
                                          ConversationPanel *panel);
static gboolean conversation_panel_lost_focus(GtkWidget *widget,
                                              GdkEventFocus *event,
                                              ConversationPanel *panel);
static void conversation_panel_set_font(ConversationPanel *panel);
static void conversation_panel_font_changed(GConfClient *client,
                                            guint cnxn_id,
                                            GConfEntry *entry,
                                            ConversationPanel *panel);
static void     conversation_panel_set_background     (ConversationPanel      *panel);
static void     conversation_panel_background_changed (GConfClient            *client,
                                                       guint                   cnxn_id,
                                                       GConfEntry             *entry,
                                                       ConversationPanel      *panel);
static gboolean conversation_panel_query_tooltip      (GtkWidget  *widget,
                                                       gint        x,
                                                       gint        y,
                                                       gboolean    keyboard_tooltip,
                                                       GtkTooltip *tooltip);

static void     timestamps_changed                    (GConfClient  *client,
                                                       guint         cnxn_id,
                                                       GConfEntry   *entry,
                                                       xtext_buffer *buffer);

static void     redundant_nickstamp_changed           (GConfClient       *client,
                                                       guint              cnxn_id,
                                                       GConfEntry        *entry,
                                                       ConversationPanel *panel);
static void     conversation_panel_print_line         (ConversationPanel      *panel,
                                                       xtext_buffer           *buffer,
                                                       char                   *text,
                                                       int                     len,
                                                       gboolean                indent,
                                                       time_t                  timet);
static void     conversation_panel_lastlog_foreach    (GtkXText               *xtext,
                                                       char                   *text,
                                                       fe_lastlog_info        *info);

static void     open_url                              (GtkAction              *action,
                                                       ConversationPanel      *panel);
static void     copy_text                             (GtkAction              *action,
                                                       ConversationPanel      *panel);
static void     send_email                            (GtkAction              *action,
                                                       ConversationPanel      *panel);
static void     drop_send_files                       (GtkAction              *action,
                                                       ConversationPanel      *panel);
static void     drop_paste_file                       (GtkAction              *action,
                                                       ConversationPanel      *panel);
static void     drop_paste_filename                   (GtkAction              *action,
                                                       ConversationPanel      *panel);
static void     drop_cancel                           (GtkAction              *action,
                                                       ConversationPanel      *panel);
static void     style_set_callback                    (GtkWidget              *widget,
                                                       GtkStyle               *previous_style,
                                                       void                   *data);
static void     drag_data_received                    (GtkWidget              *widget,
                                                       GdkDragContext         *context,
                                                       gint                    x,
                                                       gint                    y,
                                                       GtkSelectionData       *selection_data,
                                                       guint                   info,
                                                       guint                   time,
                                                       ConversationPanel      *panel);
static void     free_dropped_files                    (ConversationPanel      *panel);
static void     drop_send                             (ConversationPanel      *panel);
static void     drop_paste                            (ConversationPanel      *panel);
static void     send_file                             (gpointer                file,
                                                       gpointer                user_data);
static void     on_default_copy_activate              (GtkAction              *action,
                                                       ConversationPanel      *panel);
static gboolean uri_is_text                           (gchar                  *uri);
static gboolean check_file_size                       (gchar                  *uri);
GtkWidget* get_user_vbox_infos                        (struct User            *user);

struct _fe_lastlog_info
{
	ConversationPanel *panel;
	struct session    *sess;
	guchar            *sstr;
};

struct _ConversationPanelPriv
{
	GtkWidget      *scrollbar;
	GtkWidget      *xtext;

	struct session *current;

	GHashTable     *buffers;
	GHashTable     *timestamp_notifies;

	gchar          *selected_word;
	GSList         *dropped_files;

	gchar          *tooltip_nick;
	gboolean        redundant_nickstamps;
};

#define STOCK_MAIL_SEND "mail-send"

static GtkActionEntry url_actions[] = {
	{ "TextURLOpen", GTK_STOCK_OPEN, N_("_Open Link in Browser"), NULL, NULL, G_CALLBACK (open_url) },
	{ "TextURLCopy", GTK_STOCK_COPY, N_("_Copy Link Location"),   NULL, NULL, G_CALLBACK (copy_text) },
};

static GtkActionEntry email_actions[] = {
	{ "TextEmailSend", STOCK_MAIL_SEND, N_("Se_nd Message To..."), NULL, NULL, G_CALLBACK (send_email) },
        { "TextEmailCopy", GTK_STOCK_COPY,   N_("_Copy Address"),       NULL, NULL, G_CALLBACK (copy_text) },
};

static GtkActionEntry dnd_actions[] = {
	{ "DropSendFiles",     NULL, N_("_Send File"),           NULL, NULL, G_CALLBACK (drop_send_files) },
	{ "DropPasteFile",     NULL, N_("Paste File _Contents"), NULL, NULL, G_CALLBACK (drop_paste_file) },
	{ "DropPasteFileName", NULL, N_("Paste File_name"),      NULL, NULL, G_CALLBACK (drop_paste_filename) },
	{ "DropCancel",        NULL, N_("_Cancel"),              NULL, NULL, G_CALLBACK (drop_cancel) },
};

static GtkActionEntry default_actions[] = {
	{ "DefaultCopy",     GTK_STOCK_COPY, N_("_Copy"),        NULL, NULL, G_CALLBACK (on_default_copy_activate) },
};

#define DROP_FILE_PASTE_MAX_SIZE 1024

static struct User dialog_user;

static GtkHBoxClass *parent_class;
G_DEFINE_TYPE (ConversationPanel, conversation_panel, GTK_TYPE_HBOX);

static void
conversation_panel_class_init (ConversationPanelClass *klass)
{
	GObjectClass   *gobject_class;
	GtkWidgetClass *widget_class;

	parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = conversation_panel_finalize;

	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->realize = conversation_panel_realize;
	widget_class->query_tooltip = conversation_panel_query_tooltip;
}

static void
conversation_panel_init (ConversationPanel *panel)
{
	GtkWidget *frame;
	GtkActionGroup *action_group;
	GConfClient       *client;

	panel->priv = g_new0 (ConversationPanelPriv, 1);
	panel->priv->xtext     = gtk_xtext_new (colors, prefs.indent_nicks);
	panel->priv->scrollbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, GTK_XTEXT (panel->priv->xtext)->adj);
	frame                  = gtk_frame_new (NULL);

	panel->priv->buffers            = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) gtk_xtext_buffer_free);
	panel->priv->timestamp_notifies = g_hash_table_new      (g_direct_hash, g_direct_equal);

	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
	gtk_container_add (GTK_CONTAINER (frame), panel->priv->xtext);

	gtk_box_set_spacing (GTK_BOX (panel), 6);
	gtk_box_pack_start  (GTK_BOX (panel), frame,                  TRUE,  TRUE, 0);
	gtk_box_pack_start  (GTK_BOX (panel), panel->priv->scrollbar, FALSE, TRUE, 0);

	gtk_widget_show (panel->priv->xtext);
	gtk_widget_show (panel->priv->scrollbar);
	gtk_widget_show (frame);
	gtk_widget_show (GTK_WIDGET (panel));

	action_group = gtk_action_group_new ("TextPopups");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (action_group, url_actions,     G_N_ELEMENTS (url_actions),   panel);
	gtk_action_group_add_actions (action_group, email_actions,   G_N_ELEMENTS (email_actions), panel);
	gtk_action_group_add_actions (action_group, dnd_actions,     G_N_ELEMENTS (dnd_actions),   panel);
	gtk_action_group_add_actions (action_group, default_actions, G_N_ELEMENTS (default_actions),   panel);
	gtk_ui_manager_insert_action_group (gui.manager, action_group, 0);
	g_object_unref (action_group);

	panel->priv->tooltip_nick = NULL;
	gtk_widget_set_has_tooltip(GTK_WIDGET(panel), TRUE);

	client = gconf_client_get_default ();
	panel->priv->redundant_nickstamps =
		gconf_client_get_bool (client, "/apps/xchat/main_window/redundant_nickstamps", NULL);
	g_object_unref (client);

	g_signal_connect (G_OBJECT (panel), "style-set", G_CALLBACK (style_set_callback), panel);
	g_signal_connect (G_OBJECT (panel->priv->xtext), "drag_data_received", G_CALLBACK (drag_data_received), panel);
	gtk_drag_dest_set (panel->priv->xtext, GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP,
	                   NULL, 0, GDK_ACTION_COPY | GDK_ACTION_ASK);
        gtk_drag_dest_add_uri_targets (panel->priv->xtext);
        gtk_drag_dest_add_text_targets (panel->priv->xtext);

	conversation_panel_set_show_marker (panel, prefs.show_marker);
}

static void
conversation_panel_finalize (GObject *object)
{
	ConversationPanel *panel;

	panel = CONVERSATION_PANEL (object);

	g_hash_table_destroy (panel->priv->buffers);
	g_hash_table_destroy (panel->priv->timestamp_notifies);
	g_free (panel->priv);

	if (G_OBJECT_CLASS (parent_class)->finalize) {
		G_OBJECT_CLASS (parent_class)->finalize (object);
	}
}

static void
conversation_panel_realize (GtkWidget *widget)
{
	ConversationPanel *panel;
	GConfClient       *client;

	if (GTK_WIDGET_CLASS (parent_class)->realize) {
		GTK_WIDGET_CLASS (parent_class)->realize (widget);
	}

	panel  = CONVERSATION_PANEL (widget);
	client = gconf_client_get_default ();

	gtk_xtext_set_palette(GTK_XTEXT(panel->priv->xtext), colors);
	gtk_xtext_set_max_lines(GTK_XTEXT(panel->priv->xtext), 3000);
	gtk_xtext_set_indent(GTK_XTEXT(panel->priv->xtext), prefs.indent_nicks);
	gtk_xtext_set_max_indent(GTK_XTEXT(panel->priv->xtext), prefs.max_auto_indent);
	gtk_xtext_set_urlcheck_function(GTK_XTEXT(panel->priv->xtext), conversation_panel_check_word);

	conversation_panel_set_font(panel);
	conversation_panel_set_background(panel);

	g_signal_connect (G_OBJECT (panel->priv->xtext), "word_click",      G_CALLBACK (conversation_panel_clicked_word), panel);
	g_signal_connect (G_OBJECT (panel->priv->xtext), "word_enter",      G_CALLBACK (conversation_panel_enter_word),   panel);
	g_signal_connect (G_OBJECT (panel->priv->xtext), "word_leave",      G_CALLBACK (conversation_panel_leave_word),   panel);
	g_signal_connect (G_OBJECT (gui.main_window),    "focus-out-event", G_CALLBACK (conversation_panel_lost_focus),   panel);
	g_signal_connect (G_OBJECT (gui.main_window),    "leave-notify-event", G_CALLBACK (conversation_panel_lost_focus),   panel);
	gconf_client_notify_add (client, "/apps/xchat/main_window/use_sys_fonts",
	                         (GConfClientNotifyFunc) conversation_panel_font_changed,       panel, NULL, NULL);
	gconf_client_notify_add (client, "/apps/xchat/main_window/font",
	                         (GConfClientNotifyFunc) conversation_panel_font_changed,       panel, NULL, NULL);
	gconf_client_notify_add (client, "/apps/xchat/main_window/background_type",
	                         (GConfClientNotifyFunc) conversation_panel_background_changed, panel, NULL, NULL);
	gconf_client_notify_add (client, "/apps/xchat/main_window/background_image",
	                         (GConfClientNotifyFunc) conversation_panel_background_changed, panel, NULL, NULL);
	gconf_client_notify_add (client, "/apps/xchat/main_window/background_transparency",
	                         (GConfClientNotifyFunc) conversation_panel_background_changed, panel, NULL, NULL);
	gconf_client_notify_add (client, "/apps/xchat/main_window/redundant_nickstamps",
	                         (GConfClientNotifyFunc) redundant_nickstamp_changed, panel, NULL, NULL);


	g_object_unref (client);
}

static int
conversation_panel_check_word (GtkWidget *xtext, char *word, int len)
{
	int url;
        current_sess = gui.current_session;

	url = url_check_word (word, len);
	if (url == 0 && current_sess) {
		if (current_sess->type == SESS_DIALOG) {
			if (strcmp (word, current_sess->channel) == 0) {
				return WORD_NICK;
			}
			return WORD_DIALOG;
		} else if (((word[0]=='@' || word[0]=='+') && userlist_find (current_sess, word+1)) ||
			   userlist_find (current_sess, word)) {
			if (strcmp (word, current_sess->server->nick) != 0) {
				return WORD_NICK;
			}
		}
	}
	return url;
}

static void
conversation_panel_clicked_word (GtkWidget *xtext, char *word, GdkEventButton *event, ConversationPanel *panel)
{
	if (word == NULL) {
		return;
	}

	if (event->button == 1) {
		switch (conversation_panel_check_word (xtext, word, strlen (word))) {
		case WORD_URL:
		case WORD_HOST:
			{
				char *command;
				command = g_strdup_printf ("URL %s", word);
				handle_command (gui.current_session, command, 1);
				g_free (command);
			}
			break;
		case WORD_NICK:
			{
				char *command;
				struct session *sess;

				if ((gui.current_session->type == SESS_DIALOG) &&
				    (strcmp (gui.current_session->channel, word) == 0)) {
					break;
				}

				sess = find_dialog (gui.current_session->server, word);
				if (sess) {
					navigation_tree_select_session (gui.server_tree, sess);
				} else {
					command = g_strdup_printf ("QUERY %s", word);
					handle_command (gui.current_session, command, 1);
					g_free (command);
				}
			}
			break;
		case WORD_CHANNEL:
			{
				char *command;
				struct session *sess;

				sess = find_channel (gui.current_session->server, word);
				if (sess) {
					navigation_tree_select_session (gui.server_tree, sess);
				}
				/* Having the channel opened doesn't mean that we're still on it
				 * (if channel was left) */
				command = g_strdup_printf ("JOIN %s", word);
				handle_command (gui.current_session, command, 1);
				g_free (command);
			}
			break;
		}
	}
	if (event->button == 3) {
		switch (conversation_panel_check_word (xtext, word, strlen (word))) {
		case 0:
			{
				GtkWidget *menu;
				menu = gtk_ui_manager_get_widget (gui.manager, "/DefaultPopup");
				/* FIXME: we should not display the copy action if no text is selected */
				gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->time);
			}
			break;
		case WORD_DIALOG:
			{
				GtkWidget *menu;
				menu = gtk_ui_manager_get_widget (gui.manager, "/DialogPopup");
				strcpy (dialog_user.nick, gui.current_session->channel);
				dialog_user.nick[strlen (gui.current_session->channel)] = '\0';
				current_user = &dialog_user;
				gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->time);
			}
			break;
		case WORD_URL:
		case WORD_HOST:
			{
				GtkWidget *menu;
				menu = gtk_ui_manager_get_widget (gui.manager, "/TextURLPopup");
				if (panel->priv->selected_word) {
					g_free (panel->priv->selected_word);
				}
				panel->priv->selected_word = g_strdup (word);
				gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->time);
			}
			break;
		case WORD_NICK:
			{
				struct User *user;
				GtkWidget   *menu;

				if (panel->priv->selected_word) {
					g_free (panel->priv->selected_word);
				}
				panel->priv->selected_word = g_strdup (word);

				if (gui.current_session->type == SESS_CHANNEL) {
					menu = gtk_ui_manager_get_widget (gui.manager, "/UserlistPopup");
					user = userlist_find (gui.current_session, word);
					if (user) {
						current_user = user;
						gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->time);
					}
				} else if (gui.current_session->type == SESS_DIALOG) {
					menu = gtk_ui_manager_get_widget (gui.manager, "/UserDialogPopup");
					strcpy (dialog_user.nick, word);
					dialog_user.nick[strlen (word)] = '\0';
					current_user = &dialog_user;
					gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->time);
				}
			}
			break;
		case WORD_CHANNEL:
			/* FIXME: show channel context menu */
			break;
		case WORD_EMAIL:
			{
				GtkWidget *menu;
				menu = gtk_ui_manager_get_widget (gui.manager, "/TextEmailPopup");
				if (panel->priv->selected_word) {
					g_free (panel->priv->selected_word);
				}
				panel->priv->selected_word = g_strdup (word);
				gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->time);
			}
			break;
		}
	}
}


static void
conversation_panel_enter_word(GtkWidget *xtext,
                              char *word,
                              ConversationPanel *panel)
{
	switch (conversation_panel_check_word(xtext, word, strlen (word))) {
	case WORD_NICK:
		panel->priv->tooltip_nick = g_strdup(word);
		break;
	}
}


static void
conversation_panel_leave_word(GtkWidget *xtext,
                              char *word,
                              ConversationPanel *panel)
{
	switch (conversation_panel_check_word (xtext, word, strlen (word))) {
	case WORD_NICK:
		gtk_widget_set_has_tooltip(GTK_WIDGET(panel), FALSE);
		gtk_widget_set_has_tooltip(GTK_WIDGET(panel), TRUE);
		g_free(panel->priv->tooltip_nick);
		panel->priv->tooltip_nick = NULL;
		break;
	}
}


static gboolean
conversation_panel_lost_focus(GtkWidget *widget,
                              GdkEventFocus *event,
                              ConversationPanel *panel)
{
	g_free(panel->priv->tooltip_nick);
	panel->priv->tooltip_nick = NULL;
	return FALSE;
}

static void
conversation_panel_set_font (ConversationPanel *panel)
{
	GConfClient *client;
	gchar       *font = NULL;

	client = gconf_client_get_default ();
	if (!gconf_client_get_bool(client, "/apps/xchat/main_window/use_sys_fonts", NULL)) {
		font = gconf_client_get_string (client, "/apps/xchat/main_window/font", NULL);
	}

	/* Either use_sys_fonts==TRUE, or there is no current font preference.
	 * In both cases we try the GNOME monospace font. */
	if (font == NULL) {
		font = gconf_client_get_string (client, "/desktop/gnome/interface/monospace_font_name", NULL);
	}

	g_object_unref (client);

	if (font == NULL)
		font = g_strdup ("fixed 11");

	gtk_xtext_set_font (GTK_XTEXT (panel->priv->xtext), font);

	g_free (font);
}

static void
conversation_panel_font_changed (GConfClient *client, guint cnxn_id, GConfEntry *entry, ConversationPanel *panel)
{
	GtkAdjustment *adj;

	conversation_panel_set_font (panel);

	adj = GTK_XTEXT (panel->priv->xtext)->adj;
	gtk_adjustment_set_value (adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
	gtk_xtext_refresh (GTK_XTEXT (panel->priv->xtext), FALSE);
}

static void
conversation_panel_set_background (ConversationPanel *panel)
{
	gtk_xtext_set_alpha (GTK_XTEXT (panel->priv->xtext), 1.0);
}

static void
conversation_panel_background_changed (GConfClient *client, guint cnxn_id, GConfEntry *entry, ConversationPanel *panel)
{
	conversation_panel_set_background (panel);
	gtk_xtext_refresh (GTK_XTEXT (panel->priv->xtext), TRUE);
}


gboolean
conversation_panel_query_tooltip(GtkWidget *widget,
                                 gint x,
                                 gint y,
                                 gboolean keyboard_tooltip,
                                 GtkTooltip *tooltip)
{
	ConversationPanel *panel = CONVERSATION_PANEL (widget);

	// FIXME: keyboard tooltips
	if (panel->priv->tooltip_nick) {
		struct User *user = userlist_find(gui.current_session,
		                                  panel->priv->tooltip_nick);
		if (user != NULL) {
			gtk_tooltip_set_custom(tooltip,
			                       get_user_vbox_infos(user));
			return TRUE;
		}
	}
	return FALSE;
}


static void
timestamps_changed (GConfClient *client, guint cnxn_id, GConfEntry *entry, xtext_buffer *buffer)
{
	gtk_xtext_set_time_stamp (buffer, gconf_client_get_bool (client, entry->key, NULL));
}

static void
redundant_nickstamp_changed (GConfClient *client, guint cnxn_id, GConfEntry *entry, ConversationPanel *panel)
{
	panel->priv->redundant_nickstamps = gconf_value_get_bool (entry->value);
}

static void
open_url (GtkAction *action, ConversationPanel *panel)
{
        ConversationPanelPriv *priv = panel->priv;

        if (!priv->selected_word || !priv->selected_word[0])
                return;

	char *command;
	command = g_strdup_printf ("URL %s", priv->selected_word);
	handle_command (gui.current_session, command, 1);
	g_free (command);
}

static void
copy_text (GtkAction *action, ConversationPanel *panel)
{
	GdkDisplay *display;
	GtkClipboard *clipboard;

	display = gtk_widget_get_display (GTK_WIDGET (panel));

	clipboard = gtk_clipboard_get_for_display (display, GDK_SELECTION_PRIMARY);
	gtk_clipboard_set_text (clipboard, panel->priv->selected_word, -1);
	clipboard = gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text (clipboard, panel->priv->selected_word, -1);
}

static void
send_email (GtkAction *action, ConversationPanel *panel)
{
	/* FIXME */
}

static void
drop_send_files  (GtkAction *action, ConversationPanel *panel)
{
	if (panel->priv->current->type != SESS_DIALOG) {
		return;
	}
	drop_send (panel);
}

static void
drop_paste_file  (GtkAction *action, ConversationPanel *panel)
{
	drop_paste (panel);
}

static void
drop_paste_filename  (GtkAction *action, ConversationPanel *panel)
{
	gchar *txt = NULL, *path, *tmp;
	GSList *l;

	g_return_if_fail (panel->priv->dropped_files != NULL);

	for (l = panel->priv->dropped_files; l != NULL; l = g_slist_next (l)) {
		path = g_filename_from_uri (l->data, NULL, NULL);
		if (path == NULL) {
			path = g_strdup (l->data);
		}

		if (txt == NULL) {
			txt = g_strdup (path);
		} else {
			tmp = txt;
			txt = g_strdup_printf ("%s %s", tmp, path);

			g_free (tmp);
		}

		g_free (path);
	}

	if (panel->priv->current != NULL) {
		handle_multiline (panel->priv->current, txt, TRUE, FALSE);
	}

	g_free (txt);
	free_dropped_files (panel);
}

static void
drop_cancel  (GtkAction *action, ConversationPanel *panel)
{
	free_dropped_files (panel);
}

static void
drag_data_received (GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                    GtkSelectionData *selection_data, guint info, guint time, ConversationPanel *panel)
{
	GdkAtom target;

	target = gtk_selection_data_get_target (selection_data);

	if (gtk_targets_include_uri (&target, 1)) {
		gchar **uris;
		gint nb_uri;

		if ((panel->priv->current->type != SESS_CHANNEL) &&
		    (panel->priv->current->type != SESS_DIALOG)) {
			return;
		}

                uris = gtk_selection_data_get_uris (selection_data);
                if (!uris)
                        return;

		free_dropped_files (panel);

		for (nb_uri = 0; uris[nb_uri] && strlen (uris[nb_uri]) > 0; nb_uri++) {
			panel->priv->dropped_files = g_slist_prepend (panel->priv->dropped_files, uris[nb_uri]);
		}
		g_free (uris); /* String in uris will be freed in free_dropped_files */
		panel->priv->dropped_files = g_slist_reverse (panel->priv->dropped_files);

		if (gdk_drag_context_get_actions (context) == GDK_ACTION_ASK) {
			/* Display the context menu */
			GtkWidget *menu, *entry;

			menu = gtk_ui_manager_get_widget (gui.manager, "/DropFilePopup");
			entry = gtk_ui_manager_get_widget (gui.manager, "/DropFilePopup/DropPasteFile");
			if (nb_uri > 1 ||
			    (uri_is_text (panel->priv->dropped_files->data) == FALSE) ||
			    (check_file_size (panel->priv->dropped_files->data) == FALSE)) {
				gtk_widget_set_sensitive (entry, FALSE);
			} else {
				gtk_widget_set_sensitive (entry, TRUE);
			}

			/* Enable/Disable send files */
			entry = gtk_ui_manager_get_widget (gui.manager, "/DropFilePopup/DropSendFiles");
			if (panel->priv->current->type == SESS_CHANNEL) {
				gtk_widget_set_sensitive (entry, FALSE);
			} else {
				gtk_widget_set_sensitive (entry, TRUE);
			}

			gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 2, gtk_get_current_event_time ());
		} else {
			/* Do the default action */
			if (gui.current_session->type == SESS_CHANNEL) {
				/* Dropped in a channel */
				if (nb_uri == 1 &&
				    uri_is_text (panel->priv->dropped_files->data) &&
				    check_file_size (panel->priv->dropped_files->data)) {
					drop_paste (panel);
				}
			} else {
				/* Dropped in a query */
				if (nb_uri == 1 &&
				    uri_is_text (panel->priv->dropped_files->data) &&
				    check_file_size (panel->priv->dropped_files->data)) {
					drop_paste (panel);
				} else {
					drop_send (panel);
				}
			}
		}
	} else if (gtk_targets_include_text (&target, 1)) {
		char *txt;

                txt = (char *) gtk_selection_data_get_text (selection_data);
		if (gui.current_session != NULL) {
			handle_multiline (gui.current_session, txt, TRUE, FALSE);
		}

		g_free (txt);
	}
}

static void
free_dropped_files (ConversationPanel *panel)
{
	if (panel->priv->dropped_files) {
		g_slist_foreach (panel->priv->dropped_files, (GFunc) g_free, NULL);
		g_slist_free    (panel->priv->dropped_files);
		panel->priv->dropped_files = NULL;
	}
}

static void
drop_send (ConversationPanel *panel)
{
	g_return_if_fail (panel->priv->dropped_files != NULL);
	g_slist_foreach (panel->priv->dropped_files, send_file, NULL);
	free_dropped_files (panel);
}

static void
drop_paste (ConversationPanel *panel)
{
	gboolean res;
	char *contents;
	const char *uri = panel->priv->dropped_files->data;
	GError *error = NULL;

	g_return_if_fail (g_slist_length (panel->priv->dropped_files) == 1);

	res = g_file_get_contents (uri, &contents, NULL, NULL);

	if (res) {
		if (panel->priv->current != NULL) {
			handle_multiline (panel->priv->current, contents, TRUE, FALSE);
		}
		g_free (contents);
	} else {
		g_printerr (_("Error reading file \"%s\": %s\n"), uri, error->message);
		g_error_free (error);
	}

	free_dropped_files (panel);
}

static void
send_file (gpointer file, gpointer user_data)
{
	gchar *path;
	GError *err = NULL;

	path = g_filename_from_uri ((char*) file, NULL, &err);

	if (err) {
		g_printerr (_("Error converting URI \"%s\" into filename: %s\n"), (char*) file, err->message);
		g_error_free (err);
	} else {
		dcc_send (gui.current_session, gui.current_session->channel, path, 0, FALSE);
		g_free (path);
	}
}

static void
on_default_copy_activate (GtkAction *action, ConversationPanel *panel)
{
	conversation_panel_copy_selection (panel);
}

static gboolean
uri_is_text (gchar *uri)
{
	GFile *file;
	GFileInfo *info;
	gboolean is_text = FALSE;

	file = g_file_new_for_uri (uri);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  0, NULL, NULL);
	g_object_unref (file);

	if (info) {
		const char *type;
		type = g_file_info_get_content_type (info);
		is_text = g_content_type_is_a (type, "text");

		g_object_unref (info);
	}

	return is_text;
}

static gboolean
check_file_size (gchar *uri)
{
	GFile *file;
	GFileInfo *info;
	GError *error = NULL;
	gboolean file_size_ok = FALSE;

	file = g_file_new_for_uri (uri);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SIZE,
				  0, NULL, &error);
	g_object_unref (file);

	if (!info) {
		g_printerr (_("Error retrieving file information for \"%s\": %s\n"),
			      uri, error->message);
		g_error_free (error);
	} else {
		goffset size = g_file_info_get_size (info);

		if (size <= DROP_FILE_PASTE_MAX_SIZE)
			file_size_ok = TRUE;

		g_object_unref (info);
	}

	return file_size_ok;
}

static void
style_set_callback (GtkWidget *widget, GtkStyle  *previous_style, void *data)
{
	GConfClient *client;
	gint color_scheme;
	client = gconf_client_get_default ();
	color_scheme = gconf_client_get_int (client, "/apps/xchat/irc/color_scheme", NULL);
	g_object_unref (client);
	if (color_scheme == 3) {
		load_colors (color_scheme);
		load_palette (color_scheme);
	}
	conversation_panel_update_colors (CONVERSATION_PANEL (gui.conversation_panel));
}

GtkWidget *
conversation_panel_new (void)
{
	return GTK_WIDGET (g_object_new (conversation_panel_get_type (), NULL));
}

void
conversation_panel_update_colors (ConversationPanel *panel)
{
	gtk_xtext_set_palette (GTK_XTEXT (panel->priv->xtext), colors);
	gtk_xtext_refresh     (GTK_XTEXT (panel->priv->xtext), FALSE);
}

void
conversation_panel_add_session (ConversationPanel *panel, struct session *sess, gboolean focus)
{
	GConfClient  *client;
	xtext_buffer *buffer;
	gint          notify;

	buffer = gtk_xtext_buffer_new (GTK_XTEXT (panel->priv->xtext));

	client = gconf_client_get_default ();
	gtk_xtext_set_time_stamp (buffer, gconf_client_get_bool (client, "/apps/xchat/irc/showtimestamps", NULL));
	notify = gconf_client_notify_add (client, "/apps/xchat/irc/showtimestamps",
	                                  (GConfClientNotifyFunc) timestamps_changed, buffer, NULL, NULL);
	g_object_unref (client);

	g_hash_table_insert (panel->priv->buffers,            sess, buffer);
	g_hash_table_insert (panel->priv->timestamp_notifies, sess, GINT_TO_POINTER (notify));

	if (focus) {
		conversation_panel_set_current (panel, sess);
	}
}

void
conversation_panel_set_current (ConversationPanel *panel, struct session *sess)
{
	xtext_buffer *buffer;

	panel->priv->current = sess;
	if (sess) {
		buffer = g_hash_table_lookup (panel->priv->buffers, sess);
		gtk_xtext_buffer_show (GTK_XTEXT (panel->priv->xtext), buffer, TRUE);
	} else {
		gtk_xtext_buffer_show (GTK_XTEXT (panel->priv->xtext), GTK_XTEXT (panel->priv->xtext)->orig_buffer, TRUE);
	}

	gtk_widget_set_has_tooltip(GTK_WIDGET(panel), FALSE);
	gtk_widget_set_has_tooltip(GTK_WIDGET(panel), TRUE);
	g_free(panel->priv->tooltip_nick);
	panel->priv->tooltip_nick = NULL;
}

void
conversation_panel_save_current (ConversationPanel *panel)
{
	GtkWidget *file_chooser;
	gchar     *default_filename;
	gchar      dates[32];
	struct tm  date;
	time_t     dtime;

	if (!panel->priv->current)
		return;

	file_chooser = gtk_file_chooser_dialog_new (_("Save Transcript"),
	                                            GTK_WINDOW (gui.main_window),
	                                            GTK_FILE_CHOOSER_ACTION_SAVE,
	                                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                            GTK_STOCK_SAVE,   GTK_RESPONSE_ACCEPT,
	                                            NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (file_chooser), TRUE);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (file_chooser), TRUE);
	gtk_dialog_set_default_response (GTK_DIALOG (file_chooser), GTK_RESPONSE_ACCEPT);

	time (&dtime);
	localtime (&date);
	strftime (dates, 32, "%F-%Hh%M", &date);

	default_filename = g_strdup_printf ("%s-%s.log", panel->priv->current->channel, dates);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_chooser), default_filename);
	g_free (default_filename);

	if (gtk_dialog_run (GTK_DIALOG (file_chooser)) == GTK_RESPONSE_ACCEPT) {
		gchar *filename;
		GIOChannel *file;
		GError *error = NULL;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));
		file = g_io_channel_new_file (filename, "w", &error);
		if (error) {
			gchar *header = g_strdup_printf (_("Error saving %s"), filename);
			error_dialog (header, error->message);
			g_free (header);
			g_error_free (error);
		} else {
			gint fd = g_io_channel_unix_get_fd (file);
			gtk_xtext_save (GTK_XTEXT (panel->priv->xtext), fd);
			g_io_channel_shutdown (file, TRUE, &error);

			if (error) {
				gchar *header = g_strdup_printf (_("Error saving %s"), filename);
				error_dialog (header, error->message);
				g_free (header);
				g_error_free (error);
			}
		}
		g_free (filename);
	}
	gtk_widget_destroy (file_chooser);
}

void
conversation_panel_clear (ConversationPanel *panel, struct session *sess)
{
	xtext_buffer *buffer;

	buffer = g_hash_table_lookup (panel->priv->buffers, sess);
	gtk_xtext_clear (buffer);
	gtk_xtext_refresh (GTK_XTEXT (panel->priv->xtext), FALSE);
}

static void
conversation_panel_print_line (ConversationPanel *panel, xtext_buffer *buffer, char *text, int len, gboolean indent, time_t timet)
{
	if (len == 0) {
		return;
	}

	if (indent == FALSE) {
		int     stamp_size;
		char   *stamp;
		guchar *new_text;

		if (timet == 0)
			timet = time (NULL);

		stamp_size = get_stamp_str (prefs.stamp_format, timet, &stamp);
		new_text = g_malloc (len + stamp_size + 1);
		memcpy (new_text, stamp, stamp_size);
		g_free (stamp);
		memcpy (new_text + stamp_size, text, len);
		gtk_xtext_append (buffer, new_text, len + stamp_size);
		g_free (new_text);
		return;
	}

	char *tab = strchr (text, '\t');
	if (tab && tab < (text + len)) {
		int leftlen = tab - text;

		if(!panel->priv->redundant_nickstamps && strncmp (buffer->laststamp, text, leftlen) == 0) {
			text = tab+1;
			len -= leftlen;
			gtk_xtext_append_indent (buffer, 0, 0, (unsigned char*) text, len, timet);
		} else {
			strncpy (buffer->laststamp, text, leftlen);
			buffer->laststamp[leftlen]=0;
			gtk_xtext_append_indent (buffer, (unsigned char*) text, leftlen,
			                         (unsigned char*) tab + 1, strlen (text) - leftlen - 1,
			                         timet);
		}
	} else {
		gtk_xtext_append_indent (buffer, 0, 0, (unsigned char*) text, len, timet);
	}
}

void
conversation_panel_print (ConversationPanel *panel, struct session *sess, char *text, gboolean indent, time_t stamp)
{
	xtext_buffer *buffer;
	char *last_text = text;
	int len = 0;

	if (strlen (text) == 0) {
		return;
	}

	buffer = g_hash_table_lookup (panel->priv->buffers, sess);
	if (buffer == NULL) {
		return;
	}

	/* split the text into separate lines */
	while (1) {
		switch (*text) {
		case '\0':
			conversation_panel_print_line (panel, buffer, last_text, len, indent, stamp);
			return;
		case '\n':
			conversation_panel_print_line (panel, buffer, last_text, len, indent, stamp);
			text++;
			if (*text == '\0')
				return;
			last_text = text;
			len = 0;
			break;
		case ATTR_BEEP:
			*text = ' ';
			gdk_beep ();
		default:
			text++;
			len++;
		}
	}
}

void
conversation_panel_remove_session (ConversationPanel *panel, struct session *sess)
{
	GConfClient *client;
	gint         notify;

	client = gconf_client_get_default ();
	notify = GPOINTER_TO_INT (g_hash_table_lookup (panel->priv->timestamp_notifies, sess));
	g_hash_table_remove (panel->priv->timestamp_notifies, sess);
	gconf_client_notify_remove (client, notify);
	g_object_unref (client);

	g_hash_table_remove (panel->priv->buffers, sess);
}

static void
conversation_panel_lastlog_foreach (GtkXText *xtext, char *text, fe_lastlog_info *info)
{
	if (nocasestrstr (text, (char*) info->sstr)) {
		conversation_panel_print (info->panel, info->sess, text, prefs.indent_nicks, time(NULL));
	}
}

void
conversation_panel_lastlog (ConversationPanel *panel, struct session *sess, struct session *lsess, char *sstr)
{
	xtext_buffer *buffer;

	buffer  = g_hash_table_lookup (panel->priv->buffers, sess);

	if (gtk_xtext_is_empty (buffer)) {
		conversation_panel_print (panel, lsess, _("Search buffer is empty.\n"), TRUE, time(NULL));
	} else {
		fe_lastlog_info info;
		info.panel = panel;
		info.sess  = lsess;
		info.sstr  = (unsigned char*) sstr;

		gtk_xtext_foreach (buffer, (GtkXTextForeach) conversation_panel_lastlog_foreach, &info);
	}
}

void
conversation_panel_clear_selection (ConversationPanel *panel)
{
	gtk_xtext_selection_clear_full (GTK_XTEXT (panel->priv->xtext)->buffer);
        gtk_xtext_refresh (GTK_XTEXT (panel->priv->xtext), TRUE);
}

gpointer
conversation_panel_search (ConversationPanel *panel, const gchar *text, gpointer start, gboolean casem, gboolean reverse)
{
	return gtk_xtext_search (GTK_XTEXT (panel->priv->xtext), text, start, casem, reverse);
}

void
conversation_panel_page_up (ConversationPanel *panel)
{
	GtkAdjustment *adj;
	int end, value;

	adj = GTK_XTEXT(panel->priv->xtext)->adj;
	end = gtk_adjustment_get_upper (adj) - gtk_adjustment_get_lower (adj) - gtk_adjustment_get_page_size (adj);
	value = gtk_adjustment_get_value (adj) - (gtk_adjustment_get_page_size (adj) - 1);
	if (value < 0) {
		value = 0;
	}
	if (value > end) {
		value = end;
	}
	gtk_adjustment_set_value (adj, value);
}

void
conversation_panel_page_down (ConversationPanel *panel)
{
	GtkAdjustment *adj;
	int value, end;

	adj = GTK_XTEXT(panel->priv->xtext)->adj;
	end = gtk_adjustment_get_upper (adj) - gtk_adjustment_get_lower (adj) - gtk_adjustment_get_page_size (adj);
	value = gtk_adjustment_get_value (adj) + (gtk_adjustment_get_page_size (adj) - 1);
	if (value < 0) {
		value = 0;
	}
	if (value > end) {
		value = end;
	}
	gtk_adjustment_set_value (adj, value);
}

void
conversation_panel_copy_selection (ConversationPanel *panel)
{
	gtk_xtext_copy_selection (GTK_XTEXT (panel->priv->xtext));
}

void
conversation_panel_check_marker_visibility (ConversationPanel *panel)
{
	gtk_xtext_check_marker_visibility (GTK_XTEXT (panel->priv->xtext));
}

void
conversation_panel_set_show_marker (ConversationPanel *panel, gboolean show_marker)
{
	gtk_xtext_set_show_marker (GTK_XTEXT (panel->priv->xtext), show_marker);
}

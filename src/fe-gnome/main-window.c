/*
 * main-window.c - main GUI window functions
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
#include <string.h>
#include <gconf/gconf-client.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "about.h"
#include "channel-list-window.h"
#include "connect-dialog.h"
#include "gui.h"
#include "main-window.h"
#include "navigation-tree.h"
#include "palette.h"
#include "preferences.h"
#include "userlist-gui.h"
#include "util.h"

#include "conversation-panel.h"
#include "find-bar.h"
#include "text-entry.h"
#include "topic-label.h"

#include "../common/xchatc.h"
#include "../common/outbound.h"
#include "../common/fe.h"
#include "../common/xchat-plugin.h"
#include "../common/plugin.h"

static gboolean on_main_window_close (GtkWidget *widget, GdkEvent *event, gpointer data);
static void on_pgup (GtkAccelGroup *accelgroup, GObject *arg1, guint arg2, GdkModifierType arg3, gpointer data);
static void on_pgdn (GtkAccelGroup *accelgroup, GObject *arg1, guint arg2, GdkModifierType arg3, gpointer data);

/* action callbacks */
static void on_irc_connect_activate (GtkAction *action, gpointer data);
static void on_irc_downloads_activate (GtkAction *action, gpointer data);
static void on_irc_quit_activate (GtkAction *action, gpointer data);
static void on_edit_cut_activate (GtkAction *action, gpointer data);
static void on_edit_copy_activate (GtkAction *action, gpointer data);
static void on_edit_paste_activate (GtkAction *action, gpointer data);
static void on_edit_preferences_activate (GtkAction *action, gpointer data);
static void on_network_reconnect_activate (GtkAction *action, gpointer data);
static void on_network_disconnect_activate (GtkAction *action, gpointer data);
static void on_network_channels_activate (GtkAction *action, gpointer data);
static void on_discussion_save_activate (GtkAction *action, gpointer data);
static void on_discussion_leave_activate (GtkAction *action, gpointer data);
static void on_close_activate (GtkAction *action, gpointer data);
static void on_discussion_find_activate (GtkAction *action, gpointer data);
static void on_discussion_bans_activate (GtkAction *action, gpointer data);
static void on_discussion_topic_change_activate (GtkButton *widget, gpointer data);
static void on_discussion_users_activate (GtkAction *action, gpointer data);
static void on_help_contents_activate (GtkAction *action, gpointer data);
static void on_help_about_activate (GtkAction *action, gpointer data);
static void on_nickname_clicked (GtkButton *widget, gpointer user_data);
static void on_users_toggled (GtkToggleButton *widget, gpointer user_data);
static void on_sidebar_toggled (GtkToggleAction *action, gpointer user_data);
static void on_statusbar_toggled (GtkToggleAction *action, gpointer user_data);
static void on_fullscreen_toggled (GtkToggleAction *action, gpointer user_data);

static void on_add_widget (GtkUIManager *manager, GtkWidget *menu, GtkWidget *menu_vbox);

static gboolean on_resize (GtkWidget *widget, GdkEventConfigure *event, gpointer data);
static gboolean on_hpane_move (GtkPaned *widget, GParamSpec *param_spec, gpointer data);

static gboolean on_main_window_focus_in (GtkWidget *widget, GdkEventFocus *event, gpointer data);

static gboolean on_main_window_window_state (GtkWidget *widget, GdkEventWindowState *event, gpointer data);

static void nickname_style_set (GtkWidget *button, GtkStyle *previous_style, gpointer data);

static void main_window_userlist_location_changed (GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer user_data);

static GtkActionEntry action_entries [] = {

	/* Toplevel */
	{ "IRC",         NULL, N_("_IRC") },
	{ "Edit",        NULL, N_("_Edit") },
	{ "Insert",      NULL, N_("In_sert") },
	{ "Network",     NULL, N_("_Network") },
	{ "Discussion",  NULL, N_("_Discussion") },
	{ "View",        NULL, N_("_View") },
	{ "Help",        NULL, N_("_Help") },
	{ "PopupAction", NULL, "" },

	/* IRC menu */
	{ "IRCConnect",   NULL,           N_("_Connect..."),     "<control>N", NULL, G_CALLBACK(on_irc_connect_activate) },
	{ "IRCDownloads", NULL,           N_("_File Transfers"), "<alt>F",     NULL, G_CALLBACK(on_irc_downloads_activate) },
	{ "IRCQuit",      GTK_STOCK_QUIT, N_("_Quit"),           "<control>Q", NULL, G_CALLBACK(on_irc_quit_activate) },

	/* Edit menu */
	{ "EditCut",         GTK_STOCK_CUT,         N_("Cu_t"),         "<control>X", NULL, G_CALLBACK(on_edit_cut_activate) },
	{ "EditCopy",        GTK_STOCK_COPY,        N_("_Copy"),        "<control>C", NULL, G_CALLBACK(on_edit_copy_activate) },
	{ "EditPaste",       GTK_STOCK_PASTE,       N_("_Paste"),       "<control>V", NULL, G_CALLBACK(on_edit_paste_activate) },
	{ "EditPreferences", GTK_STOCK_PREFERENCES, N_("Prefere_nces"), "",           NULL, G_CALLBACK(on_edit_preferences_activate) },

	/* Network menu */
	{ "NetworkReconnect",   GTK_STOCK_REFRESH,     N_("_Reconnect"),   "<control>R",        NULL, G_CALLBACK(on_network_reconnect_activate) },
	{ "NetworkDisconnect",  GTK_STOCK_DISCONNECT,  N_("_Disconnect"),  "",                  NULL, G_CALLBACK(on_network_disconnect_activate) },
	{ "NetworkClose",       GTK_STOCK_CLOSE,       N_("_Close"),       "<shift><control>W", NULL, G_CALLBACK(on_close_activate) },
	{ "NetworkChannels",    NULL,                  N_("_Channels..."), "<alt>C",            NULL, G_CALLBACK(on_network_channels_activate) },

	/* Discussion menu */
	{ "DiscussionSave",        GTK_STOCK_SAVE,           N_("_Save Transcript"), "<control>S", NULL, G_CALLBACK(on_discussion_save_activate) },
	{ "DiscussionLeave",       GTK_STOCK_QUIT,           N_("_Leave"),           "",           NULL, G_CALLBACK(on_discussion_leave_activate) },
	{ "DiscussionClose",       GTK_STOCK_CLOSE,          N_("Cl_ose"),           "<control>W", NULL, G_CALLBACK(on_close_activate) },
	{ "DiscussionFind",        GTK_STOCK_FIND,           N_("_Find"),            "<control>F", NULL, G_CALLBACK(on_discussion_find_activate) },
	{ "DiscussionChangeTopic", NULL,                     N_("Change _Topic"),    "<alt>T",     NULL, G_CALLBACK(on_discussion_topic_change_activate) },
	{ "DiscussionBans",        GTK_STOCK_DIALOG_WARNING, N_("_Bans..."),         "<alt>B",     NULL, G_CALLBACK(on_discussion_bans_activate) },
	{ "DiscussionUsers",       NULL,                     N_("_Users"),           "<control>U", NULL, G_CALLBACK(on_discussion_users_activate) },

	/* Help menu */
	{ "HelpContents", GTK_STOCK_HELP,  N_("_Contents"), "F1", NULL, G_CALLBACK(on_help_contents_activate) },
	{ "HelpAbout",    GTK_STOCK_ABOUT, N_("_About"),    NULL, NULL, G_CALLBACK(on_help_about_activate) },
};

static const GtkToggleActionEntry toggle_action_entries [] =
{
	/* View menu */
	{ "ViewShowSidebar", NULL, N_("_Sidebar"), "F9", "", G_CALLBACK (on_sidebar_toggled), TRUE },
        { "ViewStatusbar", NULL, N_("Status_bar"), "", "", G_CALLBACK (on_statusbar_toggled), TRUE },
	{ "ViewFullscreen", NULL, N_("_Fullscreen"), "F11", "", G_CALLBACK (on_fullscreen_toggled), FALSE }
};

void
initialize_main_window (void)
{
	GtkWidget *close, *menu_vbox, *widget;
	GtkSizeGroup *group;
	GtkAction *action;
	GConfClient *client;
	GdkVisual *visual;

	gui.main_window = GTK_WIDGET (gtk_builder_get_object (gui.xml, "xchat-gnome"));

	visual = gdk_screen_get_rgba_visual (gtk_widget_get_screen (gui.main_window));
	if (visual != NULL)
		gtk_widget_set_visual (gui.main_window, visual);

	g_signal_connect (G_OBJECT (gui.main_window), "delete-event",    G_CALLBACK (on_main_window_close),     NULL);
	g_signal_connect (G_OBJECT (gui.main_window), "focus-in-event",  G_CALLBACK (on_main_window_focus_in),  NULL);
	g_signal_connect (G_OBJECT (gui.main_window), "window-state-event", G_CALLBACK (on_main_window_window_state), NULL);

	/* hook up the menus */
	gui.action_group = gtk_action_group_new ("MenuAction");
	gtk_action_group_set_translation_domain (gui.action_group, NULL);
	gtk_action_group_add_actions (gui.action_group, action_entries,
	                              G_N_ELEMENTS (action_entries), NULL);
	gtk_action_group_add_toggle_actions (gui.action_group, toggle_action_entries,
					     G_N_ELEMENTS (toggle_action_entries), NULL);

	gtk_ui_manager_insert_action_group (gui.manager, gui.action_group, 0);

	menu_vbox = GTK_WIDGET (gtk_builder_get_object (gui.xml, "menu_vbox"));
	g_signal_connect (gui.manager, "add-widget", G_CALLBACK (on_add_widget), menu_vbox);

	/* load the menus */
	gchar *path = locate_data_file ("xchat-gnome-ui.xml");
	g_assert (path != NULL);
	gtk_ui_manager_add_ui_from_file (gui.manager, path, NULL);
	g_free (path);

	/* hook up accelerators */
	gtk_window_add_accel_group (GTK_WINDOW (gui.main_window), gtk_ui_manager_get_accel_group (gui.manager));

	close = GTK_WIDGET (gtk_builder_get_object (gui.xml, "close discussion"));
	action = gtk_action_group_get_action (gui.action_group, "DiscussionClose");
	gtk_activatable_set_related_action (GTK_ACTIVATABLE (close), action);

#define GW(name) ((gui.name) = GTK_WIDGET (gtk_builder_get_object (gui.xml, #name)))
	GW(conversation_panel);
	GW(find_bar);
	GW(status_bar);
	GW(text_entry);
	GW(topic_hbox);
	GW(topic_label);
	GW(nick_button);
#undef GW

	/* Hook up accelerators for pgup/pgdn */
	{
		GtkAccelGroup *pg_accel;
		GClosure *closure;

		/* Create our accelerator group */
		pg_accel = gtk_accel_group_new ();

		/* Add the two accelerators */
		closure = g_cclosure_new (G_CALLBACK (on_pgup), NULL, NULL);
		gtk_accel_group_connect (pg_accel, GDK_KEY_Page_Up, 0, GTK_ACCEL_VISIBLE, closure);
		g_closure_unref (closure);

		closure = g_cclosure_new (G_CALLBACK (on_pgdn), NULL, NULL);
		gtk_accel_group_connect (pg_accel, GDK_KEY_Page_Down, 0, GTK_ACCEL_VISIBLE, closure);
		g_closure_unref (closure);

		/* Add the accelgroup to the main window. */
		gtk_window_add_accel_group (GTK_WINDOW (gui.main_window), pg_accel);
	}

	navigation_tree_add_accels (gui.server_tree, GTK_WINDOW (gui.main_window));

	/* Size group between users button and entry field */
	group = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
	gui.userlist_toggle = GTK_WIDGET (gtk_builder_get_object (gui.xml, "userlist_toggle"));
	g_signal_connect (G_OBJECT (gui.userlist_toggle), "toggled", G_CALLBACK (on_users_toggled), NULL);

	gtk_button_set_image (GTK_BUTTON (gui.userlist_toggle), gtk_image_new_from_icon_name ("xchat-gnome-users", GTK_ICON_SIZE_MENU));
	gtk_size_group_add_widget (group, gui.userlist_toggle);
	widget = GTK_WIDGET (gtk_builder_get_object (gui.xml, "entry hbox"));
	gtk_size_group_add_widget (group, widget);
	g_object_unref (group);

	/* connect nickname button */
	gtk_button_set_use_underline (GTK_BUTTON (gui.nick_button), FALSE);
	g_signal_connect (G_OBJECT (gui.nick_button), "clicked",   G_CALLBACK (on_nickname_clicked), NULL);
	g_signal_connect (G_OBJECT (gtk_bin_get_child (GTK_BIN (gui.nick_button))), "style-set", G_CALLBACK (nickname_style_set),  NULL);
    
    client = gconf_client_get_default ();
    
    /* move userlist to main window if applicable */
    main_window_set_show_userlist (gconf_client_get_bool (client, "/apps/xchat/main_window/userlist_in_main_window", NULL));
    
	gconf_client_notify_add (client, "/apps/xchat/main_window/userlist_in_main_window", (GConfClientNotifyFunc) main_window_userlist_location_changed, NULL, NULL, NULL);

    g_object_unref (client);
}

void
run_main_window (gboolean fullscreen)
{
	GConfClient *client = gconf_client_get_default();

	int width = gconf_client_get_int(client,
	                                 "/apps/xchat/main_window/width",
	                                 NULL);
	int height = gconf_client_get_int(client,
	                                  "/apps/xchat/main_window/height",
	                                  NULL);
	if (width == 0 || height == 0) {
		width = 800;
		height = 550;
	}
	gtk_window_set_default_size(GTK_WINDOW(gui.main_window), width, height);

	int x = gconf_client_get_int (client, "/apps/xchat/main_window/x", NULL);
	int y = gconf_client_get_int (client, "/apps/xchat/main_window/y", NULL);
	if (!(x == 0 || y == 0)) {
		gtk_window_move (GTK_WINDOW(gui.main_window), x, y);
	}

	int h = gconf_client_get_int(client,
	                             "/apps/xchat/main_window/hpane",
	                             NULL);
	if (h != 0) {
		GtkWidget *hpane = GTK_WIDGET (gtk_builder_get_object (gui.xml, "HPane"));
		gtk_paned_set_position(GTK_PANED(hpane), h);
	}
	g_signal_connect(G_OBJECT(gui.main_window), "configure-event",
	                 G_CALLBACK(on_resize), NULL);

	GtkWidget *pane = GTK_WIDGET (gtk_builder_get_object (gui.xml, "HPane"));
	g_signal_connect(G_OBJECT(pane), "notify::position",
	                 G_CALLBACK(on_hpane_move), NULL);

	gboolean val;
	GtkAction *action;

	action = gtk_action_group_get_action(gui.action_group, "ViewShowSidebar");
	val = gconf_client_get_bool(client, "/apps/xchat/main_window/show_hpane", NULL);
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), val);

	action = gtk_action_group_get_action(gui.action_group, "ViewStatusbar");        
	val = gconf_client_get_bool(client, "/apps/xchat/main_window/show_statusbar",NULL);
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), val);

	g_object_unref (client);

	gtk_widget_show (gui.main_window);

	action = gtk_action_group_get_action(gui.action_group,
                                                        "ViewFullscreen");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), fullscreen);
}

void
rename_main_window (gchar *server, gchar *channel)
{
	gchar *new_title;

	if (server == NULL) {
		if (channel && strlen (channel) != 0) {
			gtk_window_set_title (GTK_WINDOW (gui.main_window), channel);
		} else {
			gtk_window_set_title (GTK_WINDOW (gui.main_window), "XChat-GNOME");
		}
		return;
	}
	new_title = g_strconcat (server, ": ", channel, NULL);
	gtk_window_set_title (GTK_WINDOW (gui.main_window), new_title);

	g_free (new_title);
}

static void on_add_widget (GtkUIManager *manager, GtkWidget *menu, GtkWidget *menu_vbox)
{
	gtk_box_pack_start (GTK_BOX (menu_vbox), menu, FALSE, FALSE, 0);
}

static void
on_irc_connect_activate (GtkAction *action, gpointer data)
{
	ConnectDialog *dialog = connect_dialog_new ();
	gtk_widget_show_all (GTK_WIDGET (dialog));
}

void
save_main_window ()
{
	gint x, y;
	GConfClient *client;

	gtk_window_get_position (GTK_WINDOW (gui.main_window), &x, &y);
	client = gconf_client_get_default ();
	gconf_client_set_int (client, "/apps/xchat/main_window/x", x, NULL);
	gconf_client_set_int (client, "/apps/xchat/main_window/y", y, NULL);
	g_object_unref (client);
}

static gboolean
on_main_window_close (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	session *s = gui.current_session;
	int r = plugin_emit_dummy_print (s, "Close Main");
	if (r & XCHAT_EAT_XCHAT) {
	  return TRUE;
	}
	save_main_window ();
	gui.quit = TRUE;

	gtk_widget_hide (GTK_WIDGET (gui.dcc));
	userlist_gui_hide ();
	xchat_exit ();
	return FALSE;
}

static void
on_irc_quit_activate (GtkAction *action, gpointer data)
{
	save_main_window ();
	gtk_widget_hide (GTK_WIDGET (gui.main_window));
	gtk_widget_hide (GTK_WIDGET (gui.dcc));
	userlist_gui_hide ();
	gui.quit = TRUE;
	xchat_exit ();
}

static void
on_edit_cut_activate (GtkAction *action, gpointer data)
{
	gtk_editable_cut_clipboard (GTK_EDITABLE (gui.text_entry));
}

static void
on_edit_copy_activate (GtkAction *action, gpointer data)
{
	if (gtk_editable_get_selection_bounds (GTK_EDITABLE (gui.text_entry), NULL, NULL)) {
		/* There is something selected in the text_entry */
		gtk_editable_copy_clipboard (GTK_EDITABLE (gui.text_entry));
	} else {
		/* Nothing selected, we copy from the conversation panel */
		conversation_panel_copy_selection (CONVERSATION_PANEL (gui.conversation_panel));
	}
}

static void
on_edit_paste_activate (GtkAction *action, gpointer data)
{
	gtk_editable_paste_clipboard (GTK_EDITABLE (gui.text_entry));
}

static void
on_edit_preferences_activate (GtkAction *action, gpointer data)
{
	if (!gui.prefs_dialog) {
		gui.prefs_dialog = preferences_dialog_new ();
		g_object_add_weak_pointer (G_OBJECT (gui.prefs_dialog),
		                           (gpointer *) (&gui.prefs_dialog));
	}

	preferences_dialog_show (gui.prefs_dialog);
}

static void
on_network_reconnect_activate (GtkAction *action, gpointer data)
{
	handle_command (gui.current_session, "reconnect", FALSE);
}

static void
on_network_disconnect_activate (GtkAction *action, gpointer data)
{
	session *s = gui.current_session;
	if (s) {
		s->server->disconnect (s, TRUE, -1);
	}
}

static void
on_irc_downloads_activate (GtkAction *action, gpointer data)
{
	gtk_window_present (GTK_WINDOW (gui.dcc));
}

static void
on_network_channels_activate (GtkAction *action, gpointer data)
{
	create_channel_list_window (gui.current_session, TRUE);
}

static void
on_discussion_users_activate (GtkAction *action, gpointer data)
{
	userlist_gui_show ();
}

static void
on_discussion_save_activate (GtkAction *action, gpointer data)
{
	conversation_panel_save_current (CONVERSATION_PANEL (gui.conversation_panel));
}

static void
on_discussion_leave_activate (GtkAction *action, gpointer data)
{
	session *s = gui.current_session;
	if ((s != NULL) && (s->type == SESS_CHANNEL) && (s->channel[0] != '\0')) {
		gchar *text;
		GConfClient *client;

		client = gconf_client_get_default ();
		text = gconf_client_get_string (client, "/apps/xchat/irc/partmsg", NULL);
		if (text == NULL) {
			text = g_strdup (_("Ex-Chat"));
		}
		s->server->p_part (s->server, s->channel, text);
		g_object_unref (client);
		g_free (text);
	}
}

static void
on_close_activate (GtkAction *action, gpointer data)
{
	session *s = gui.current_session;
	if (s == NULL) {
		return;
	}

	switch (s->type) {
	case SESS_CHANNEL:
	{
		GConfClient *client = gconf_client_get_default ();
		gchar *text = gconf_client_get_string (client, "/apps/xchat/irc/partmsg", NULL);
		if (text == NULL) {
			text = g_strdup (_("Ex-Chat"));
		}

		s->server->p_part (s->server, s->channel, text);

		g_object_unref (client);
		g_free (text);

		break;
	}

	case SESS_SERVER:
		s->server->disconnect (s, TRUE, -1);
		break;
	}

	fe_close_window (s);
}

static void
on_discussion_find_activate (GtkAction *action, gpointer data)
{
	find_bar_open (FIND_BAR (gui.find_bar));
}

static void
on_discussion_bans_activate (GtkAction *action, gpointer data)
{
	/* FIXME: implement */
}

static void
on_pgup (GtkAccelGroup *accelgroup, GObject *arg1, guint arg2, GdkModifierType arg3, gpointer data)
{
	conversation_panel_page_up (CONVERSATION_PANEL (gui.conversation_panel));
}

static void
on_pgdn (GtkAccelGroup *accelgroup, GObject *arg1, guint arg2, GdkModifierType arg3, gpointer data)
{
	conversation_panel_page_down (CONVERSATION_PANEL (gui.conversation_panel));
}

static void
on_help_contents_activate (GtkAction *action, gpointer data)
{
	GError *error = NULL;

	gtk_show_uri (gtk_widget_get_screen (gui.main_window),
		      "help:xchat-gnome",
		      gtk_get_current_event_time (), &error);
	if (error) {
		error_dialog (_("Error showing help"), error->message);
		g_error_free (error);
	}
}

static void
on_help_about_activate (GtkAction *action, gpointer data)
{
	show_about_dialog ();
}

static void
on_sidebar_toggled (GtkToggleAction *action, gpointer data)
{
	GConfClient *client;
	GtkWidget *paned, *child;

	client = gconf_client_get_default();
	gconf_client_set_bool(client,
	                      "/apps/xchat/main_window/show_hpane",
	                      gtk_toggle_action_get_active(action),
	                      NULL);
	g_object_unref(client);

	paned = GTK_WIDGET (gtk_builder_get_object (gui.xml, "HPane"));
	child = gtk_paned_get_child1(GTK_PANED(paned));

	if (gtk_toggle_action_get_active(action)) {
		gtk_widget_show(child);
	} else {
		gtk_widget_hide(child);
	}
}

static void
on_statusbar_toggled (GtkToggleAction *action, gpointer data)
{
        GConfClient *client;
        client = gconf_client_get_default();
        gconf_client_set_bool(client,
                              "/apps/xchat/main_window/show_statusbar",
                              gtk_toggle_action_get_active(action),
                              NULL);
        g_object_unref(client);

        if (gtk_toggle_action_get_active(action)) {
                gtk_widget_show(gui.status_bar);
        } else {
                gtk_widget_hide(gui.status_bar);
        }
}

static void
on_fullscreen_toggled (GtkToggleAction *action, gpointer data)
{
	if (gtk_toggle_action_get_active(action)) {
		gtk_window_fullscreen( GTK_WINDOW(gui.main_window));
	} else {
		gtk_window_unfullscreen( GTK_WINDOW(gui.main_window));
	}
}

static void
nickname_dialog_entry_activated (GtkEntry *entry, GtkDialog *dialog)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_OK);
}

static void
on_nickname_clicked (GtkButton *widget, gpointer user_data)
{
	if (gui.current_session == NULL) {
		return;
	}
	current_sess = gui.current_session;

	GtkWidget *dialog = GTK_WIDGET (gtk_builder_get_object (gui.xml, "nickname dialog"));
	GtkWidget *entry = GTK_WIDGET (gtk_builder_get_object (gui.xml, "nickname dialog entry"));
	GtkWidget *away_button = GTK_WIDGET (gtk_builder_get_object (gui.xml, "nickname dialog away"));

	gtk_entry_set_text(GTK_ENTRY(entry), current_sess->server->nick);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(away_button), current_sess->server->is_away);
	g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(nickname_dialog_entry_activated), dialog);

	gint result = gtk_dialog_run(GTK_DIALOG(dialog));
	if (result == GTK_RESPONSE_OK) {
		GtkWidget *check_all;
		gboolean all;
		gchar *text, *buf;

		check_all = GTK_WIDGET (gtk_builder_get_object (gui.xml, "nickname dialog all"));
		all = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (check_all));

		/* Nick */
		text = (gchar *) gtk_entry_get_text(GTK_ENTRY (entry));
		if (all) {
			buf = g_strdup_printf("allserv nick %s", text);
		} else {
			buf = g_strdup_printf("nick %s", text);
		}
		handle_command(current_sess, buf, FALSE);
		g_free (buf);

		/* Away */
		gboolean away = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(away_button));
		if (current_sess->server->is_away != away) {
			if (away) {
				if (all) {
					handle_command(current_sess, "allserv away", FALSE);
				} else {
					handle_command(current_sess, "away", FALSE);
				}
			} else {
				if (all) {
					handle_command(current_sess, "allserv back", FALSE);
				} else {
					handle_command(current_sess, "back", FALSE);
				}
			}
		}
	}
	gtk_widget_hide(dialog);
}

static gboolean
on_resize (GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
	GConfClient *client;

	client = gconf_client_get_default ();
	gconf_client_set_int (client, "/apps/xchat/main_window/width",  event->width,  NULL);
	gconf_client_set_int (client, "/apps/xchat/main_window/height", event->height, NULL);
	g_object_unref (client);
	return FALSE;
}

static gboolean
on_hpane_move (GtkPaned *widget, GParamSpec *param_spec, gpointer data)
{
	GConfClient *client;
	int pos;

	client = gconf_client_get_default ();
	pos = gtk_paned_get_position (widget);
	gconf_client_set_int (client, "/apps/xchat/main_window/hpane", pos, NULL);
	g_object_unref (client);
	return FALSE;
}

static void
on_discussion_topic_change_activate (GtkButton *widget, gpointer data)
{
	topic_label_change_current (TOPIC_LABEL (gui.topic_label));
}

static void
on_users_toggled (GtkToggleButton *widget, gpointer user_data)
{
	gboolean toggled;

	toggled = gtk_toggle_button_get_active (widget);

	if (toggled) {
		userlist_gui_show ();
	} else {
		userlist_gui_hide ();
	}
}

void
set_nickname_label (struct server *serv, char *newnick)
{
	GtkLabel *label;

	if (gui.current_session == NULL) {
		return;
	}

	label = GTK_LABEL (gtk_bin_get_child (GTK_BIN (gui.nick_button)));

	if (serv == NULL) {
		gtk_label_set_text (label, "");
		return;
	}

	if (serv == gui.current_session->server) {
		if (newnick == NULL) {
			gtk_label_set_text (label, serv->nick);
		} else {
			gtk_label_set_text (label, newnick);
		}
		set_nickname_color (serv);
	}
}

void
set_nickname_color (struct server *serv)
{
	if (gui.current_session == NULL) {
		return;
	}

	if (serv == gui.current_session->server) {
		GtkLabel *label;
		PangoAttribute *attr;
		PangoAttrList *l;
		GtkStyle *style;
		GdkColor *color;

		l = pango_attr_list_new ();
		label = GTK_LABEL (gtk_bin_get_child (GTK_BIN (gui.nick_button)));

		style = gtk_widget_get_style (GTK_WIDGET (label));

		if (serv->is_away) {
			color = &(style->fg[GTK_STATE_INSENSITIVE]);
		} else {
			color = &(style->fg[GTK_STATE_NORMAL]);
		}
		attr = pango_attr_foreground_new (color->red, color->green, color->blue);

		attr->start_index = 0;
		attr->end_index = G_MAXUINT;
		pango_attr_list_insert (l, attr);
		gtk_label_set_attributes (label, l);

		pango_attr_list_unref (l);
	}
}

static gboolean
on_main_window_focus_in (GtkWidget * widget, GdkEventFocus * event, gpointer data)
{
	conversation_panel_check_marker_visibility (CONVERSATION_PANEL (gui.conversation_panel));
	gtk_window_set_urgency_hint (GTK_WINDOW (widget), FALSE);
	return FALSE;
}

static gboolean
on_main_window_window_state (GtkWidget *widget, GdkEventWindowState *event, gpointer data)
{
	if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED)
	{
		if (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED)
			gtk_window_set_has_resize_grip (GTK_WINDOW (widget), FALSE);
		else
			gtk_window_set_has_resize_grip (GTK_WINDOW (widget), TRUE);
	}

	return FALSE;
}

static void
nickname_style_set (GtkWidget *button, GtkStyle *previous_style, gpointer data)
{
	if (gui.current_session == NULL) {
		return;
	}

	set_nickname_color (gui.current_session->server);
}

static void
main_window_userlist_location_changed (GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer user_data)
{
	GConfValue *value = gconf_entry_get_value (entry);
	main_window_set_show_userlist (gconf_value_get_bool (value));
}

void
main_window_set_show_userlist (gboolean show_in_main_window)
{
	GtkAction *discussion_users = gtk_ui_manager_get_action (gui.manager, "/menubar/DiscussionMenu/DiscussionUsers");
    
	if (show_in_main_window) {
		gtk_widget_hide (gui.userlist_toggle);
		gtk_widget_show (GTK_WIDGET (gtk_builder_get_object  (gui.xml, "scrolledwindow_userlist_main")));
		gtk_widget_reparent (GTK_WIDGET (gui.userlist), GTK_WIDGET (gtk_builder_get_object (gui.xml, "scrolledwindow_userlist_main")));
		gtk_action_set_visible (discussion_users, FALSE);
	} else {
		gtk_widget_show (gui.userlist_toggle);
		gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (gui.xml, "scrolledwindow_userlist_main") ));
		gtk_widget_reparent (GTK_WIDGET (gui.userlist), GTK_WIDGET (gtk_builder_get_object (gui.xml, "scrolledwindow_userlist") ));
		gtk_action_set_visible (discussion_users, TRUE);
	}
}

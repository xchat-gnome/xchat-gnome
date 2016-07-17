/*
 * fe-gnome.c - main frontend implementation
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

#include "gui.h"
#include "main-window.h"
#include "migration.h"
#include "navigation-tree.h"
#include "preferences.h"
#include "setup-dialog.h"
#include "userlist-gui.h"
#include <config.h>
#include <gconf/gconf-client.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "connect-dialog.h"
#include "conversation-panel.h"
#include "find-bar.h"
#include "status-bar.h"
#include "text-entry.h"
#include "topic-label.h"

#include "../common/cfgfiles.h"
#include "../common/dcc.h"
#include "../common/fe.h"
#include "../common/plugin.h"
#include "../common/servlist.h"
#include "../common/util.h"
#include "../common/xchat.h"
#include "../common/xchatc.h"
#include "channel-list-window.h"
#include "palette.h"
#include "plugins.h"
#include "preferences-page-plugins.h"
#include "util.h"

#define GNOME_DOT_GNOME ".gnome2"

static gboolean opt_fullscreen = FALSE;
static gboolean opt_version = FALSE;
static gboolean opt_noplugins = FALSE;
static gchar *opt_cfgdir = NULL;

static GOptionEntry entries[] =
    { { "full-screen",
        'f',
        0,
        G_OPTION_ARG_NONE,
        &opt_fullscreen,
        N_("Full-screen the window"),
        NULL },
      { "cfgdir",
        'd',
        0,
        G_OPTION_ARG_FILENAME,
        &opt_cfgdir,
        N_("Use directory instead of the default config dir"),
        "directory" },
      { "no-auto",
        'a',
        0,
        G_OPTION_ARG_NONE,
        &arg_dont_autoconnect,
        N_("Don't auto-connect to servers"),
        NULL },
      { "no-plugins",
        'n',
        0,
        G_OPTION_ARG_NONE,
        &opt_noplugins,
        N_("Don't auto-load plugins"),
        NULL },
      { "url",
        'u',
        0,
        G_OPTION_ARG_STRING,
        &arg_url,
        N_("Open an irc:// url"),
        "irc://server:port/channel" },
      { "existing",
        'e',
        0,
        G_OPTION_ARG_NONE,
        &arg_existing,
        N_("Open URL in an existing XChat-GNOME instance"),
        NULL },
      { "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version, N_("Show version information"), NULL },
      { NULL } };

static gboolean not_autoconnect(void);

static gchar *get_accels_filename(void)
{
        const char *home;

        home = g_get_home_dir();
        if (!home)
                return NULL;
        return g_build_filename(home, GNOME_DOT_GNOME, "accels", PACKAGE_NAME, NULL);
}

static void load_accels(void)
{
        char *filename;

        filename = get_accels_filename();
        if (!filename)
                return;

        gtk_accel_map_load(filename);
        g_free(filename);
}

static void save_accels(void)
{
        char *filename;

        filename = get_accels_filename();
        if (!filename)
                return;

        gtk_accel_map_save(filename);
        g_free(filename);
}

int fe_args(int argc, char *argv[])
{
        GError *error = NULL;
        GOptionContext *context;

        gui.main_window = NULL;

#ifdef ENABLE_NLS
        bindtextdomain(GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
        textdomain(GETTEXT_PACKAGE);
#endif
        context = g_option_context_new(NULL);

        g_option_context_add_main_entries(context, entries, GETTEXT_PACKAGE);
        g_option_context_add_group(context, gtk_get_option_group(FALSE));

        gtk_init(&argc, &argv);

        if (!g_option_context_parse(context, &argc, &argv, &error)) {
                g_printerr(_("Failed to parse arguments: %s\n"), error->message);
                g_error_free(error);
                g_option_context_free(context);
                return 0;
        }

        g_option_context_free(context);

        if (opt_version) {
                g_print("xchat-gnome %s\n", PACKAGE_VERSION);
                return 0;
        }

        if (opt_cfgdir) {
                xdir_fs = opt_cfgdir;
        }

        load_accels();

        return -1;
}

void fe_init(void)
{
        GConfClient *client;

        client = gconf_client_get_default();
        gconf_client_add_dir(client, "/apps/xchat", GCONF_CLIENT_PRELOAD_NONE, NULL);
        g_object_unref(client);

        u = userlist_new();
        gui.quit = FALSE;
        palette_init();
        run_migrations();
        initialize_gui_1();
        if (!preferences_exist()) {
                run_setup_dialog();
        } else {
                set_version();
        }
        servlist_init();
        initialize_gui_2();
        load_preferences();
        run_main_window(opt_fullscreen);

        /* Force various window-related options to match our interaction model */
        prefs.use_server_tab = TRUE;
        prefs.notices_tabs = FALSE;
        prefs.servernotice = TRUE;
        prefs.slist_skip = FALSE;

        /* If we don't have a specific DCC IP address, force get-from-server */
        if (strlen(prefs.dcc_ip_str) == 0) {
                prefs.ip_from_server = TRUE;
        }

        /* Don't allow the core to autoload plugins. We use our own
         * method for autoloading.
         */
        arg_skip_plugins = 1;

        if (not_autoconnect()) {
                ConnectDialog *cd;

                cd = connect_dialog_new();
                gtk_widget_show_all(GTK_WIDGET(cd));
        }

#ifdef USE_PLUGIN
        plugins_initialize();
#endif
}

void fe_main(void)
{
        gtk_main();

        /* sleep for 3 seconds so any QUIT messages are not lost. The  */
        /* GUI is closed at this point, so the user doesn't even know! */

        /* FIXME: this is a crappy hack copied from fe-gtk. There's got
         * to be a way to ensure that the quit messages get sent before
         * we finish */
        if (prefs.wait_on_exit) {
                sleep(3);
        }
}

void fe_cleanup(void)
{
        save_accels();
}

void fe_exit(void)
{
        gtk_main_quit();
}

int fe_timeout_add(int interval, void *callback, void *userdata)
{
        return g_timeout_add(interval, (GSourceFunc)callback, userdata);
}

void fe_timeout_remove(int tag)
{
        g_source_remove(tag);
}

void fe_new_window(struct session *sess, int focus)
{
        static gboolean loaded = FALSE;

        conversation_panel_add_session(CONVERSATION_PANEL(gui.conversation_panel),
                                       sess,
                                       (gboolean)focus);

        switch (sess->type) {
        case SESS_SERVER:
                navigation_model_add_server(gui.tree_model, sess);
                break;
        case SESS_CHANNEL:
        case SESS_DIALOG:
                navigation_model_add_channel(gui.tree_model, sess);
        }

        if (focus) {
                navigation_tree_select_session(gui.server_tree, sess);
        }

#ifdef USE_PLUGIN
        if (!(opt_noplugins || loaded)) {
                loaded = TRUE;
                autoload_plugins();
        }
#endif
}

void fe_new_server(struct server *serv)
{
        /* FIXME: implement */
}

void fe_add_rawlog(struct server *serv, char *text, int len, int outbound)
{
        /* FIXME: implement */
}

void fe_message(char *msg, int wait)
{
        /* FIXME: implement */
}

int fe_input_add(int sok, int flags, void *func, void *data)
{
        int tag, type = 0;
        GIOChannel *channel;

        channel = g_io_channel_unix_new(sok);

        if (flags & FIA_READ) {
                type |= G_IO_IN | G_IO_HUP | G_IO_ERR;
        }
        if (flags & FIA_WRITE) {
                type |= G_IO_OUT | G_IO_ERR;
        }
        if (flags & FIA_EX) {
                type |= G_IO_PRI;
        }

        tag = g_io_add_watch(channel, type, (GIOFunc)func, data);
        g_io_channel_unref(channel);

        return tag;
}

void fe_input_remove(int tag)
{
        g_source_remove(tag);
}

void fe_idle_add(void *func, void *data)
{
        g_idle_add(func, data);
}

void fe_set_topic(struct session *sess, char *topic)
{
        topic_label_set_topic(TOPIC_LABEL(gui.topic_label), sess, topic);
}

void fe_set_hilight(struct session *sess)
{
        navigation_model_set_hilight(gui.tree_model, sess);
        fe_flash_window(sess);
}

void fe_set_tab_color(struct session *sess, int col)
{
        /* FIXME: implement */
}

void fe_update_mode_buttons(struct session *sess, char mode, char sign)
{
        /* FIXME: implement */
}

void fe_update_channel_key(struct session *sess)
{
        /* FIXME: implement */
}

void fe_update_channel_limit(struct session *sess)
{
        /* FIXME: implement */
}

int fe_is_chanwindow(struct server *serv)
{
        return channel_list_exists(serv);
}

void fe_add_chan_list(struct server *serv, char *chan, char *users, char *topic)
{
        char *clean_topic;

        clean_topic = strip_color(topic, -1, STRIP_ALL);
        channel_list_append(serv, chan, users, clean_topic);
        free(clean_topic);
}

void fe_chan_list_end(struct server *serv)
{
        /* FIXME: implement */
}

int fe_is_banwindow(struct session *sess)
{
        /* FIXME: implement */
        return 0;
}

void fe_add_ban_list(struct session *sess, char *mask, char *who, char *when, int is_exemption)
{
        /* FIXME: implement */
}

void fe_ban_list_end(struct session *sess, int is_exemption)
{
        /* FIXME: implement */
}

void fe_notify_update(char *name)
{
        /* FIXME: implement */
}

void fe_text_clear(struct session *sess)
{
        conversation_panel_clear(CONVERSATION_PANEL(gui.conversation_panel), sess);
}

void fe_close_window(struct session *sess)
{
        /*
         * There's really no point in doing all of this if the user is
         * quitting the app.  It makes it slow (as they watch individual
         * channels and servers disappear), and the OS is about to free
         * everything much more efficiently than we ever could.
         *
         * If we ever choose to run on Windows ME, this could be a problem :)
         */
        if (gui.quit) {
                session_free(sess);
                return;
        }

        navigation_tree_remove_session(gui.server_tree, sess);
        conversation_panel_remove_session(CONVERSATION_PANEL(gui.conversation_panel), sess);
        topic_label_remove_session(TOPIC_LABEL(gui.topic_label), sess);
        text_entry_remove_session(TEXT_ENTRY(gui.text_entry), sess);
        if (sess->type == SESS_SERVER) {
                status_bar_remove_server(STATUS_BAR(gui.status_bar), sess->server);
        }

        if (sess == gui.current_session) {
                gui.current_session = NULL;
        }

        session_free(sess);
}

void fe_progressbar_start(struct session *sess)
{
        /* FIXME: implement */
}

void fe_progressbar_end(struct server *serv)
{
        /* FIXME: implement */
}

void fe_print_text(struct session *sess, char *text, time_t stamp)
{
        if (text == NULL) {
                g_warning("NULL passed to fe_print_text.  Perhaps a misbehaving plugin?\n");
                return;
        }

        conversation_panel_print(CONVERSATION_PANEL(gui.conversation_panel),
                                 sess,
                                 text,
                                 prefs.indent_nicks,
                                 stamp);
        sess->new_data = TRUE;
        navigation_model_set_hilight(gui.tree_model, sess);
        if (sess->nick_said) {
                if (!gtk_window_is_active(GTK_WINDOW(gui.main_window))) {
                        gtk_window_set_urgency_hint(GTK_WINDOW(gui.main_window), TRUE);
                }
        }
}

void fe_userlist_insert(struct session *sess, struct User *newuser, int row, int sel)
{
        userlist_insert(u, sess, newuser, row, sel);
}

int fe_userlist_remove(struct session *sess, struct User *user)
{
        return userlist_remove_user(u, sess, user);
}

void fe_userlist_rehash(struct session *sess, struct User *user)
{
        userlist_update(u, sess, user);
}

void fe_userlist_move(struct session *sess, struct User *user, int new_row)
{
        userlist_move(u, sess, user, new_row);
}

void fe_userlist_numbers(struct session *sess)
{
        /* FIXME: implement */
}

void fe_userlist_clear(struct session *sess)
{
        userlist_clear_all(u, sess);
}

void fe_dcc_add(struct DCC *dcc)
{
        if (dcc->type == TYPE_CHATRECV || dcc->type == TYPE_CHATSEND) {
                /* chats */
                if (prefs.autodccchat == FALSE) {
                        GtkWidget *dialog;
                        gint response;

                        dialog = gtk_message_dialog_new(GTK_WINDOW(gui.main_window),
                                                        GTK_DIALOG_MODAL |
                                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        GTK_MESSAGE_QUESTION,
                                                        GTK_BUTTONS_CANCEL,
                                                        _("Incoming DCC Chat"));
                        gtk_dialog_add_button(GTK_DIALOG(dialog),
                                              _("_Accept"),
                                              GTK_RESPONSE_ACCEPT);
                        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                                 _("%s is attempting to create a "
                                                                   "direct chat. Do you wish to "
                                                                   "accept the connection?"),
                                                                 dcc->nick);

                        response = gtk_dialog_run(GTK_DIALOG(dialog));
                        if (response == GTK_RESPONSE_ACCEPT) {
                                dcc_get(dcc);
                        } else {
                                dcc_abort(dcc->serv->server_session, dcc);
                        }
                        gtk_widget_destroy(dialog);
                }
        } else {
                /* file transfers */
                dcc_window_add(gui.dcc, dcc);
        }
}

void fe_dcc_update(struct DCC *dcc)
{
        if (dcc->type == TYPE_CHATRECV || dcc->type == TYPE_CHATSEND) {
                /* chats */
        } else {
                /* file transfers */
                dcc_window_update(gui.dcc, dcc);
        }
}

void fe_dcc_remove(struct DCC *dcc)
{
        if (dcc->type == TYPE_CHATRECV || dcc->type == TYPE_CHATSEND) {
                /* chats */
        } else {
                /* file transfers */
                dcc_window_remove(gui.dcc, dcc);
        }
}

int fe_dcc_open_recv_win(int passive)
{
        /* FIXME: implement? */
        return TRUE;
}

int fe_dcc_open_send_win(int passive)
{
        /* FIXME: implement? */
        return TRUE;
}

int fe_dcc_open_chat_win(int passive)
{
        /* FIXME: implement? */
        return TRUE;
}

void fe_clear_channel(struct session *sess)
{
        navigation_model_set_disconnected(gui.tree_model, sess);
}

void fe_session_callback(struct session *sess)
{
        if (sess->type == SESS_SERVER) {
                status_bar_remove_server(STATUS_BAR(gui.status_bar), sess->server);
        }

        conversation_panel_remove_session(CONVERSATION_PANEL(gui.conversation_panel), sess);
        topic_label_remove_session(TOPIC_LABEL(gui.topic_label), sess);
        text_entry_remove_session(TEXT_ENTRY(gui.text_entry), sess);
        userlist_erase(u, sess);
}

void fe_server_callback(struct server *serv)
{
        /* this frees things */
        /* FIXME: implement */
}

void fe_url_add(const char *text)
{
        /* FIXME: implement */
}

void fe_pluginlist_update(void)
{
}

void fe_buttons_update(struct session *sess)
{
        /* FIXME: implement */
}

void fe_dlgbuttons_update(struct session *sess)
{
        /* FIXME: implement */
}

void fe_dcc_send_filereq(struct session *sess, char *nick, int maxcps, int passive)
{
        /* FIXME: implement */
}

void fe_set_channel(struct session *sess)
{
        navigation_model_update(gui.tree_model, sess);
}

void fe_set_title(struct session *sess)
{
        if (sess == gui.current_session) {
                if (sess->server->network == NULL) {
                        rename_main_window(NULL, sess->channel);
                } else {
                        ircnet *net = sess->server->network;
                        rename_main_window(net->name, sess->channel);
                }
        }
}

void fe_set_nonchannel(struct session *sess, int state)
{
        /* stub? */
}

void fe_set_nick(struct server *serv, char *newnick)
{
        set_nickname_label(serv, newnick);
}

void fe_ignore_update(int level)
{
        /* FIXME: implement */
}

void fe_beep(void)
{
        gdk_beep();
}

void fe_lastlog(session *sess, session *lastlog_sess, char *sstr, gboolean regexp)
{
        /* FIXME: handle regexp */
        conversation_panel_lastlog(CONVERSATION_PANEL(gui.conversation_panel),
                                   sess,
                                   lastlog_sess,
                                   sstr);
}

void fe_set_lag(server *serv, int lag)
{
        unsigned long now;
        float seconds;

        if (gui.quit) {
                return;
        }

        if (lag == -1) {
                if (!serv->lag_sent) {
                        return;
                }
                now = make_ping_time();
                seconds = (now - serv->lag_sent) / 1000000.0f;
        } else {
                seconds = lag / 10.0f;
        }

        status_bar_set_lag(STATUS_BAR(gui.status_bar), serv, seconds, (serv->lag_sent != 0.0f));
}

void fe_set_throttle(server *serv)
{
        if (gui.quit) {
                return;
        }

        status_bar_set_queue(STATUS_BAR(gui.status_bar), serv, serv->sendq_len);
}

void fe_set_away(server *serv)
{
        set_nickname_color(serv);
}

void fe_serverlist_open(session *sess)
{
        /* FIXME: implement */
}

void fe_ctrl_gui(session *sess, fe_gui_action action, int arg)
{
        switch (action) {
        case FE_GUI_HIDE:
                gtk_widget_hide(gui.main_window);
                break;

        case FE_GUI_SHOW:
                gtk_widget_show(gui.main_window);
                gtk_window_present(GTK_WINDOW(gui.main_window));
                break;

        case FE_GUI_ICONIFY:
                gtk_window_iconify(GTK_WINDOW(gui.main_window));
                break;

        case FE_GUI_FOCUS:
        case FE_GUI_FLASH:
                navigation_tree_select_session(gui.server_tree, sess);
                break;
        case FE_GUI_MENU:
        case FE_GUI_ATTACH:
        case FE_GUI_APPLY:
        case FE_GUI_COLOR:
                /* These are unhandled for now */
                break;
        }
}

void fe_confirm(const char *message, void (*yesproc)(void *), void (*noproc)(void *), void *ud)
{
}

int fe_gui_info(session *sess, int info_type)
{
        switch (info_type) {
        case 0:
                if (!gtk_widget_get_visible(GTK_WIDGET(gui.main_window))) {
                        return 2;
                }
                if (gtk_window_is_active(GTK_WINDOW(gui.main_window))) {
                        return 1;
                }
                return 0;
                break;
        }

        return -1;
}

void *fe_gui_info_ptr(session *sess, int info_type)
{
        switch (info_type) {
        case 0: /* native window pointer (for plugins) */
                return GTK_WINDOW(gui.main_window);
        }
        return NULL;
}

void fe_set_inputbox_cursor(session *sess, int delta, int pos)
{
        /* FIXME: implement? */
}

void fe_set_inputbox_contents(session *sess, char *text)
{
        /* FIXME: implement? */
}

char *fe_get_inputbox_contents(session *sess)
{
        /* FIXME: implement? */
        return NULL;
}

int fe_get_inputbox_cursor(struct session *sess)
{
        /* FIXME: implement? */
        return 0;
}

static void get_str_response(GtkDialog *dialog, gint arg1, gpointer entry)
{
        void (*callback)(int cancel, char *text, void *user_data);
        char *text;
        void *user_data;

        text = (char *)gtk_entry_get_text(GTK_ENTRY(entry));
        callback = g_object_get_data(G_OBJECT(dialog), "cb");
        user_data = g_object_get_data(G_OBJECT(dialog), "ud");

        switch (arg1) {
        case GTK_RESPONSE_REJECT:
                callback(TRUE, text, user_data);
                break;
        case GTK_RESPONSE_ACCEPT:
                callback(FALSE, text, user_data);
                break;
        }

        gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void str_enter(GtkWidget *entry, GtkWidget *dialog)
{
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
}

void fe_get_str(char *msg, char *def, void *callback, void *userdata)
{
        GtkWidget *dialog;
        GtkWidget *entry;
        GtkWidget *hbox;
        GtkWidget *label;

        dialog = gtk_dialog_new_with_buttons(msg,
                                             NULL,
                                             0,
                                             GTK_STOCK_CANCEL,
                                             GTK_RESPONSE_REJECT,
                                             GTK_STOCK_OK,
                                             GTK_RESPONSE_ACCEPT,
                                             NULL);
        gtk_box_set_homogeneous(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), TRUE);
        gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);
        hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

        g_object_set_data(G_OBJECT(dialog), "cb", callback);
        g_object_set_data(G_OBJECT(dialog), "ud", userdata);

        entry = gtk_entry_new();
        g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(str_enter), dialog);
        gtk_entry_set_text(GTK_ENTRY(entry), def);
        gtk_box_pack_end(GTK_BOX(hbox), entry, 0, 0, 0);

        label = gtk_label_new(msg);
        gtk_box_pack_end(GTK_BOX(hbox), label, 0, 0, 0);

        g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(get_str_response), entry);

        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), hbox);

        gtk_widget_show_all(dialog);
}

static void get_number_response(GtkDialog *dialog, gint arg1, gpointer spin)
{
        void (*callback)(int cancel, int value, void *user_data);
        int num;
        void *user_data;

        num = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        callback = g_object_get_data(G_OBJECT(dialog), "cb");
        user_data = g_object_get_data(G_OBJECT(dialog), "ud");

        switch (arg1) {
        case GTK_RESPONSE_REJECT:
                callback(TRUE, num, user_data);
                break;
        case GTK_RESPONSE_ACCEPT:
                callback(FALSE, num, user_data);
                break;
        }

        gtk_widget_destroy(GTK_WIDGET(dialog));
}

void fe_get_int(char *msg, int def, void *callback, void *userdata)
{
        GtkWidget *dialog;
        GtkWidget *spin;
        GtkWidget *hbox;
        GtkWidget *label;
        GtkAdjustment *adj;

        dialog = gtk_dialog_new_with_buttons(msg,
                                             NULL,
                                             0,
                                             GTK_STOCK_CANCEL,
                                             GTK_RESPONSE_REJECT,
                                             GTK_STOCK_OK,
                                             GTK_RESPONSE_ACCEPT,
                                             NULL);
        gtk_box_set_homogeneous(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), TRUE);
        gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);
        hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

        g_object_set_data(G_OBJECT(dialog), "cb", callback);
        g_object_set_data(G_OBJECT(dialog), "ud", userdata);

        spin = gtk_spin_button_new(NULL, 1, 0);
        adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin));
        gtk_adjustment_set_lower(adj, 0);
        gtk_adjustment_set_upper(adj, 1024);
        gtk_adjustment_set_step_increment(adj, 1);
        gtk_adjustment_changed(adj);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), def);
        gtk_box_pack_end(GTK_BOX(hbox), spin, 0, 0, 0);

        label = gtk_label_new(msg);
        gtk_box_pack_end(GTK_BOX(hbox), label, 0, 0, 0);

        g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(get_number_response), spin);

        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), hbox);

        gtk_widget_show_all(dialog);
}

void fe_open_url(const char *url)
{
        GdkScreen *screen;
        GError *err = NULL;

        screen = gtk_widget_get_screen(gui.main_window);
        if (strstr(url, "://") == NULL) {
                gchar *newword = g_strdup_printf("http://%s", url);
                gtk_show_uri(screen, newword, gtk_get_current_event_time(), &err);
                g_free(newword);
        } else {
                gtk_show_uri(screen, url, gtk_get_current_event_time(), &err);
        }

        if (err != NULL) {
                gchar *message = g_strdup_printf(_("Unable to show '%s'"), url);
                error_dialog(message, err->message);
                g_free(message);
                g_error_free(err);
        }
}

void fe_menu_del(menu_entry *entry)
{
        /* FIXME: implement? */
}

char *fe_menu_add(menu_entry *entry)
{
        /* FIXME: implement? */
        return NULL;
}

void fe_set_color_paste(session *sess, int status)
{
        /* FIXME: implement? */
}

void fe_menu_update(menu_entry *entry)
{
        /* FIXME: implement? */
}

void fe_uselect(session *sess, char *word[], int do_clear, int scroll_to)
{
        /* FIXME: implement? */
}

void fe_server_event(server *serv, int type, int arg)
{
        GSList *list = sess_list;
        session *sess;

        while (list) {
                sess = list->data;
                if (sess->server == serv) {
                        switch (type) {
                        case FE_SE_LOGGEDIN:
                                if (arg == 0) {
                                        /* No auto-join channels */
                                        GConfClient *client;
                                        gboolean popup_channel_list;

                                        client = gconf_client_get_default();
                                        popup_channel_list = gconf_client_get_bool(
                                            client, "/apps/xchat/channel_list/auto_popup", NULL);
                                        g_object_unref(client);

                                        if (popup_channel_list)
                                                create_channel_list_window(sess, FALSE);
                                }
                                break;
                        case FE_SE_DISCONNECT:
                                navigation_model_set_disconnected(gui.tree_model, sess);
                                break;
                        }
                }
                list = list->next;
        }
}

void fe_userlist_set_selected(struct session *sess)
{
        /* FIXME: implement? */
}

void fe_flash_window(struct session *sess)
{
        if (!gtk_window_is_active(GTK_WINDOW(gui.main_window))) {
                gtk_window_set_urgency_hint(GTK_WINDOW(gui.main_window), TRUE);
        }
}

void fe_get_file(const char *title, char *initial, void (*callback)(void *userdata, char *file),
                 void *userdata, int flags)
{
        /* FIXME: implement */
}

static gboolean not_autoconnect(void)
{
        GSList *i;

        if (arg_dont_autoconnect) {
                return TRUE;
        }

        for (i = network_list; i; i = g_slist_next(i)) {
                ircnet *net = (ircnet *)(i->data);
                if (net->flags & FLAG_AUTO_CONNECT) {
                        return FALSE;
                }
        }

        return TRUE;
}

void fe_set_current(session *sess)
{
        // If find bar is open, hide it
        find_bar_close(FIND_BAR(gui.find_bar));

        gui.current_session = sess;

        // Notify parts of the UI that the current session has changed
        conversation_panel_set_current(CONVERSATION_PANEL(gui.conversation_panel), sess);
        topic_label_set_current(TOPIC_LABEL(gui.topic_label), sess);
        text_entry_set_current(TEXT_ENTRY(gui.text_entry), sess);
        status_bar_set_current(STATUS_BAR(gui.status_bar), sess->server);
        navigation_model_set_current(gui.tree_model, sess);

        // Change the window name
        if (sess->server->network == NULL) {
                rename_main_window(NULL, sess->channel);
        } else {
                ircnet *net = sess->server->network;
                rename_main_window(net->name, sess->channel);
        }

        // Set nickname button
        set_nickname_label(sess->server, NULL);

        // Set the label of the user list button
        userlist_set_user_button(u, sess);
        gtk_widget_set_sensitive(GTK_WIDGET(gui.userlist_toggle), sess->type == SESS_CHANNEL);

        // FIXME: Userlist should be more encapsulated
        gtk_tree_view_set_model(GTK_TREE_VIEW(gui.userlist),
                                GTK_TREE_MODEL(userlist_get_store(u, sess)));

        // Emit "focus tab" event for plugins that rely on it
        plugin_emit_dummy_print(sess, "Focus Tab");

        gtk_widget_grab_focus(GTK_WIDGET(gui.text_entry));
}

/* Tray stubs -- xchat-gnome bundles this as a plugin */
void fe_tray_set_flash(const char *filename1, const char *filename2, int timeout)
{
}
void fe_tray_set_file(const char *filename)
{
}
void fe_tray_set_icon(feicon icon)
{
}
void fe_tray_set_tooltip(const char *text)
{
}
void fe_tray_set_balloon(const char *title, const char *text)
{
}

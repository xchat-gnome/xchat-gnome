/*
 * preferences.h - interface to storing preferences
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
#include "preferences.h"
#include "palette.h"
#include "../common/xchat.h"
#include "../common/xchatc.h"
#include "../common/util.h"
#include "gui.h"

static void nickname_changed           (GConfClient *client, guint cnxn_id,
                                        GConfEntry *entry, gpointer data);
static void showcolors_changed         (GConfClient *client, guint cnxn_id,
                                        GConfEntry *entry, gpointer data);
static void colorize_nicknames_changed (GConfClient *client, guint cnxn_id,
                                        GConfEntry *entry, gpointer data);
static void colors_changed             (GConfClient *client, guint cnxn_id,
                                        GConfEntry *entry, gpointer data);

gboolean
preferences_exist (void)
{
	GConfClient *client;
	char *text;

	client = gconf_client_get_default ();

	text = gconf_client_get_string (client, "/apps/xchat/version", NULL);
	if (text == NULL) {
		return FALSE;
	} else {
		g_free (text);
	}

	g_object_unref (client);

	return TRUE;
}

static void
string_preference_changed (GConfClient *client, guint cnxn_id,
                           GConfEntry *entry, gpointer user_data)
{
	gchar *text = gconf_client_get_string (client, entry->key, NULL);
	if (text) {
		strcpy (user_data, text);
		g_free (text);
	}
}

/*
 * This goes through the mechanics of hooking up a gconf notify for
 * the given key, and also calls the callback once to populate the
 * initial value.
 */
static void hook_preference (GConfClient *client, GConfEntry entry, gchar *path,
			     GConfClientNotifyFunc callback, gpointer preference)
{
	entry.key = path;
	callback (client, 0, &entry, preference);
	gconf_client_notify_add (client, entry.key, callback,
	                         preference, NULL, NULL);
}

void
load_preferences (void)
{
	gchar *ddir;
	GConfClient *client = gconf_client_get_default ();
	GConfEntry entry;

	gconf_client_notify_add (client, "/apps/xchat/irc/nickname",
	                         nickname_changed, NULL, NULL, NULL);

	hook_preference(client, entry, "/apps/xchat/irc/realname",
	                string_preference_changed, prefs.realname);
	hook_preference(client, entry, "/apps/xchat/irc/awaymsg",
	                string_preference_changed, prefs.awayreason);
	hook_preference(client, entry, "/apps/xchat/irc/quitmsg",
	                string_preference_changed, prefs.quitreason);
	hook_preference(client, entry, "/apps/xchat/irc/partmsg",
	                string_preference_changed, prefs.partreason);
	hook_preference(client, entry, "/apps/xchat/irc/showcolors",
	                showcolors_changed, NULL);
	hook_preference(client, entry, "/apps/xchat/irc/colorize_nicknames",
			colorize_nicknames_changed, NULL);
	hook_preference(client, entry, "/apps/xchat/irc/color_scheme",
	                colors_changed, NULL);

	g_object_unref (client);

	ddir = get_save_directory ();
	g_utf8_strncpy (prefs.dccdir, ddir, sizeof (prefs.dccdir));
	g_free (ddir);
}

void set_version (void)
{
	GConfClient *client = gconf_client_get_default ();
	gconf_client_set_string (client, "/apps/xchat/version",
	                         PACKAGE_VERSION, NULL);
	g_object_unref (client);
}

void
set_nickname (const gchar *text)
{
	if (NULL == text) {
		return;
	}

	strcpy (prefs.nick1, text);

	gchar *text2;
	text2 = g_strdup_printf ("%s_", text);
	strcpy (prefs.nick2, text2);
	g_free (text2);

	text2 = g_strdup_printf ("%s__", text);
	strcpy (prefs.nick3, text2);
	g_free (text2);
}

static void
nickname_changed (GConfClient *client, guint cnxn_id,
                  GConfEntry *entry, gpointer data)
{
	gchar *text = gconf_client_get_string (client, entry->key, NULL);
	set_nickname (text);
	g_free (text);
}

static void
showcolors_changed (GConfClient *client, guint cnxn_id,
                    GConfEntry *entry, gpointer data)
{
	prefs.stripcolor = (unsigned int)
		(!gconf_client_get_bool (client, entry->key, NULL));
}

static void
colorize_nicknames_changed (GConfClient *client, guint cnxn_id,
			    GConfEntry *entry, gpointer data)
{
	prefs.colorednicks = (unsigned int)
		gconf_client_get_bool (client, entry->key, NULL);
}

static void
colors_changed (GConfClient *client, guint cnxn_id,
                GConfEntry *entry, gpointer data)
{
	gint color_scheme = gconf_client_get_int (client, entry->key, NULL);
	load_colors (color_scheme);
	load_palette (color_scheme);
	gtk_widget_queue_draw (gui.conversation_panel);
}

gchar *
get_save_directory(void)
{
	char *dir;
	if ( !prefs.dccdir || 0 == strlen (prefs.dccdir) || 
	    (prefs.dccdir[0] == '~' && prefs.dccdir[1] == '/') ||
            g_file_test (prefs.dccdir, G_FILE_TEST_IS_DIR) == FALSE ) {
                dir = g_strdup (get_default_download_dir());
	} else {
		dir = g_strdup (prefs.dccdir);
	}

	return dir;
}

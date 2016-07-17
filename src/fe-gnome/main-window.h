/*
 * main-window.h - main GUI window functions
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

#ifndef XCHAT_GNOME_MAIN_WINDOW_H
#define XCHAT_GNOME_MAIN_WINDOW_H

void initialize_main_window(void);
void run_main_window(gboolean fullscreen);
void save_main_window(void);
void rename_main_window(gchar *server, gchar *channel);
void set_nickname_label(struct server *serv, char *newnick);
void set_nickname_color(struct server *serv);
void main_window_set_show_userlist(gboolean show_in_main_window);

#endif

/*
 *      toolbar.h - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2009-2010 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
 *      Copyright 2009-2010 Nick Treleaven <nick(dot)treleaven(at)btinternet(dot)com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id$
 */

#ifndef GEANY_TOOLBAR_H
#define GEANY_TOOLBAR_H


typedef struct GeanyToolbarPrefs
{
	gboolean		visible;
	GtkIconSize		icon_size;
	gint			icon_style;
	gboolean		append_to_menu;
}
GeanyToolbarPrefs;

extern GeanyToolbarPrefs toolbar_prefs;


GtkWidget *toolbar_get_widget_child_by_name(const gchar *name);

GtkWidget *toolbar_get_widget_by_name(const gchar *name);

GtkAction *toolbar_get_action_by_name(const gchar *name);

gint toolbar_get_insert_position(void);

void toolbar_update_ui(void);

void toolbar_apply_settings(void);

void toolbar_show_hide(void);

void toolbar_item_ref(GtkToolItem *item);

GtkWidget *toolbar_init(void);

void toolbar_finalize(void);

void toolbar_configure(GtkWindow *parent);

#endif

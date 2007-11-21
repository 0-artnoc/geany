/*
 *      treeviews.h - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2005-2007 Enrico Tröger <enrico.troeger@uvena.de>
 *      Copyright 2006-2007 Nick Treleaven <nick.treleaven@btinternet.com>
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



#ifndef GEANY_TREEVIEWS_H
#define GEANY_TREEVIEWS_H 1


typedef struct SidebarTreeviews
{
	GtkWidget		*tree_openfiles;
	GtkWidget		*default_tag_tree;
	GtkWidget		*popup_taglist;
	GtkWidget		*popup_openfiles;
} SidebarTreeviews;

extern SidebarTreeviews tv;

enum
{
	SYMBOLS_COLUMN_ICON,
	SYMBOLS_COLUMN_NAME,
	SYMBOLS_COLUMN_LINE,
	SYMBOLS_N_COLUMNS,
};

void treeviews_init();

void treeviews_update_tag_list(gint idx, gboolean update);

void treeviews_openfiles_add(gint idx);

void treeviews_openfiles_update(gint idx);

void treeviews_openfiles_update_all();

void treeviews_select_openfiles_item(gint idx);

void treeviews_remove_document(gint idx);

#endif

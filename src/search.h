/*
 *      search.h - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2006-2009 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
 *      Copyright 2006-2009 Nick Treleaven <nick(dot)treleaven(at)btinternet(dot)com>
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
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $Id$
 */


#ifndef GEANY_SEARCH_H
#define GEANY_SEARCH_H 1

/* the flags given in the search dialog for "find next", also used by the search bar */
typedef struct GeanySearchData
{
	gchar		*text;
	gint		flags;
	gboolean	backwards;
	/* set to TRUE when text was set by a search bar callback to keep track of
	 * search bar background colour */
	gboolean	search_bar;
}
GeanySearchData;

extern GeanySearchData search_data;


typedef struct GeanySearchPrefs
{
	gboolean	suppress_dialogs;
	gboolean	use_current_word;
	gboolean	use_current_file_dir;	/* find in files directory to use on showing dialog */
}
GeanySearchPrefs;

extern GeanySearchPrefs search_prefs;


void search_init(void);

void search_finalize(void);

void search_show_find_dialog(void);

void search_show_replace_dialog(void);

void search_show_find_in_files_dialog(const gchar *dir);

void search_find_usage(const gchar *search_text, gint flags, gboolean in_session);

void search_find_selection(GeanyDocument *doc, gboolean search_backwards);

#endif

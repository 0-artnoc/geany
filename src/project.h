/*
 *      project.h - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2007 Enrico Tröger <enrico.troeger@uvena.de>
 *      Copyright 2007 Nick Treleaven <nick.treleaven@btinternet.com>
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


#ifndef GEANY_PROJECT_H
#define GEANY_PROJECT_H 1


/* structure for representing a project. */
struct _GeanyProject
{
	gchar *name; 			// the name of the project
	gchar *description; 	// short description of the project

	gchar *file_name; 		// where the project file is stored (in UTF-8)

	gchar *base_path;		// base path of the project directory (in UTF-8)
	gchar *run_cmd; 		// project run command (in UTF-8)
	// ...					// fields for build process(run arguments and so on) should be added

	gchar **file_patterns;	// array of filename extension patterns
};

typedef struct
{
	gchar *session_file;
} ProjectPrefs;

extern ProjectPrefs project_prefs;


void project_new();

void project_open();

void project_close();

void project_properties();


gboolean project_load_file(const gchar *locale_file_name);

const gchar *project_get_make_dir();


void project_save_prefs(GKeyFile *config);

void project_load_prefs(GKeyFile *config);

void project_setup_prefs();

void project_apply_prefs();

#endif

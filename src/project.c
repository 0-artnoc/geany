/*
 *      project.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2007-2009 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
 *      Copyright 2007-2009 Nick Treleaven <nick(dot)treleaven(at)btinternet(dot)com>
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

/** @file project.h
 * Project Management.
 */

#include "geany.h"

#include <string.h>
#include <unistd.h>

#include "project.h"
#include "projectprivate.h"

#include "dialogs.h"
#include "support.h"
#include "utils.h"
#include "ui_utils.h"
#include "document.h"
#include "msgwindow.h"
#include "main.h"
#include "keyfile.h"
#ifdef G_OS_WIN32
# include "win32.h"
#endif
#include "build.h"
#include "interface.h"
#include "editor.h"
#include "stash.h"


ProjectPrefs project_prefs = { NULL, FALSE, FALSE };


static GeanyProjectPrivate priv;
static GeanyIndentPrefs indentation;

static GeanyPrefGroup *indent_group = NULL;

static struct
{
	gchar *project_file_path; /* in UTF-8 */
} local_prefs = {NULL};


static gboolean entries_modified;

/* simple struct to keep references to the elements of the properties dialog */
typedef struct _PropertyDialogElements
{
	GtkWidget *dialog;
	GtkWidget *name;
	GtkWidget *description;
	GtkWidget *file_name;
	GtkWidget *base_path;
	GtkWidget *make_in_base_path;
	GtkWidget *run_cmd;
	GtkWidget *patterns;
} PropertyDialogElements;



static gboolean update_config(const PropertyDialogElements *e);
static void on_file_save_button_clicked(GtkButton *button, PropertyDialogElements *e);
static void on_file_open_button_clicked(GtkButton *button, PropertyDialogElements *e);
static gboolean load_config(const gchar *filename);
static gboolean write_config(gboolean emit_signal);
static void on_name_entry_changed(GtkEditable *editable, PropertyDialogElements *e);
static void on_entries_changed(GtkEditable *editable, PropertyDialogElements *e);


#define SHOW_ERR(args) dialogs_show_msgbox(GTK_MESSAGE_ERROR, args)
#define SHOW_ERR1(args,more) dialogs_show_msgbox(GTK_MESSAGE_ERROR, args, more)
#define MAX_NAME_LEN 50
/* "projects" is part of the default project base path so be careful when translating
 * please avoid special characters and spaces, look at the source for details or ask Frank */
#define PROJECT_DIR _("projects")


void project_new()
{
	GtkWidget *vbox;
	GtkWidget *table;
	GtkWidget *image;
	GtkWidget *button;
	GtkWidget *bbox;
	GtkWidget *label;
	PropertyDialogElements *e;
	gint response;

	if (! project_ask_close()) return;

	g_return_if_fail(app->project == NULL);

	e = g_new0(PropertyDialogElements, 1);
	e->dialog = gtk_dialog_new_with_buttons(_("New Project"), GTK_WINDOW(main_widgets.window),
										 GTK_DIALOG_DESTROY_WITH_PARENT,
										 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);

	gtk_widget_set_name(e->dialog, "GeanyDialogProject");
	bbox = gtk_hbox_new(FALSE, 0);
	button = gtk_button_new();
	image = gtk_image_new_from_stock("gtk-new", GTK_ICON_SIZE_BUTTON);
	label = gtk_label_new_with_mnemonic(_("C_reate"));
	gtk_box_pack_start(GTK_BOX(bbox), image, FALSE, FALSE, 3);
	gtk_box_pack_start(GTK_BOX(bbox), label, FALSE, FALSE, 3);
	gtk_container_add(GTK_CONTAINER(button), bbox);
	gtk_dialog_add_action_widget(GTK_DIALOG(e->dialog), button, GTK_RESPONSE_OK);

	vbox = ui_dialog_vbox_new(GTK_DIALOG(e->dialog));

	entries_modified = FALSE;

	table = gtk_table_new(3, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(table), 10);

	label = gtk_label_new(_("Name:"));
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	e->name = gtk_entry_new();
	ui_entry_add_clear_icon(e->name);
	gtk_entry_set_max_length(GTK_ENTRY(e->name), MAX_NAME_LEN);

	ui_table_add_row(GTK_TABLE(table), 0, label, e->name, NULL);

	label = gtk_label_new(_("Filename:"));
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	e->file_name = gtk_entry_new();
	ui_entry_add_clear_icon(e->file_name);
	gtk_entry_set_width_chars(GTK_ENTRY(e->file_name), 30);
	button = gtk_button_new();
	g_signal_connect(button, "clicked", G_CALLBACK(on_file_save_button_clicked), e);
	image = gtk_image_new_from_stock("gtk-open", GTK_ICON_SIZE_BUTTON);
	gtk_container_add(GTK_CONTAINER(button), image);
	bbox = gtk_hbox_new(FALSE, 6);
	gtk_box_pack_start_defaults(GTK_BOX(bbox), e->file_name);
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);

	ui_table_add_row(GTK_TABLE(table), 1, label, bbox, NULL);

	label = gtk_label_new(_("Base path:"));
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	e->base_path = gtk_entry_new();
	ui_entry_add_clear_icon(e->base_path);
	ui_widget_set_tooltip_text(e->base_path,
		_("Base directory of all files that make up the project. "
		"This can be a new path, or an existing directory tree. "
		"You can use paths relative to the project filename."));
	bbox = ui_path_box_new(_("Choose Project Base Path"),
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, GTK_ENTRY(e->base_path));

	ui_table_add_row(GTK_TABLE(table), 2, label, bbox, NULL);

	gtk_container_add(GTK_CONTAINER(vbox), table);

	/* signals */
	g_signal_connect(e->name, "changed", G_CALLBACK(on_name_entry_changed), e);
	/* run the callback manually to initialise the base_path and file_name fields */
	on_name_entry_changed(GTK_EDITABLE(e->name), e);

	g_signal_connect(e->file_name, "changed", G_CALLBACK(on_entries_changed), e);
	g_signal_connect(e->base_path, "changed", G_CALLBACK(on_entries_changed), e);

	gtk_widget_show_all(e->dialog);

	retry:
	response = gtk_dialog_run(GTK_DIALOG(e->dialog));
	if (response == GTK_RESPONSE_OK)
		if (! update_config(e))
			goto retry;

	gtk_widget_destroy(e->dialog);
	g_free(e);
}


gboolean project_load_file_with_session(const gchar *locale_file_name)
{
	if (project_load_file(locale_file_name))
	{
		if (project_prefs.project_session)
		{
			configuration_open_files();
			/* open a new file if no other file was opened */
			document_new_file_if_non_open();
		}
		return TRUE;
	}
	return FALSE;
}


#ifndef G_OS_WIN32
static void run_open_dialog(GtkDialog *dialog)
{
	gint response;

	retry:
	response = gtk_dialog_run(dialog);

	if (response == GTK_RESPONSE_ACCEPT)
	{
		gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		/* try to load the config */
		if (! project_load_file_with_session(filename))
		{
			gchar *utf8_filename = utils_get_utf8_from_locale(filename);

			SHOW_ERR1(_("Project file \"%s\" could not be loaded."), utf8_filename);
			gtk_widget_grab_focus(GTK_WIDGET(dialog));
			g_free(utf8_filename);
			g_free(filename);
			goto retry;
		}
		g_free(filename);
	}
}
#endif


void project_open()
{
	const gchar *dir = local_prefs.project_file_path;
#ifdef G_OS_WIN32
	gchar *file;
#else
	GtkWidget *dialog;
	GtkFileFilter *filter;
	gchar *locale_path;
#endif
	if (! project_ask_close()) return;

#ifdef G_OS_WIN32
	file = win32_show_project_open_dialog(main_widgets.window, _("Open Project"), dir, FALSE, TRUE);
	if (file != NULL)
	{
		/* try to load the config */
		if (! project_load_file_with_session(file))
		{
			SHOW_ERR1(_("Project file \"%s\" could not be loaded."), file);
		}
		g_free(file);
	}
#else

	dialog = gtk_file_chooser_dialog_new(_("Open Project"), GTK_WINDOW(main_widgets.window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_widget_set_name(dialog, "GeanyDialogProject");

	/* set default Open, so pressing enter can open multiple files */
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(main_widgets.window));
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

	/* add FileFilters */
	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("All files"));
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("Project files"));
	gtk_file_filter_add_pattern(filter, "*." GEANY_PROJECT_EXT);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filter);

	locale_path = utils_get_locale_from_utf8(dir);
	if (g_file_test(locale_path, G_FILE_TEST_EXISTS) &&
		g_file_test(locale_path, G_FILE_TEST_IS_DIR))
	{
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), locale_path);
	}
	g_free(locale_path);

	gtk_widget_show_all(dialog);
	run_open_dialog(GTK_DIALOG(dialog));
	gtk_widget_destroy(GTK_WIDGET(dialog));
#endif
}


/* Called when creating, opening, closing and updating projects. */
static void update_ui(void)
{
	ui_set_window_title(NULL);
	build_menu_update(NULL);
}


/* open_default will make function reload default session files on close */
void project_close(gboolean open_default)
{
	g_return_if_fail(app->project != NULL);

	ui_set_statusbar(TRUE, _("Project \"%s\" closed."), app->project->name);

	/* use write_config() to save project session files */
	write_config(FALSE);

	g_free(app->project->name);
	g_free(app->project->description);
	g_free(app->project->file_name);
	g_free(app->project->base_path);
	g_free(app->project->run_cmd);

	g_free(app->project);
	app->project = NULL;

	if (project_prefs.project_session)
	{
		/* close all existing tabs first */
		document_close_all();

		/* after closing all tabs let's open the tabs found in the default config */
		if (open_default == TRUE && cl_options.load_session)
		{
			configuration_reload_default_session();
			configuration_open_files();
			/* open a new file if no other file was opened */
			document_new_file_if_non_open();
		}
	}
	g_signal_emit_by_name(geany_object, "project-close");

	update_ui();
}


static void create_properties_dialog(PropertyDialogElements *e)
{
	GtkWidget *table, *notebook;
	GtkWidget *image;
	GtkWidget *button;
	GtkWidget *bbox;
	GtkWidget *label;
	GtkWidget *swin;

	e->dialog = create_project_dialog();
	gtk_window_set_transient_for(GTK_WINDOW(e->dialog), GTK_WINDOW(main_widgets.window));
	gtk_window_set_destroy_with_parent(GTK_WINDOW(e->dialog), TRUE);
	gtk_widget_set_name(e->dialog, "GeanyDialogProject");

	ui_entry_add_clear_icon(ui_lookup_widget(e->dialog, "spin_indent_width"));
	ui_entry_add_clear_icon(ui_lookup_widget(e->dialog, "spin_tab_width"));

	table = gtk_table_new(6, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 6);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(table), 10);

	label = gtk_label_new(_("Name:"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), -1, 0);

	e->name = gtk_entry_new();
	ui_entry_add_clear_icon(e->name);
	gtk_entry_set_max_length(GTK_ENTRY(e->name), MAX_NAME_LEN);
	gtk_table_attach(GTK_TABLE(table), e->name, 1, 2, 0, 1,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);

	label = gtk_label_new(_("Filename:"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), -1, 0);

	e->file_name = gtk_entry_new();
	ui_entry_add_clear_icon(e->file_name);
	gtk_editable_set_editable(GTK_EDITABLE(e->file_name), FALSE);	/* read-only */
	gtk_table_attach(GTK_TABLE(table), e->file_name, 1, 2, 1, 2,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);

	label = gtk_label_new(_("Description:"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), -1, 0);

	e->description = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(e->description), GTK_WRAP_WORD);
	swin = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(swin, 250, 80);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swin),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(swin), GTK_WIDGET(e->description));
	gtk_table_attach(GTK_TABLE(table), swin, 1, 2, 2, 3,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);

	label = gtk_label_new(_("Base path:"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 3, 4,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), -1, 0);

	e->base_path = gtk_entry_new();
	ui_entry_add_clear_icon(e->base_path);
	ui_widget_set_tooltip_text(e->base_path,
		_("Base directory of all files that make up the project. "
		"This can be a new path, or an existing directory tree. "
		"You can use paths relative to the project filename."));
	bbox = ui_path_box_new(_("Choose Project Base Path"),
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, GTK_ENTRY(e->base_path));
	gtk_table_attach(GTK_TABLE(table), bbox, 1, 2, 3, 4,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);

	e->make_in_base_path = gtk_check_button_new_with_label(_("Make in base path"));
	gtk_table_attach(GTK_TABLE(table), e->make_in_base_path, 0, 3, 4, 5,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);

	label = gtk_label_new(_("Run command:"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 5, 6,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), -1, 0);

	e->run_cmd = gtk_entry_new();
	ui_entry_add_clear_icon(e->run_cmd);
	ui_widget_set_tooltip_text(e->run_cmd,
		_("Command-line to run in the project base directory. "
		"Options can be appended to the command. "
		"Leave blank to use the default run command."));
	button = gtk_button_new();
	g_signal_connect(button, "clicked", G_CALLBACK(on_file_open_button_clicked), e->run_cmd);
	image = gtk_image_new_from_stock("gtk-open", GTK_ICON_SIZE_BUTTON);
	gtk_container_add(GTK_CONTAINER(button), image);
	bbox = gtk_hbox_new(FALSE, 6);
	gtk_box_pack_start_defaults(GTK_BOX(bbox), e->run_cmd);
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);
	gtk_table_attach(GTK_TABLE(table), bbox, 1, 2, 5, 6,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);

#if 0
	label = gtk_label_new(_("File patterns:"));
	/* <small>Separate multiple patterns by a new line</small> */
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 6, 7,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), -1, 0);

	e->patterns = gtk_text_view_new();
	swin = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(swin, -1, 80);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swin),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(swin), GTK_WIDGET(e->patterns));
	gtk_table_attach(GTK_TABLE(table), swin, 1, 2, 6, 7,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
#endif

	notebook = ui_lookup_widget(e->dialog, "project_notebook");
	label = gtk_label_new(_("Project"));
	gtk_widget_show(table);	/* needed to switch current page */
	gtk_notebook_insert_page(GTK_NOTEBOOK(notebook), table, label, 0);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
}


void project_properties()
{
	PropertyDialogElements *e = g_new(PropertyDialogElements, 1);
	GeanyProject *p = app->project;
	gint response;

	g_return_if_fail(app->project != NULL);

	entries_modified = FALSE;

	create_properties_dialog(e);

	stash_group_display(indent_group, e->dialog);

	/* fill the elements with the appropriate data */
	gtk_entry_set_text(GTK_ENTRY(e->name), p->name);

	if (p->description != NULL)
	{	/* set text */
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(e->description));
		gtk_text_buffer_set_text(buffer, p->description, -1);
	}

#if 0
	if (p->file_patterns != NULL)
	{	/* set the file patterns */
		gint i;
		gint len = g_strv_length(p->file_patterns);
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(e->patterns));
		GString *str = g_string_sized_new(len * 4);

		for (i = 0; i < len; i++)
		{
			if (p->file_patterns[i] != NULL)
			{
				g_string_append(str, p->file_patterns[i]);
				g_string_append_c(str, '\n');
			}
		}
		gtk_text_buffer_set_text(buffer, str->str, -1);
		g_string_free(str, TRUE);
	}
#endif

	gtk_entry_set_text(GTK_ENTRY(e->file_name), p->file_name);
	gtk_entry_set_text(GTK_ENTRY(e->base_path), p->base_path);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(e->make_in_base_path),
		p->make_in_base_path);
	if (p->run_cmd != NULL)
		gtk_entry_set_text(GTK_ENTRY(e->run_cmd), p->run_cmd);

	gtk_widget_show_all(e->dialog);

	retry:
	response = gtk_dialog_run(GTK_DIALOG(e->dialog));
	if (response == GTK_RESPONSE_OK)
	{
		if (! update_config(e))
			goto retry;

		stash_group_update(indent_group, e->dialog);
	}

	gtk_widget_destroy(e->dialog);
	g_free(e);
}


/* checks whether there is an already open project and asks the user if he wants to close it or
 * abort the current action. Returns FALSE when the current action(the caller) should be cancelled
 * and TRUE if we can go ahead */
gboolean project_ask_close(void)
{
	if (app->project != NULL)
	{
		if (dialogs_show_question_full(NULL, GTK_STOCK_CLOSE, GTK_STOCK_CANCEL,
			_("Do you want to close it before proceeding?"),
			_("The '%s' project is already open."), app->project->name))
		{
			project_close(FALSE);
			return TRUE;
		}
		else
			return FALSE;
	}
	else
		return TRUE;
}


static GeanyProject *create_project(void)
{
	GeanyProject *project = g_new0(GeanyProject, 1);

	memset(&priv, 0, sizeof priv);
	indentation = *editor_get_indent_prefs(NULL);
	priv.indentation = &indentation;
	project->priv = &priv;

	app->project = project;
	return project;
}


/* Verifies data for New & Properties dialogs.
 * Returns: FALSE if the user needs to change any data. */
static gboolean update_config(const PropertyDialogElements *e)
{
	const gchar *name, *file_name, *base_path;
	gchar *locale_filename;
	gint name_len;
	gint err_code = 0;
	gboolean new_project = FALSE;
	GeanyProject *p;

	g_return_val_if_fail(e != NULL, TRUE);

	name = gtk_entry_get_text(GTK_ENTRY(e->name));
	name_len = strlen(name);
	if (name_len == 0)
	{
		SHOW_ERR(_("The specified project name is too short."));
		gtk_widget_grab_focus(e->name);
		return FALSE;
	}
	else if (name_len > MAX_NAME_LEN)
	{
		SHOW_ERR1(_("The specified project name is too long (max. %d characters)."), MAX_NAME_LEN);
		gtk_widget_grab_focus(e->name);
		return FALSE;
	}

	file_name = gtk_entry_get_text(GTK_ENTRY(e->file_name));
	if (! NZV(file_name))
	{
		SHOW_ERR(_("You have specified an invalid project filename."));
		gtk_widget_grab_focus(e->file_name);
		return FALSE;
	}

	locale_filename = utils_get_locale_from_utf8(file_name);
	base_path = gtk_entry_get_text(GTK_ENTRY(e->base_path));
	if (NZV(base_path))
	{	/* check whether the given directory actually exists */
		gchar *locale_path = utils_get_locale_from_utf8(base_path);

		if (! g_path_is_absolute(locale_path))
		{	/* relative base path, so add base dir of project file name */
			gchar *dir = g_path_get_dirname(locale_filename);
			setptr(locale_path, g_strconcat(dir, G_DIR_SEPARATOR_S, locale_path, NULL));
			g_free(dir);
		}

		if (! g_file_test(locale_path, G_FILE_TEST_IS_DIR))
		{
			gboolean create_dir;

			create_dir = dialogs_show_question_full(NULL, GTK_STOCK_OK, GTK_STOCK_CANCEL,
				_("Create the project's base path directory?"),
				_("The path \"%s\" does not exist."),
				base_path);

			if (create_dir)
				err_code = utils_mkdir(locale_path, TRUE);

			if (! create_dir || err_code != 0)
			{
				if (err_code != 0)
					SHOW_ERR1(_("Project base directory could not be created (%s)."),
						g_strerror(err_code));
				gtk_widget_grab_focus(e->base_path);
				utils_free_pointers(2, locale_path, locale_filename, NULL);
				return FALSE;
			}
		}
		g_free(locale_path);
	}
	/* finally test whether the given project file can be written */
	if ((err_code = utils_is_file_writeable(locale_filename)) != 0)
	{
		SHOW_ERR1(_("Project file could not be written (%s)."), g_strerror(err_code));
		gtk_widget_grab_focus(e->file_name);
		g_free(locale_filename);
		return FALSE;
	}
	g_free(locale_filename);

	if (app->project == NULL)
	{
		create_project();
		new_project = TRUE;
	}
	p = app->project;

	setptr(p->name, g_strdup(name));
	setptr(p->file_name, g_strdup(file_name));
	/* use "." if base_path is empty */
	setptr(p->base_path, g_strdup(NZV(base_path) ? base_path : "./"));

	if (! new_project)	/* save properties specific fields */
	{
		GtkTextIter start, end;
		/*gchar *tmp;*/
		GtkTextBuffer *buffer;

		p->make_in_base_path = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(e->make_in_base_path));
		setptr(p->run_cmd, g_strdup(gtk_entry_get_text(GTK_ENTRY(e->run_cmd))));

		/* get and set the project description */
		buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(e->description));
		gtk_text_buffer_get_start_iter(buffer, &start);
		gtk_text_buffer_get_end_iter(buffer, &end);
		setptr(p->description, g_strdup(gtk_text_buffer_get_text(buffer, &start, &end, FALSE)));

#if 0
		/* get and set the project file patterns */
		buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(e->patterns));
		gtk_text_buffer_get_start_iter(buffer, &start);
		gtk_text_buffer_get_end_iter(buffer, &end);
		tmp = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
		g_strfreev(p->file_patterns);
		p->file_patterns = g_strsplit(tmp, "\n", -1);
		g_free(tmp);
#endif
	}
	write_config(TRUE);
	if (new_project)
		ui_set_statusbar(TRUE, _("Project \"%s\" created."), p->name);
	else
		ui_set_statusbar(TRUE, _("Project \"%s\" saved."), p->name);

	update_ui();

	return TRUE;
}


#ifndef G_OS_WIN32
static void run_dialog(GtkWidget *dialog, GtkWidget *entry)
{
	/* set filename in the file chooser dialog */
	const gchar *utf8_filename = gtk_entry_get_text(GTK_ENTRY(entry));
	gchar *locale_filename = utils_get_locale_from_utf8(utf8_filename);

	if (g_path_is_absolute(locale_filename))
	{
		if (g_file_test(locale_filename, G_FILE_TEST_EXISTS))
		{
			/* if the current filename is a directory, we must use
			 * gtk_file_chooser_set_current_folder(which expects a locale filename) otherwise
			 * we end up in the parent directory */
			if (g_file_test(locale_filename, G_FILE_TEST_IS_DIR))
				gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), locale_filename);
			else
				gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), utf8_filename);
		}
		else /* if the file doesn't yet exist, use at least the current directory */
		{
			gchar *locale_dir = g_path_get_dirname(locale_filename);
			gchar *name = g_path_get_basename(utf8_filename);

			if (g_file_test(locale_dir, G_FILE_TEST_EXISTS))
				gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), locale_dir);
			gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), name);

			g_free(name);
			g_free(locale_dir);
		}
	}
	else
	if (gtk_file_chooser_get_action(GTK_FILE_CHOOSER(dialog)) != GTK_FILE_CHOOSER_ACTION_OPEN)
	{
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), utf8_filename);
	}
	g_free(locale_filename);

	/* run it */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		gchar *tmp_utf8_filename = utils_get_utf8_from_locale(filename);

		gtk_entry_set_text(GTK_ENTRY(entry), tmp_utf8_filename);

		g_free(tmp_utf8_filename);
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
}
#endif


static void on_file_save_button_clicked(GtkButton *button, PropertyDialogElements *e)
{
#ifdef G_OS_WIN32
	gchar *path = win32_show_project_open_dialog(e->dialog, _("Choose Project Filename"),
						gtk_entry_get_text(GTK_ENTRY(e->file_name)), TRUE, TRUE);
	if (path != NULL)
	{
		gtk_entry_set_text(GTK_ENTRY(e->file_name), path);
		g_free(path);
	}
#else
	GtkWidget *dialog;

	/* initialise the dialog */
	dialog = gtk_file_chooser_dialog_new(_("Choose Project Filename"), NULL,
					GTK_FILE_CHOOSER_ACTION_SAVE,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_widget_set_name(dialog, "GeanyDialogProject");
	gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

	run_dialog(dialog, e->file_name);
#endif
}


static void on_file_open_button_clicked(GtkButton *button, PropertyDialogElements *e)
{
#ifdef G_OS_WIN32
	gchar *path = win32_show_project_open_dialog(e->dialog, _("Choose Project Run Command"),
						gtk_entry_get_text(GTK_ENTRY(e->run_cmd)), FALSE, FALSE);
	if (path != NULL)
	{
		gtk_entry_set_text(GTK_ENTRY(e->run_cmd), path);
		g_free(path);
	}
#else
	GtkWidget *dialog;

	/* initialise the dialog */
	dialog = gtk_file_chooser_dialog_new(_("Choose Project Run Command"), NULL,
					GTK_FILE_CHOOSER_ACTION_OPEN,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_widget_set_name(dialog, "GeanyDialog");
	gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

	run_dialog(dialog, e->run_cmd);
#endif
}


/* sets the project base path and the project file name according to the project name */
static void on_name_entry_changed(GtkEditable *editable, PropertyDialogElements *e)
{
	gchar *base_path;
	gchar *file_name;
	gchar *name;
	const gchar *project_dir = local_prefs.project_file_path;

	if (entries_modified)
		return;

	name = gtk_editable_get_chars(editable, 0, -1);
	if (name != NULL && strlen(name) > 0)
	{
		base_path = g_strconcat(project_dir, G_DIR_SEPARATOR_S,
			name, G_DIR_SEPARATOR_S, NULL);
		if (project_prefs.project_file_in_basedir)
			file_name = g_strconcat(project_dir, G_DIR_SEPARATOR_S, name, G_DIR_SEPARATOR_S,
				name, "." GEANY_PROJECT_EXT, NULL);
		else
			file_name = g_strconcat(project_dir, G_DIR_SEPARATOR_S,
				name, "." GEANY_PROJECT_EXT, NULL);
	}
	else
	{
		base_path = g_strconcat(project_dir, G_DIR_SEPARATOR_S, NULL);
		file_name = g_strconcat(project_dir, G_DIR_SEPARATOR_S, NULL);
	}
	g_free(name);

	gtk_entry_set_text(GTK_ENTRY(e->base_path), base_path);
	gtk_entry_set_text(GTK_ENTRY(e->file_name), file_name);

	entries_modified = FALSE;

	g_free(base_path);
	g_free(file_name);
}


static void on_entries_changed(GtkEditable *editable, PropertyDialogElements *e)
{
	entries_modified = TRUE;
}


gboolean project_load_file(const gchar *locale_file_name)
{
	g_return_val_if_fail(locale_file_name != NULL, FALSE);

	if (load_config(locale_file_name))
	{
		gchar *utf8_filename = utils_get_utf8_from_locale(locale_file_name);

		ui_set_statusbar(TRUE, _("Project \"%s\" opened."), app->project->name);

		ui_add_recent_project_file(utf8_filename);
		g_free(utf8_filename);
		return TRUE;
	}
	else
	{
		gchar *utf8_filename = utils_get_utf8_from_locale(locale_file_name);

		ui_set_statusbar(TRUE, _("Project file \"%s\" could not be loaded."), utf8_filename);
		g_free(utf8_filename);
	}
	return FALSE;
}


/* Reads the given filename and creates a new project with the data found in the file.
 * At this point there should not be an already opened project in Geany otherwise it will just
 * return.
 * The filename is expected in the locale encoding. */
static gboolean load_config(const gchar *filename)
{
	GKeyFile *config;
	GeanyProject *p;

	/* there should not be an open project */
	g_return_val_if_fail(app->project == NULL && filename != NULL, FALSE);

	config = g_key_file_new();
	if (! g_key_file_load_from_file(config, filename, G_KEY_FILE_NONE, NULL))
	{
		g_key_file_free(config);
		return FALSE;
	}

	p = create_project();

	stash_group_load_from_key_file(indent_group, config);

	p->name = utils_get_setting_string(config, "project", "name", GEANY_STRING_UNTITLED);
	p->description = utils_get_setting_string(config, "project", "description", "");
	p->file_name = utils_get_utf8_from_locale(filename);
	p->base_path = utils_get_setting_string(config, "project", "base_path", "");
	p->make_in_base_path = utils_get_setting_boolean(config, "project", "make_in_base_path", TRUE);
	p->run_cmd = utils_get_setting_string(config, "project", "run_cmd", "");
	p->file_patterns = g_key_file_get_string_list(config, "project", "file_patterns", NULL, NULL);

	if (project_prefs.project_session)
	{
		/* save current (non-project) session (it could has been changed since program startup) */
		configuration_save_default_session();
		/* now close all open files */
		document_close_all();
		/* read session files so they can be opened with configuration_open_files() */
		configuration_load_session_files(config);
	}
	g_signal_emit_by_name(geany_object, "project-open", config);
	g_key_file_free(config);

	update_ui();
	return TRUE;
}


/* Write the project settings as well as the project session files into its configuration files.
 * emit_signal defines whether the project-save signal should be emitted. When write_config()
 * is called while closing a project, this is used to skip emitting the signal because
 * project-close will be emitted afterwards.
 * Returns: TRUE if project file was written successfully. */
static gboolean write_config(gboolean emit_signal)
{
	GeanyProject *p;
	GKeyFile *config;
	gchar *filename;
	gchar *data;
	gboolean ret = FALSE;

	g_return_val_if_fail(app->project != NULL, FALSE);

	p = app->project;

	config = g_key_file_new();
	/* try to load an existing config to keep manually added comments */
	filename = utils_get_locale_from_utf8(p->file_name);
	g_key_file_load_from_file(config, filename, G_KEY_FILE_NONE, NULL);

	stash_group_save_to_key_file(indent_group, config);

	g_key_file_set_string(config, "project", "name", p->name);
	g_key_file_set_string(config, "project", "base_path", p->base_path);

	if (p->description)
		g_key_file_set_string(config, "project", "description", p->description);
	g_key_file_set_boolean(config, "project", "make_in_base_path", p->make_in_base_path);
	if (p->run_cmd)
		g_key_file_set_string(config, "project", "run_cmd", p->run_cmd);
	if (p->file_patterns)
		g_key_file_set_string_list(config, "project", "file_patterns",
			(const gchar**) p->file_patterns, g_strv_length(p->file_patterns));

	/* store the session files into the project too */
	if (project_prefs.project_session)
		configuration_save_session_files(config);

	if (emit_signal)
	{
		g_signal_emit_by_name(geany_object, "project-save", config);
	}
	/* write the file */
	data = g_key_file_to_data(config, NULL, NULL);
	ret = (utils_write_file(filename, data) == 0);

	g_free(data);
	g_free(filename);
	g_key_file_free(config);

	return ret;
}


/* Constructs the project's base path which is used for "Make all" and "Execute".
 * The result is an absolute string in UTF-8 encoding which is either the same as
 * base path if it is absolute or it is built out of project file name's dir and base_path.
 * If there is no project or project's base_path is invalid, NULL will be returned.
 * The returned string should be freed when no longer needed. */
gchar *project_get_base_path()
{
	GeanyProject *project = app->project;

	if (project && NZV(project->base_path))
	{
		if (g_path_is_absolute(project->base_path))
			return g_strdup(project->base_path);
		else
		{	/* build base_path out of project file name's dir and base_path */
			gchar *path;
			gchar *dir = g_path_get_dirname(project->file_name);

			path = g_strconcat(dir, G_DIR_SEPARATOR_S, project->base_path, NULL);
			g_free(dir);
			return path;
		}
	}
	return NULL;
}


/* Returns: NULL if the default path should be used, or a UTF-8 path.
 * Maybe in future this will support a separate project make path from base path. */
gchar *project_get_make_dir()
{
	GeanyProject *project = app->project;

	if (project && ! project->make_in_base_path)
		return NULL;
	else
		return project_get_base_path();
}


/* This is to save project-related global settings, NOT project file settings. */
void project_save_prefs(GKeyFile *config)
{
	GeanyProject *project = app->project;

	if (cl_options.load_session)
	{
		gchar *utf8_filename = (project == NULL) ? "" : project->file_name;

		g_key_file_set_string(config, "project", "session_file", utf8_filename);
	}
	g_key_file_set_string(config, "project", "project_file_path",
		NVL(local_prefs.project_file_path, ""));
}


void project_load_prefs(GKeyFile *config)
{
	if (cl_options.load_session)
	{
		g_return_if_fail(project_prefs.session_file == NULL);
		project_prefs.session_file = utils_get_setting_string(config, "project",
			"session_file", "");
	}
	local_prefs.project_file_path = utils_get_setting_string(config, "project",
		"project_file_path", NULL);
	if (local_prefs.project_file_path == NULL)
	{
		local_prefs.project_file_path = g_strconcat(g_get_home_dir(),
			G_DIR_SEPARATOR_S, PROJECT_DIR, NULL);
	}
}


/* Initialize project-related preferences in the Preferences dialog. */
void project_setup_prefs()
{
	GtkWidget *path_entry = ui_lookup_widget(ui_widgets.prefs_dialog, "project_file_path_entry");
	GtkWidget *path_btn = ui_lookup_widget(ui_widgets.prefs_dialog, "project_file_path_button");

	g_return_if_fail(local_prefs.project_file_path != NULL);
	gtk_entry_set_text(GTK_ENTRY(path_entry), local_prefs.project_file_path);
	ui_setup_open_button_callback(path_btn, NULL,
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, GTK_ENTRY(path_entry));
}


/* Update project-related preferences after using the Preferences dialog. */
void project_apply_prefs()
{
	GtkWidget *path_entry = ui_lookup_widget(ui_widgets.prefs_dialog, "project_file_path_entry");
	const gchar *str;

	str = gtk_entry_get_text(GTK_ENTRY(path_entry));
	setptr(local_prefs.project_file_path, g_strdup(str));
}


void project_init(void)
{
	GeanyPrefGroup *group;

	group = stash_group_new("indentation");
	/* defaults are copied from editor indent prefs */
	stash_group_set_use_defaults(group, FALSE);
	indent_group = group;

	stash_group_add_spin_button_integer(group, &indentation.width,
		"indent_width", 4, "spin_indent_width");
	stash_group_add_radio_buttons(group, (gint*)(gpointer)&indentation.type,
		"indent_type", GEANY_INDENT_TYPE_TABS,
		"radio_indent_spaces", GEANY_INDENT_TYPE_SPACES,
		"radio_indent_tabs", GEANY_INDENT_TYPE_TABS,
		"radio_indent_both", GEANY_INDENT_TYPE_BOTH,
		NULL);
	stash_group_add_spin_button_integer(group, &indentation.hard_tab_width,
		"indent_hard_tab_width", 8, "spin_tab_width");
	stash_group_add_toggle_button(group, &indentation.detect_type,
		"detect_indent", FALSE, "check_detect_indent");
	stash_group_add_combo_box(group, (gint*)(gpointer)&indentation.auto_indent_mode,
		"indent_mode", GEANY_AUTOINDENT_CURRENTCHARS, "combo_auto_indent_mode");
}


void project_finalize(void)
{
	stash_group_free(indent_group);
}


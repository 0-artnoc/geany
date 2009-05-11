/*
 *      dialogs.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2005-2009 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
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

/*
 * File related dialogs, miscellaneous dialogs, font dialog.
 */

#include "geany.h"

#include <gdk/gdkkeysyms.h>
#include <string.h>

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <time.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

/* gstdio.h also includes sys/stat.h */
#include <glib/gstdio.h>

#include "dialogs.h"

#include "callbacks.h"
#include "document.h"
#include "filetypes.h"
#include "win32.h"
#include "sciwrappers.h"
#include "support.h"
#include "utils.h"
#include "ui_utils.h"
#include "keybindings.h"
#include "encodings.h"
#include "build.h"
#include "main.h"
#include "project.h"


enum
{
	GEANY_RESPONSE_RENAME,
	GEANY_RESPONSE_VIEW
};

#if ! GEANY_USE_WIN32_DIALOG
static GtkWidget *add_file_open_extra_widget(void);


static void
on_file_open_dialog_response           (GtkDialog *dialog,
                                        gint response,
                                        gpointer user_data)
{
	gtk_widget_hide(ui_widgets.open_filesel);

	if (response == GTK_RESPONSE_ACCEPT || response == GEANY_RESPONSE_VIEW)
	{
		GSList *filelist;
		gint filetype_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(
						ui_lookup_widget(GTK_WIDGET(dialog), "filetype_combo")));
		gint encoding_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(
						ui_lookup_widget(GTK_WIDGET(dialog), "encoding_combo")));
		GeanyFiletype *ft = NULL;
		gchar *charset = NULL;
		gboolean ro = (response == GEANY_RESPONSE_VIEW);	/* View clicked */

		/* ignore detect from file item */
		if (filetype_idx > 0 && filetype_idx < GEANY_MAX_BUILT_IN_FILETYPES)
			ft = g_slist_nth_data(filetypes_by_title, filetype_idx);
		if (encoding_idx >= 0 && encoding_idx < GEANY_ENCODINGS_MAX)
			charset = encodings[encoding_idx].charset;

		filelist = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(ui_widgets.open_filesel));
		if (filelist != NULL)
		{
			document_open_files(filelist, ro, ft, charset);
			g_slist_foreach(filelist, (GFunc) g_free, NULL);	/* free filenames */
		}
		g_slist_free(filelist);
	}
	if (app->project && NZV(app->project->base_path))
		gtk_file_chooser_remove_shortcut_folder(GTK_FILE_CHOOSER(ui_widgets.open_filesel),
			app->project->base_path, NULL);
}


static void on_file_open_notify(GObject *filechooser, GParamSpec *pspec, gpointer data)
{
	GValue *value;

	value = g_new0(GValue, 1);
	g_value_init(value, pspec->value_type);
	g_object_get_property(filechooser, pspec->name, value);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
		ui_lookup_widget(GTK_WIDGET(filechooser), "check_hidden")), g_value_get_boolean(value));
}


static void
on_file_open_check_hidden_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	gboolean is_on = gtk_toggle_button_get_active(togglebutton);

	gtk_file_chooser_set_show_hidden(GTK_FILE_CHOOSER(ui_widgets.open_filesel), is_on);
}


static void create_open_file_dialog(void)
{
	GtkWidget *filetype_combo, *encoding_combo;
	GtkWidget *viewbtn;
	guint i;
	gchar *encoding_string;
	GeanyFiletype *ft;
	GSList *node;

	ui_widgets.open_filesel = gtk_file_chooser_dialog_new(_("Open File"), GTK_WINDOW(main_widgets.window),
			GTK_FILE_CHOOSER_ACTION_OPEN, NULL, NULL);
	gtk_widget_set_name(ui_widgets.open_filesel, "GeanyDialog");

	viewbtn = gtk_dialog_add_button(GTK_DIALOG(ui_widgets.open_filesel), _("_View"),
				GEANY_RESPONSE_VIEW);
	ui_widget_set_tooltip_text(viewbtn,
		_("Opens the file in read-only mode. If you choose more than one file to open, all files will be opened read-only."));

	gtk_dialog_add_buttons(GTK_DIALOG(ui_widgets.open_filesel),
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(ui_widgets.open_filesel), GTK_RESPONSE_ACCEPT);

	gtk_widget_set_size_request(ui_widgets.open_filesel, -1, 460);
	gtk_window_set_modal(GTK_WINDOW(ui_widgets.open_filesel), TRUE);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(ui_widgets.open_filesel), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(ui_widgets.open_filesel), FALSE);
	gtk_window_set_type_hint(GTK_WINDOW(ui_widgets.open_filesel), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_window_set_transient_for(GTK_WINDOW(ui_widgets.open_filesel), GTK_WINDOW(main_widgets.window));
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(ui_widgets.open_filesel), TRUE);
	if (gtk_check_version(2, 14, 0) == NULL)
		gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(ui_widgets.open_filesel), FALSE);

	/* add checkboxes and filename entry */
	gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(ui_widgets.open_filesel),
		add_file_open_extra_widget());
	filetype_combo = ui_lookup_widget(ui_widgets.open_filesel, "filetype_combo");

	gtk_combo_box_append_text(GTK_COMBO_BOX(filetype_combo), _("Detect by file extension"));
	/* add FileFilters(start with "All Files") */
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(ui_widgets.open_filesel),
				filetypes_create_file_filter(filetypes[GEANY_FILETYPES_NONE]));
	/* now create meta filter "All Source" */
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(ui_widgets.open_filesel),
				filetypes_create_file_filter_all_source());
	foreach_slist(ft, node, filetypes_by_title)
	{
		if (G_UNLIKELY(ft->id == GEANY_FILETYPES_NONE))
			continue;
		gtk_combo_box_append_text(GTK_COMBO_BOX(filetype_combo), ft->title);
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(ui_widgets.open_filesel),
				filetypes_create_file_filter(ft));
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(filetype_combo), 0);

	/* fill encoding combo box */
	encoding_combo = ui_lookup_widget(ui_widgets.open_filesel, "encoding_combo");
	for (i = 0; i < GEANY_ENCODINGS_MAX; i++)
	{
		encoding_string = encodings_to_string(&encodings[i]);
		gtk_combo_box_append_text(GTK_COMBO_BOX(encoding_combo), encoding_string);
		g_free(encoding_string);
	}
	gtk_combo_box_append_text(GTK_COMBO_BOX(encoding_combo), _("Detect from file"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(encoding_combo), GEANY_ENCODINGS_MAX);

	g_signal_connect(ui_widgets.open_filesel, "notify::show-hidden",
				G_CALLBACK(on_file_open_notify), NULL);
	g_signal_connect(ui_widgets.open_filesel, "delete-event",
				G_CALLBACK(gtk_widget_hide_on_delete), NULL);
	g_signal_connect(ui_widgets.open_filesel, "response",
				G_CALLBACK(on_file_open_dialog_response), NULL);
}
#endif


/* This shows the file selection dialog to open a file. */
void dialogs_show_open_file()
{
	gchar *initdir;

	/* set dialog directory to the current file's directory, if present */
	initdir = utils_get_current_file_dir_utf8();

	/* use project or default startup directory (if set) if no files are open */
	/** TODO should it only be used when initally open the dialog and not on every show? */
	if (! initdir)
		initdir = g_strdup(utils_get_default_dir_utf8());

	setptr(initdir, utils_get_locale_from_utf8(initdir));

#if GEANY_USE_WIN32_DIALOG
	win32_show_file_dialog(TRUE, initdir);
#else /* X11, not win32: use GTK_FILE_CHOOSER */

	/* We use the same file selection widget each time, so first of all we create it
	 * if it hasn't already been created. */
	if (ui_widgets.open_filesel == NULL)
		create_open_file_dialog();

	if (initdir != NULL)
	{
		if (g_path_is_absolute(initdir))
			gtk_file_chooser_set_current_folder(
				GTK_FILE_CHOOSER(ui_widgets.open_filesel), initdir);
	}

	if (app->project && NZV(app->project->base_path))
		gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(ui_widgets.open_filesel),
			app->project->base_path, NULL);

	gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(ui_widgets.open_filesel));
	gtk_window_present(GTK_WINDOW(ui_widgets.open_filesel));
#endif
	g_free(initdir);
}


#if ! GEANY_USE_WIN32_DIALOG
static GtkWidget *add_file_open_extra_widget()
{
	GtkWidget *expander, *vbox, *table, *check_hidden;
	GtkWidget *filetype_ebox, *filetype_label, *filetype_combo;
	GtkWidget *encoding_ebox, *encoding_label, *encoding_combo;

	expander = gtk_expander_new_with_mnemonic(_("_More Options"));
	vbox = gtk_vbox_new(FALSE, 6);
	gtk_container_add(GTK_CONTAINER(expander), vbox);

	table = gtk_table_new(2, 4, FALSE);

	/* line 1 with checkbox and encoding combo */
	check_hidden = gtk_check_button_new_with_mnemonic(_("Show _hidden files"));
	gtk_widget_show(check_hidden);
	gtk_table_attach(GTK_TABLE(table), check_hidden, 0, 1, 0, 1,
					(GtkAttachOptions) (GTK_FILL | GTK_EXPAND),
					(GtkAttachOptions) (0), 0, 5);

	/* spacing */
	gtk_table_attach(GTK_TABLE(table), gtk_label_new(""), 1, 2, 0, 1,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 5, 5);

	encoding_label = gtk_label_new(_("Set encoding:"));
	gtk_misc_set_alignment(GTK_MISC(encoding_label), 1, 0);
	gtk_table_attach(GTK_TABLE(table), encoding_label, 2, 3, 0, 1,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 4, 5);
	/* the ebox is for the tooltip, because gtk_combo_box can't show tooltips */
	encoding_ebox = gtk_event_box_new();
	encoding_combo = gtk_combo_box_new_text();
	gtk_combo_box_set_wrap_width(GTK_COMBO_BOX(encoding_combo), 3);
	ui_widget_set_tooltip_text(encoding_ebox,
		_("Explicitly defines an encoding for the file, if it would not be detected. This is useful when you know that the encoding of a file cannot be detected correctly by Geany.\nNote if you choose multiple files, they will all be opened with the chosen encoding."));
	gtk_container_add(GTK_CONTAINER(encoding_ebox), encoding_combo);
	gtk_table_attach(GTK_TABLE(table), encoding_ebox, 3, 4, 0, 1,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 5);

	/* line 2 with filetype combo */
	filetype_label = gtk_label_new(_("Set filetype:"));
	gtk_misc_set_alignment(GTK_MISC(filetype_label), 1, 0);
	gtk_table_attach(GTK_TABLE(table), filetype_label, 2, 3, 1, 2,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 4, 5);
	/* the ebox is for the tooltip, because gtk_combo_box can't show tooltips */
	filetype_ebox = gtk_event_box_new();
	filetype_combo = gtk_combo_box_new_text();
	gtk_combo_box_set_wrap_width(GTK_COMBO_BOX(filetype_combo), 2);
	ui_widget_set_tooltip_text(filetype_ebox,
		_("Explicitly defines a filetype for the file, if it would not be detected by filename extension.\nNote if you choose multiple files, they will all be opened with the chosen filetype."));
	gtk_container_add(GTK_CONTAINER(filetype_ebox), filetype_combo);
	gtk_table_attach(GTK_TABLE(table), filetype_ebox, 3, 4, 1, 2,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 5);

	gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);
	gtk_widget_show_all(vbox);

	g_signal_connect(check_hidden, "toggled",
				G_CALLBACK(on_file_open_check_hidden_toggled), NULL);

	g_object_set_data_full(G_OBJECT(ui_widgets.open_filesel), "check_hidden",
				g_object_ref(check_hidden), (GDestroyNotify)g_object_unref);
	g_object_set_data_full(G_OBJECT(ui_widgets.open_filesel), "filetype_combo",
				g_object_ref(filetype_combo), (GDestroyNotify)g_object_unref);
	g_object_set_data_full(G_OBJECT(ui_widgets.open_filesel), "encoding_combo",
				g_object_ref(encoding_combo), (GDestroyNotify)g_object_unref);

	return expander;
}
#endif


#if ! GEANY_USE_WIN32_DIALOG
static void on_save_as_new_tab_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	gtk_widget_set_sensitive(GTK_WIDGET(user_data),
		! gtk_toggle_button_get_active(togglebutton));
}
#endif


#if ! GEANY_USE_WIN32_DIALOG
static void handle_save_as(const gchar *utf8_filename, gboolean open_new_tab, gboolean rename_file)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(NZV(utf8_filename));

	if (open_new_tab)
	{	/* "open" the saved file in a new tab and switch to it */
		doc = document_clone(doc, utf8_filename);
		document_save_file_as(doc, NULL);
	}
	else
	{
		if (doc->file_name != NULL)
		{
			if (rename_file)
			{
				document_rename_file(doc, utf8_filename);
			}
			/* create a new tm_source_file object otherwise tagmanager won't work correctly */
			tm_workspace_remove_object(doc->tm_file, TRUE, TRUE);
			doc->tm_file = NULL;
		}
		document_save_file_as(doc, utf8_filename);

		build_menu_update(doc);
	}
}


static void
on_file_save_dialog_response           (GtkDialog *dialog,
                                        gint response,
                                        gpointer user_data)
{
	gboolean rename_file = FALSE;

	switch (response)
	{
		case GEANY_RESPONSE_RENAME:
			rename_file = TRUE;
			/* fall through */
		case GTK_RESPONSE_ACCEPT:
		{
			gchar *new_filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(ui_widgets.save_filesel));
			gboolean open_new_tab = gtk_toggle_button_get_active(
					GTK_TOGGLE_BUTTON(ui_lookup_widget(ui_widgets.save_filesel, "check_open_new_tab")));
			gchar *utf8_filename;

			if (! NZV(new_filename))	/* rename doesn't check for empty filename */
			{
				utils_beep();
				g_free(new_filename);
				return;
			}
			utf8_filename = utils_get_utf8_from_locale(new_filename);

			handle_save_as(utf8_filename, open_new_tab, rename_file);

			g_free(utf8_filename);
			g_free(new_filename);
		}
	}
	gtk_widget_hide(ui_widgets.save_filesel);
}
#endif


#if ! GEANY_USE_WIN32_DIALOG
static void create_save_file_dialog(void)
{
	GtkWidget *vbox, *check_open_new_tab, *rename_btn;

	ui_widgets.save_filesel = gtk_file_chooser_dialog_new(_("Save File"), GTK_WINDOW(main_widgets.window),
				GTK_FILE_CHOOSER_ACTION_SAVE, NULL, NULL);
	gtk_window_set_modal(GTK_WINDOW(ui_widgets.save_filesel), TRUE);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(ui_widgets.save_filesel), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(ui_widgets.save_filesel), FALSE);
	gtk_window_set_type_hint(GTK_WINDOW(ui_widgets.save_filesel), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_widget_set_name(ui_widgets.save_filesel, "GeanyDialog");

	rename_btn = gtk_dialog_add_button(GTK_DIALOG(ui_widgets.save_filesel), _("R_ename"),
					GEANY_RESPONSE_RENAME);
	ui_widget_set_tooltip_text(rename_btn, _("Save the file and rename it"));

	gtk_dialog_add_buttons(GTK_DIALOG(ui_widgets.save_filesel),
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(ui_widgets.save_filesel), GTK_RESPONSE_ACCEPT);

	vbox = gtk_vbox_new(FALSE, 0);
	check_open_new_tab = gtk_check_button_new_with_mnemonic(_("_Open file in a new tab"));
	ui_widget_set_tooltip_text(check_open_new_tab,
		_("Keep the current unsaved document open"
		" and open the newly saved file in a new tab"));
	gtk_box_pack_start(GTK_BOX(vbox), check_open_new_tab, FALSE, FALSE, 0);
	gtk_widget_show_all(vbox);
	gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(ui_widgets.save_filesel), vbox);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(ui_widgets.save_filesel), TRUE);
	if (gtk_check_version(2, 14, 0) == NULL)
		gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(ui_widgets.save_filesel), FALSE);

	g_signal_connect(check_open_new_tab, "toggled",
				G_CALLBACK(on_save_as_new_tab_toggled), rename_btn);

	g_object_set_data_full(G_OBJECT(ui_widgets.save_filesel), "check_open_new_tab",
				g_object_ref(check_open_new_tab), (GDestroyNotify)g_object_unref);

	g_signal_connect(ui_widgets.save_filesel, "delete-event",
		G_CALLBACK(gtk_widget_hide_on_delete), NULL);
	g_signal_connect(ui_widgets.save_filesel, "response",
		G_CALLBACK(on_file_save_dialog_response), NULL);

	gtk_window_set_transient_for(GTK_WINDOW(ui_widgets.save_filesel), GTK_WINDOW(main_widgets.window));
}
#endif


#if ! GEANY_USE_WIN32_DIALOG
static gboolean gtk_show_save_as(const gchar *initdir)
{
	GeanyDocument *doc = document_get_current();
	gint resp;
	gboolean folder_set = FALSE;

	if (G_UNLIKELY(ui_widgets.save_filesel == NULL))
		create_save_file_dialog();

	gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(ui_widgets.save_filesel));

	if (doc->file_name != NULL)
	{
		if (g_path_is_absolute(doc->file_name))
		{
			gchar *locale_filename = utils_get_locale_from_utf8(doc->file_name);
			gchar *locale_basename = g_path_get_basename(locale_filename);
			gchar *locale_dirname = g_path_get_dirname(locale_filename);

			folder_set = TRUE;
			gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(ui_widgets.save_filesel),
				locale_dirname);
			gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(ui_widgets.save_filesel),
				locale_basename);

			g_free(locale_filename);
			g_free(locale_basename);
			g_free(locale_dirname);
		}
		else
			gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(ui_widgets.save_filesel),
				doc->file_name);
	}
	else
	{
		gchar *fname = NULL;

		if (doc->file_type != NULL && doc->file_type->id != GEANY_FILETYPES_NONE &&
			doc->file_type->extension != NULL)
			fname = g_strconcat(GEANY_STRING_UNTITLED, ".",
								doc->file_type->extension, NULL);
		else
			fname = g_strdup(GEANY_STRING_UNTITLED);

		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(ui_widgets.save_filesel), fname);

		g_free(fname);
	}

	if (app->project && NZV(app->project->base_path))
		gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(ui_widgets.save_filesel),
			app->project->base_path, NULL);

	/* if the folder wasn't set so far, we set it to the given directory */
	if (! folder_set && initdir != NULL && g_path_is_absolute(initdir))
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(ui_widgets.save_filesel), initdir);

	/* Run the dialog synchronously, pausing this function call */
	resp = gtk_dialog_run(GTK_DIALOG(ui_widgets.save_filesel));

	if (app->project && NZV(app->project->base_path))
		gtk_file_chooser_remove_shortcut_folder(GTK_FILE_CHOOSER(ui_widgets.save_filesel),
			app->project->base_path, NULL);

	return (resp == GTK_RESPONSE_ACCEPT);
}
#endif


/**
 *  Show the Save As dialog for the current notebook page.
 *
 *  @return @a TRUE if the file was saved, otherwise @a FALSE.
 **/
gboolean dialogs_show_save_as()
{
	gboolean result;
	gchar *initdir = NULL;
	static gboolean initial = TRUE;

	initdir = utils_get_current_file_dir_utf8();

	/* use project or default startup directory (if set) if no files are open */
	if (initdir == NULL && initial)
	{
		initdir = g_strdup(utils_get_default_dir_utf8());
		initial = FALSE;
	}

	setptr(initdir, utils_get_locale_from_utf8(initdir));

#if GEANY_USE_WIN32_DIALOG
	result = win32_show_file_dialog(FALSE, initdir);
#else
	result = gtk_show_save_as(initdir);
#endif
	g_free(initdir);
	return result;
}


/**
 *  Show a message box of the type @c type with @c text.
 *  On Unix-like systems a GTK message dialog box is shown, on Win32 systems a native Windows
 *  message dialog box is shown.
 *
 *  @param type A GtkMessageType, can be one of: GTK_MESSAGE_INFO, GTK_MESSAGE_WARNING,
 *              GTK_MESSAGE_QUESTION, GTK_MESSAGE_ERROR
 *  @param text Printf()-style format string.
 *  @param ... Arguments for the @c text format string.
 **/
void dialogs_show_msgbox(gint type, const gchar *text, ...)
{
#ifndef G_OS_WIN32
	GtkWidget *dialog;
#endif
	gchar string[512];
	va_list args;

	va_start(args, text);
	g_vsnprintf(string, 511, text, args);
	va_end(args);

#ifdef G_OS_WIN32
	win32_message_dialog(NULL, type, string);
#else
	dialog = gtk_message_dialog_new(GTK_WINDOW(main_widgets.window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                  type, GTK_BUTTONS_OK, "%s", string);
	gtk_widget_set_name(dialog, "GeanyDialog");
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
#endif
}


void dialogs_show_msgbox_with_secondary(gint type, const gchar *text, const gchar *secondary)
{
#ifdef G_OS_WIN32
	/* put the two strings together because Windows message boxes don't support secondary texts */
	gchar *string = g_strconcat(text, "\n", secondary, NULL);
	win32_message_dialog(NULL, type, string);
	g_free(string);
#else
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new(GTK_WINDOW(main_widgets.window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                  type, GTK_BUTTONS_OK, "%s", text);
	gtk_widget_set_name(dialog, "GeanyDialog");
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", secondary);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
#endif
}


#ifndef G_OS_WIN32
static gint run_unsaved_dialog(const gchar *msg, const gchar *msg2)
{
	GtkWidget *dialog, *button;
	gint ret;

	dialog = gtk_message_dialog_new(GTK_WINDOW(main_widgets.window), GTK_DIALOG_DESTROY_WITH_PARENT,
                                  GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", msg);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", msg2);
	gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	button = ui_button_new_with_image(GTK_STOCK_CLEAR, _("_Don't save"));
	gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, GTK_RESPONSE_NO);
	gtk_widget_show(button);

	gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_SAVE, GTK_RESPONSE_YES);

	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
	ret = gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);

	return ret;
}
#endif


gboolean dialogs_show_unsaved_file(GeanyDocument *doc)
{
	gchar *msg, *short_fn = NULL;
	const gchar *msg2;
	gint ret;
	gboolean old_quitting_state = main_status.quitting;

	/* display the file tab to remind the user of the document */
	main_status.quitting = FALSE;
	gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),
		document_get_notebook_page(doc));
	main_status.quitting = old_quitting_state;

	short_fn = document_get_basename_for_display(doc, -1);

	msg = g_strdup_printf(_("The file '%s' is not saved."),
		(short_fn != NULL) ? short_fn : GEANY_STRING_UNTITLED);
	msg2 = _("Do you want to save it before closing?");
	g_free(short_fn);

#ifdef G_OS_WIN32
	setptr(msg, g_strconcat(msg, "\n", msg2, NULL));
	ret = win32_message_dialog_unsaved(msg);
#else
	ret = run_unsaved_dialog(msg, msg2);
#endif
	g_free(msg);

	switch(ret)
	{
		case GTK_RESPONSE_YES:
		{
			if (doc->file_name == NULL)
			{
				ret = dialogs_show_save_as();
			}
			else
				/* document_save_file() returns the status if the file could be saved */
				ret = document_save_file(doc, FALSE);
			break;
		}
		case GTK_RESPONSE_NO: ret = TRUE; break;
		case GTK_RESPONSE_CANCEL: /* fall through to default and leave the function */
		default: ret = FALSE; break;
	}

	return (gboolean) ret;
}


#ifndef G_OS_WIN32
static void
on_font_apply_button_clicked           (GtkButton       *button,
                                        gpointer         user_data)
{
	gchar *fontname;

	fontname = gtk_font_selection_dialog_get_font_name(
		GTK_FONT_SELECTION_DIALOG(ui_widgets.open_fontsel));
	ui_set_editor_font(fontname);
	g_free(fontname);
}


static void
on_font_ok_button_clicked              (GtkButton       *button,
                                        gpointer         user_data)
{
	/* We do the same thing as apply, but we close the dialog after. */
	on_font_apply_button_clicked(button, NULL);
	gtk_widget_hide(ui_widgets.open_fontsel);
}


static void
on_font_cancel_button_clicked         (GtkButton       *button,
                                        gpointer         user_data)
{
	gtk_widget_hide(ui_widgets.open_fontsel);
}
#endif


/* This shows the font selection dialog to choose a font. */
void dialogs_show_open_font()
{
#ifdef G_OS_WIN32
	win32_show_font_dialog();
#else

	if (ui_widgets.open_fontsel == NULL)
	{
		ui_widgets.open_fontsel = gtk_font_selection_dialog_new(_("Choose font"));;
		gtk_container_set_border_width(GTK_CONTAINER(ui_widgets.open_fontsel), 4);
		gtk_window_set_modal(GTK_WINDOW(ui_widgets.open_fontsel), TRUE);
		gtk_window_set_destroy_with_parent(GTK_WINDOW(ui_widgets.open_fontsel), TRUE);
		gtk_window_set_skip_taskbar_hint(GTK_WINDOW(ui_widgets.open_fontsel), TRUE);
		gtk_window_set_type_hint(GTK_WINDOW(ui_widgets.open_fontsel), GDK_WINDOW_TYPE_HINT_DIALOG);
		gtk_widget_set_name(ui_widgets.open_fontsel, "GeanyDialog");

		gtk_widget_show(GTK_FONT_SELECTION_DIALOG(ui_widgets.open_fontsel)->apply_button);

		g_signal_connect(ui_widgets.open_fontsel,
					"delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
		g_signal_connect(GTK_FONT_SELECTION_DIALOG(ui_widgets.open_fontsel)->ok_button,
					"clicked", G_CALLBACK(on_font_ok_button_clicked), NULL);
		g_signal_connect(GTK_FONT_SELECTION_DIALOG(ui_widgets.open_fontsel)->cancel_button,
					"clicked", G_CALLBACK(on_font_cancel_button_clicked), NULL);
		g_signal_connect(GTK_FONT_SELECTION_DIALOG(ui_widgets.open_fontsel)->apply_button,
					"clicked", G_CALLBACK(on_font_apply_button_clicked), NULL);

		gtk_font_selection_dialog_set_font_name(
			GTK_FONT_SELECTION_DIALOG(ui_widgets.open_fontsel), interface_prefs.editor_font);
		gtk_window_set_transient_for(GTK_WINDOW(ui_widgets.open_fontsel), GTK_WINDOW(main_widgets.window));
	}
	/* We make sure the dialog is visible. */
	gtk_window_present(GTK_WINDOW(ui_widgets.open_fontsel));
#endif
}


static void
on_input_dialog_show(GtkDialog *dialog, GtkWidget *entry)
{
	gtk_widget_grab_focus(entry);
}


static void
on_input_entry_activate(GtkEntry *entry, GtkDialog *dialog)
{
	gtk_dialog_response(dialog, GTK_RESPONSE_ACCEPT);
}


static void
on_input_numeric_activate(GtkEntry *entry, GtkDialog *dialog)
{
	gtk_dialog_response(dialog, GTK_RESPONSE_ACCEPT);
}


static void
on_input_dialog_response(GtkDialog *dialog,
                         gint response,
                         GtkWidget *entry)
{
	gboolean persistent = (gboolean) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "has_combo"));

	if (response == GTK_RESPONSE_ACCEPT)
	{
		const gchar *str = gtk_entry_get_text(GTK_ENTRY(entry));
		InputCallback input_cb =
			(InputCallback) g_object_get_data(G_OBJECT(dialog), "input_cb");

		if (persistent)
		{
			GtkWidget *combo = (GtkWidget *) g_object_get_data(G_OBJECT(dialog), "combo");
			ui_combo_box_add_to_history(GTK_COMBO_BOX(combo), str);
		}
		input_cb(str);
	}

	if (persistent)
		gtk_widget_hide(GTK_WIDGET(dialog));
	else
		gtk_widget_destroy(GTK_WIDGET(dialog));
}


static void add_input_widgets(GtkWidget *dialog, GtkWidget *vbox,
		const gchar *label_text, const gchar *default_text, gboolean persistent)
{
	GtkWidget *label, *entry;

	label = gtk_label_new(label_text);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_container_add(GTK_CONTAINER(vbox), label);

	if (persistent)	/* remember previous entry text in a combo box */
	{
		GtkWidget *combo = gtk_combo_box_entry_new_text();

		entry = GTK_BIN(combo)->child;
		ui_entry_add_clear_icon(entry);
		g_object_set_data(G_OBJECT(dialog), "combo", combo);
		gtk_container_add(GTK_CONTAINER(vbox), combo);
	}
	else
	{
		entry = gtk_entry_new();
		ui_entry_add_clear_icon(entry);
		gtk_container_add(GTK_CONTAINER(vbox), entry);
	}

	if (default_text != NULL)
	{
		gtk_entry_set_text(GTK_ENTRY(entry), default_text);
	}
	gtk_entry_set_max_length(GTK_ENTRY(entry), 255);
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 30);

	g_signal_connect(entry, "activate", G_CALLBACK(on_input_entry_activate), dialog);
	g_signal_connect(dialog, "show", G_CALLBACK(on_input_dialog_show), entry);
	g_signal_connect(dialog, "response", G_CALLBACK(on_input_dialog_response), entry);
}


/* Create and display an input dialog.
 * persistent: whether to remember previous entry text in a combo box;
 * 	in this case the dialog returned is not destroyed on a response,
 * 	and can be reshown.
 * Returns: the dialog widget. */
GtkWidget *
dialogs_show_input(const gchar *title, const gchar *label_text, const gchar *default_text,
						gboolean persistent, InputCallback input_cb)
{
	GtkWidget *dialog, *vbox;

	dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_widgets.window),
		GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);
	vbox = ui_dialog_vbox_new(GTK_DIALOG(dialog));
	gtk_widget_set_name(dialog, "GeanyDialog");
	gtk_box_set_spacing(GTK_BOX(vbox), 6);

	g_object_set_data(G_OBJECT(dialog), "has_combo", GINT_TO_POINTER(persistent));
	g_object_set_data(G_OBJECT(dialog), "input_cb", (gpointer) input_cb);

	add_input_widgets(dialog, vbox, label_text, default_text, persistent);

	if (persistent)
		g_signal_connect(dialog, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
	else
		g_signal_connect(dialog, "delete-event", G_CALLBACK(gtk_widget_destroy), NULL);

	gtk_widget_show_all(dialog);
	return dialog;
}


/**
 *  Show an input box to enter a numerical value using a GtkSpinButton.
 *  If the dialog is aborted, @c value remains untouched.
 *
 *  @param title The dialog title.
 *  @param label_text The shown dialog label.
 *  @param value The default value for the spin button and the return location of the entered value.
 * 				 Must be non-NULL.
 *  @param min Minimum allowable value (see documentation for @a gtk_spin_button_new_with_range()).
 *  @param max Maximum allowable value (see documentation for @a gtk_spin_button_new_with_range()).
 *  @param step Increment added or subtracted by spinning the widget
 * 				(see documentation for @a gtk_spin_button_new_with_range()).
 *
 *  @return @a TRUE if a value was entered and the dialog closed with 'OK'. @a FALSE otherwise.
 *
 *  @since 0.16
 **/
gboolean dialogs_show_input_numeric(const gchar *title, const gchar *label_text,
									gdouble *value, gdouble min, gdouble max, gdouble step)
{
	GtkWidget *dialog, *label, *spin, *vbox;
	gboolean res = FALSE;

	g_return_val_if_fail(title != NULL, FALSE);
	g_return_val_if_fail(label_text != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);

	dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_widgets.window),
										GTK_DIALOG_DESTROY_WITH_PARENT,
										GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
										GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
	vbox = ui_dialog_vbox_new(GTK_DIALOG(dialog));
	gtk_widget_set_name(dialog, "GeanyDialog");

	label = gtk_label_new(label_text);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	spin = gtk_spin_button_new_with_range(min, max, step);
	ui_entry_add_clear_icon(spin);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), *value);
	g_signal_connect(spin, "activate", G_CALLBACK(on_input_numeric_activate), dialog);

	gtk_container_add(GTK_CONTAINER(vbox), label);
	gtk_container_add(GTK_CONTAINER(vbox), spin);
	gtk_widget_show_all(vbox);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		*value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
		res = TRUE;
	}
	gtk_widget_destroy(dialog);

	return res;
}


void dialogs_show_file_properties(GeanyDocument *doc)
{
	GtkWidget *dialog, *label, *table, *hbox, *image, *perm_table, *check, *vbox;
	gchar *file_size, *title, *base_name, *time_changed, *time_modified, *time_accessed, *enctext;
	gchar *short_name;
#ifdef HAVE_SYS_TYPES_H
	struct stat st;
	off_t filesize;
	mode_t mode;
	gchar *locale_filename;
#else
	gint filesize = 0;
	gint mode = 0;
#endif

/* define this ones, to avoid later trouble */
#ifndef S_IRUSR
# define S_IRUSR 0
# define S_IWUSR 0
# define S_IXUSR 0
#endif
#ifndef S_IRGRP
# define S_IRGRP 0
# define S_IWGRP 0
# define S_IXGRP 0
# define S_IROTH 0
# define S_IWOTH 0
# define S_IXOTH 0
#endif

	if (doc == NULL || doc->file_name == NULL)
	{
		dialogs_show_msgbox(GTK_MESSAGE_ERROR,
		_("An error occurred or file information could not be retrieved (e.g. from a new file)."));
		return;
	}


#ifdef HAVE_SYS_TYPES_H
	locale_filename = utils_get_locale_from_utf8(doc->file_name);
	if (g_stat(locale_filename, &st) == 0)
	{
		/* first copy the returned string and the trim it, to not modify the static glibc string
		 * g_strchomp() is used to remove trailing EOL chars, which are there for whatever reason */
		time_changed  = g_strchomp(g_strdup(ctime(&st.st_ctime)));
		time_modified = g_strchomp(g_strdup(ctime(&st.st_mtime)));
		time_accessed = g_strchomp(g_strdup(ctime(&st.st_atime)));
		filesize = st.st_size;
		mode = st.st_mode;
	}
	else
	{
		time_changed  = g_strdup(_("unknown"));
		time_modified = g_strdup(_("unknown"));
		time_accessed = g_strdup(_("unknown"));
		filesize = (off_t) 0;
		mode = (mode_t) 0;
	}
	g_free(locale_filename);
#else
	time_changed  = g_strdup(_("unknown"));
	time_modified = g_strdup(_("unknown"));
	time_accessed = g_strdup(_("unknown"));
#endif

	base_name = g_path_get_basename(doc->file_name);
	short_name = utils_str_middle_truncate(base_name, 30);
	title = g_strconcat(short_name, " ", _("Properties"), NULL);
	dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_widgets.window),
										 GTK_DIALOG_DESTROY_WITH_PARENT,
										 GTK_STOCK_CLOSE, GTK_RESPONSE_CANCEL, NULL);
	g_free(short_name);
	g_free(title);
	gtk_widget_set_name(dialog, "GeanyDialog");
	vbox = ui_dialog_vbox_new(GTK_DIALOG(dialog));

	g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
	g_signal_connect(dialog, "delete-event", G_CALLBACK(gtk_widget_destroy), NULL);

	gtk_window_set_default_size(GTK_WINDOW(dialog), 300, -1);

	title = g_strdup_printf("<b>%s</b>", base_name);
	label = gtk_label_new(title);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	image = gtk_image_new_from_stock("gtk-file", GTK_ICON_SIZE_BUTTON);
	gtk_misc_set_alignment(GTK_MISC(image), 1.0, 0.5);
	hbox = gtk_hbox_new(FALSE, 6);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_container_add(GTK_CONTAINER(hbox), image);
	gtk_container_add(GTK_CONTAINER(hbox), label);
	gtk_container_add(GTK_CONTAINER(vbox), hbox);
	g_free(title);

	table = gtk_table_new(8, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 10);
	gtk_table_set_col_spacings(GTK_TABLE(table), 10);

	label = gtk_label_new(_("<b>Type:</b>"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	label = gtk_label_new(doc->file_type->title);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_table_attach(GTK_TABLE(table), label, 1, 2, 0, 1,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("<b>Size:</b>"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	file_size = utils_make_human_readable_str(filesize, 1, 0);
	label = gtk_label_new(file_size);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_table_attach(GTK_TABLE(table), label, 1, 2, 1, 2,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	g_free(file_size);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("<b>Location:</b>"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	label = gtk_label_new(doc->file_name);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_table_attach(GTK_TABLE(table), label, 1, 2, 2, 3,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0);

	label = gtk_label_new(_("<b>Read-only:</b>"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 3, 4,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	check = gtk_check_button_new_with_label(_("(only inside Geany)"));
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), doc->readonly);
	gtk_table_attach(GTK_TABLE(table), check, 1, 2, 3, 4,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.0, 0);

	label = gtk_label_new(_("<b>Encoding:</b>"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 4, 5,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	enctext = g_strdup_printf("%s %s",
		doc->encoding,
		(encodings_is_unicode_charset(doc->encoding)) ?
			((doc->has_bom) ? _("(with BOM)") : _("(without BOM)")) : "");

	label = gtk_label_new(enctext);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	g_free(enctext);

	gtk_table_attach(GTK_TABLE(table), label, 1, 2, 4, 5,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0);

	label = gtk_label_new(_("<b>Modified:</b>"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 5, 6,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	label = gtk_label_new(time_modified);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_table_attach(GTK_TABLE(table), label, 1, 2, 5, 6,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("<b>Changed:</b>"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 6, 7,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	label = gtk_label_new(time_changed);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_table_attach(GTK_TABLE(table), label, 1, 2, 6, 7,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("<b>Accessed:</b>"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 7, 8,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 1, 0);

	label = gtk_label_new(time_accessed);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_table_attach(GTK_TABLE(table), label, 1, 2, 7, 8,
					(GtkAttachOptions) (GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	/* add table */
	gtk_container_add(GTK_CONTAINER(vbox), table);

	/* create table with the permissions */
	perm_table = gtk_table_new(5, 4, TRUE);
	gtk_table_set_row_spacings(GTK_TABLE(perm_table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(perm_table), 5);

	label = gtk_label_new(_("<b>Permissions:</b>"));
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_table_attach(GTK_TABLE(perm_table), label, 0, 4, 0, 1,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);

	/* Header */
	label = gtk_label_new(_("Read:"));
	gtk_table_attach(GTK_TABLE(perm_table), label, 1, 2, 1, 2,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0);

	label = gtk_label_new(_("Write:"));
	gtk_table_attach(GTK_TABLE(perm_table), label, 2, 3, 1, 2,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0);

	label = gtk_label_new(_("Execute:"));
	gtk_table_attach(GTK_TABLE(perm_table), label, 3, 4, 1, 2,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0);

	/* Owner */
	label = gtk_label_new(_("Owner:"));
	gtk_table_attach(GTK_TABLE(perm_table), label, 0, 1, 2, 3,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0);

	check = gtk_check_button_new();
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), mode & S_IRUSR);
	gtk_table_attach(GTK_TABLE(perm_table), check, 1, 2, 2, 3,
					(GtkAttachOptions) (GTK_EXPAND | GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.5, 0);

	check = gtk_check_button_new();
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), mode & S_IWUSR);
	gtk_table_attach(GTK_TABLE(perm_table), check, 2, 3, 2, 3,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.5, 0);

	check = gtk_check_button_new();
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), mode & S_IXUSR);
	gtk_table_attach(GTK_TABLE(perm_table), check, 3, 4, 2, 3,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.5, 0);


	/* Group */
	label = gtk_label_new(_("Group:"));
	gtk_table_attach(GTK_TABLE(perm_table), label, 0, 1, 3, 4,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0);

	check = gtk_check_button_new();
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), mode & S_IRGRP);
	gtk_table_attach(GTK_TABLE(perm_table), check, 1, 2, 3, 4,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.5, 0);

	check = gtk_check_button_new();
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), mode & S_IWGRP);
	gtk_table_attach(GTK_TABLE(perm_table), check, 2, 3, 3, 4,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.5, 0);

	check = gtk_check_button_new();
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), mode & S_IXGRP);
	gtk_table_attach(GTK_TABLE(perm_table), check, 3, 4, 3, 4,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.5, 0);


	/* Other */
	label = gtk_label_new(_("Other:"));
	gtk_table_attach(GTK_TABLE(perm_table), label, 0, 1, 4, 5,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0);

	check = gtk_check_button_new();
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), mode & S_IROTH);
	gtk_table_attach(GTK_TABLE(perm_table), check, 1, 2, 4, 5,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.5, 0);

	check = gtk_check_button_new();
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), mode & S_IWOTH);
	gtk_table_attach(GTK_TABLE(perm_table), check, 2, 3, 4, 5,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.5, 0);

	check = gtk_check_button_new();
	gtk_widget_set_sensitive(check, FALSE);
	gtk_button_set_focus_on_click(GTK_BUTTON(check), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), mode & S_IXOTH);
	gtk_table_attach(GTK_TABLE(perm_table), check, 3, 4, 4, 5,
					(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
					(GtkAttachOptions) (0), 0, 0);
	gtk_button_set_alignment(GTK_BUTTON(check), 0.5, 0);

	gtk_container_add(GTK_CONTAINER(vbox), perm_table);

	g_free(base_name);
	g_free(time_changed);
	g_free(time_modified);
	g_free(time_accessed);

	gtk_widget_show_all(dialog);
}


static gboolean show_question(GtkWidget *parent, const gchar *yes_btn, const gchar *no_btn,
							  const gchar *question_text, const gchar *extra_text)
{
	gboolean ret = FALSE;
#ifdef G_OS_WIN32
	gchar *string = (extra_text == NULL) ? g_strdup(question_text) :
		g_strconcat(question_text, "\n\n", extra_text, NULL);

	ret = win32_message_dialog(parent, GTK_MESSAGE_QUESTION, string);
	g_free(string);
#else
	GtkWidget *dialog;

	if (parent == NULL)
		parent = main_widgets.window;

	dialog = gtk_message_dialog_new(GTK_WINDOW(parent),
		GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
		GTK_BUTTONS_NONE, "%s", question_text);
	gtk_widget_set_name(dialog, "GeanyDialog");
	/* question_text will be in bold if optional extra_text used */
	if (extra_text != NULL)
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
			"%s", extra_text);

	/* For a cancel button, use cancel response so user can press escape to cancel */
	gtk_dialog_add_button(GTK_DIALOG(dialog), no_btn,
		utils_str_equal(no_btn, GTK_STOCK_CANCEL) ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_NO);
	gtk_dialog_add_button(GTK_DIALOG(dialog), yes_btn, GTK_RESPONSE_YES);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES)
		ret = TRUE;
	gtk_widget_destroy(dialog);
#endif
	return ret;
}


/**
 *  Show a question message box with @c text and Yes/No buttons.
 *  On Unix-like systems a GTK message dialog box is shown, on Win32 systems a native Windows
 *  message dialog box is shown.
 *
 *  @param text Printf()-style format string.
 *  @param ... Arguments for the @c text format string.
 *
 *  @return @a TRUE if the user answered with Yes, otherwise @a FALSE.
 **/
gboolean dialogs_show_question(const gchar *text, ...)
{
	gboolean ret = FALSE;
	gchar string[512];
	va_list args;

	va_start(args, text);
	g_vsnprintf(string, 511, text, args);
	va_end(args);
	ret = show_question(main_widgets.window, GTK_STOCK_YES, GTK_STOCK_NO, string, NULL);
	return ret;
}


/* extra_text can be NULL; otherwise it is displayed below main_text.
 * if parent is NULL, main_widgets.window will be used */
gboolean dialogs_show_question_full(GtkWidget *parent, const gchar *yes_btn, const gchar *no_btn,
	const gchar *extra_text, const gchar *main_text, ...)
{
	gboolean ret = FALSE;
	gchar string[512];
	va_list args;

	va_start(args, main_text);
	g_vsnprintf(string, 511, main_text, args);
	va_end(args);
	ret = show_question(parent, yes_btn, no_btn, string, extra_text);
	return ret;
}



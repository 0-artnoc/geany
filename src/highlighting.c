/*
 *      highlighting.c - this file is part of Geany, a fast and lightweight IDE
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

/**
 * @file highlighting.h
 * Syntax highlighting for the different filetypes, using the Scintilla lexers.
 */

#include "geany.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "SciLexer.h"
#include "highlighting.h"
#include "editor.h"
#include "utils.h"
#include "filetypes.h"
#include "symbols.h"
#include "ui_utils.h"
#include "utils.h"
#include "main.h"
#include "support.h"


#define GEANY_COLORSCHEMES_SUBDIR "colorschemes"

/* Whitespace has to be set after setting wordchars. */
#define GEANY_WHITESPACE_CHARS " \t" "!\"#$%&'()*+,-./:;<=>?@[\\]^`{|}~"


static gchar *whitespace_chars;


static void styleset_markup(ScintillaObject *sci, gboolean set_keywords);


typedef struct
{
	gsize			count;		/* number of styles */
	GeanyLexerStyle	*styling;		/* array of styles, NULL if not used or uninitialised */
	gchar			**keywords;
	gchar			*wordchars;	/* NULL used for style sets with no styles */
	gchar			**property_keys;
	gchar			**property_values;
} StyleSet;

/* each filetype has a styleset except GEANY_FILETYPES_NONE, which uses common_style_set */
static StyleSet *style_sets = NULL;


enum	/* Geany common styling */
{
	GCS_DEFAULT,
	GCS_SELECTION,
	GCS_BRACE_GOOD,
	GCS_BRACE_BAD,
	GCS_MARGIN_LINENUMBER,
	GCS_MARGIN_FOLDING,
	GCS_CURRENT_LINE,
	GCS_CARET,
	GCS_INDENT_GUIDE,
	GCS_WHITE_SPACE,
	GCS_LINE_WRAP_VISUALS,
	GCS_LINE_WRAP_INDENT,
	GCS_TRANSLUCENCY,
	GCS_MARKER_LINE,
	GCS_MARKER_SEARCH,
	GCS_MARKER_MARK,
	GCS_MARKER_TRANSLUCENCY,
	GCS_LINE_HEIGHT,
	GCS_MAX
};

static struct
{
	GeanyLexerStyle	 styling[GCS_MAX];

	/* can take values 1 or 2 (or 3) */
	gint fold_marker;
	gint fold_lines;
	gint fold_draw_line;

	gchar			*wordchars;
} common_style_set;


/* For filetypes.common [named_styles] section.
 * 0xBBGGRR format.
 * e.g. "comment" => &GeanyLexerStyle{0x0000d0, 0xffffff, FALSE, FALSE} */
static GHashTable *named_style_hash = NULL;

/* 0xBBGGRR format, set by "default" named style. */
static GeanyLexerStyle gsd_default = {0x000000, 0xffffff, FALSE, FALSE};


/* Note: use sciwrappers.h instead where possible.
 * Do not use SSM in files unrelated to scintilla. */
#define SSM(s, m, w, l) scintilla_send_message(s, m, w, l)

/* filetypes should use the filetypes.foo [lexer_properties] group instead of hardcoding */
static void sci_set_property(ScintillaObject *sci, const gchar *name, const gchar *value)
{
	SSM(sci, SCI_SETPROPERTY, (uptr_t) name, (sptr_t) value);
}


static void sci_set_keywords(ScintillaObject *sci, gint idx, const gchar *words)
{
	SSM(sci, SCI_SETKEYWORDS, idx, (sptr_t) words);
}


static void new_styleset(gint file_type_id, gint styling_count)
{
	StyleSet *set = &style_sets[file_type_id];

	set->count = styling_count;
	set->styling = g_new0(GeanyLexerStyle, styling_count);
}


static void free_styleset(gint file_type_id)
{
	StyleSet *style_ptr;
	style_ptr = &style_sets[file_type_id];

	style_ptr->count = 0;
	g_free(style_ptr->styling);
	style_ptr->styling = NULL;
	g_strfreev(style_ptr->keywords);
	style_ptr->keywords = NULL;
	g_free(style_ptr->wordchars);
	style_ptr->wordchars = NULL;
	g_strfreev(style_ptr->property_keys);
	style_ptr->property_keys = NULL;
	g_strfreev(style_ptr->property_values);
	style_ptr->property_values = NULL;
}


static void get_keyfile_keywords(GKeyFile *config, GKeyFile *configh,
				const gchar *key, gint ft_id, gint pos)
{
	const gchar section[] = "keywords";
	gchar *result;
	const gchar *default_value = "";

	if (config == NULL || configh == NULL)
	{
		style_sets[ft_id].keywords[pos] = g_strdup(default_value);
		return;
	}

	result = g_key_file_get_string(configh, section, key, NULL);
	if (result == NULL)
		result = g_key_file_get_string(config, section, key, NULL);

	if (result == NULL)
	{
		style_sets[ft_id].keywords[pos] = g_strdup(default_value);
	}
	else
	{
		style_sets[ft_id].keywords[pos] = result;
	}
}


static void get_keyfile_wordchars(GKeyFile *config, GKeyFile *configh, gchar **wordchars)
{
	gchar *result;

	if (config == NULL || configh == NULL)
	{
		*wordchars = g_strdup(GEANY_WORDCHARS);
		return;
	}

	result = g_key_file_get_string(configh, "settings", "wordchars", NULL);
	if (result == NULL) result = g_key_file_get_string(config, "settings", "wordchars", NULL);

	if (result == NULL)
	{
		*wordchars = g_strdup(GEANY_WORDCHARS);
	}
	else
		*wordchars = result;
}


static void read_named_style(const gchar *named_style, GeanyLexerStyle *style)
{
	GeanyLexerStyle *cs;
	gchar *comma, *name = NULL;
	const gchar *bold = NULL;
	const gchar *italic = NULL;

	g_return_if_fail(named_style);
	name = utils_strdupa(named_style);	/* named_style must not be written to, may be a static string */

	comma = strstr(name, ",");
	if (comma)
	{
		bold = strstr(comma, ",bold");
		italic = strstr(comma, ",italic");
		*comma = '\0';	/* terminate name to make lookup work */
	}
	cs = g_hash_table_lookup(named_style_hash, name);

	if (cs)
	{
 		*style = *cs;
 		if (bold)
 			style->bold = !style->bold;
 		if (italic)
 			style->italic = !style->italic;
	}
	else
	{
		*style = gsd_default;
		geany_debug("No named style '%s'! Check filetypes.common.", name);
	}
}


static void parse_color(const gchar *str, gint *clr)
{
	gint c;

	/* ignore empty strings */
	if (!NZV(str))
		return;

	c = utils_strtod(str, NULL, FALSE);
	if (c > -1)
	{
		*clr = c;
		return;
	}
	geany_debug("Bad color '%s'", str);
}


static void parse_keyfile_style(gchar **list,
		const GeanyLexerStyle *default_style, GeanyLexerStyle *style)
{
	gsize len;
	gchar *str;

	g_return_if_fail(default_style);
	g_return_if_fail(style);

	*style = *default_style;

	if (!list)
		return;

	len = g_strv_length(list);

	str = list[0];
	if (len == 1 && isalpha(str[0]))
		read_named_style(str, style);
	else
	{
		switch (len)
		{
			case 4:
				style->italic = utils_atob(list[3]);
			case 3:
				style->bold = utils_atob(list[2]);
			case 2:
				parse_color(list[1], &style->background);
			case 1:
				parse_color(list[0], &style->foreground);
		}
	}
}


static void get_keyfile_style(GKeyFile *config, GKeyFile *configh,
		const gchar *key_name, GeanyLexerStyle *style)
{
	gchar **list;
	gsize len;

	g_return_if_fail(config);
	g_return_if_fail(configh);
	g_return_if_fail(key_name);
	g_return_if_fail(style);

	list = g_key_file_get_string_list(configh, "styling", key_name, &len, NULL);
	if (list == NULL)
		list = g_key_file_get_string_list(config, "styling", key_name, &len, NULL);

	parse_keyfile_style(list, &gsd_default, style);
	g_strfreev(list);
}


/* Convert 0xRRGGBB to 0xBBGGRR, which scintilla expects. */
static gint rotate_rgb(gint color)
{
	return ((color & 0xFF0000) >> 16) +
		(color & 0x00FF00) +
		((color & 0x0000FF) << 16);
}


static void convert_int(const gchar *int_str, gint *val)
{
	gchar *end;
	gint v = strtol(int_str, &end, 10);

	if (int_str != end)
		*val = v;
}


/* Get first and second integer numbers, store in foreground and background fields of @a style. */
static void get_keyfile_int(GKeyFile *config, GKeyFile *configh, const gchar *section,
							const gchar *key, gint fdefault_val, gint sdefault_val,
							GeanyLexerStyle *style)
{
	gchar **list;
	gsize len;
	GeanyLexerStyle def = {fdefault_val, sdefault_val, FALSE, FALSE};

	g_return_if_fail(config);
	g_return_if_fail(configh);
	g_return_if_fail(section);
	g_return_if_fail(key);

	list = g_key_file_get_string_list(configh, section, key, &len, NULL);
	if (list == NULL)
		list = g_key_file_get_string_list(config, section, key, &len, NULL);

	*style = def;
	if (!list)
		return;

	if (list[0])
	{
		convert_int(list[0], &style->foreground);
		if (list[1])
		{
			convert_int(list[1], &style->background);
		}
	}
	g_strfreev(list);
}


/* first or second can be NULL. */
static void get_keyfile_ints(GKeyFile *config, GKeyFile *configh, const gchar *section,
							const gchar *key,
							gint fdefault_val, gint sdefault_val,
							gint *first, gint *second)
{
	GeanyLexerStyle tmp_style;

	get_keyfile_int(config, configh, section, key, fdefault_val, sdefault_val, &tmp_style);
	if (first)
		*first = tmp_style.foreground;
	if (second)
		*second = tmp_style.background;
}


static guint invert(guint icolour)
{
	if (interface_prefs.highlighting_invert_all)
		return utils_invert_color(icolour);

	return icolour;
}


static GeanyLexerStyle *get_style(guint ft_id, guint styling_index)
{
	g_assert(ft_id < filetypes_array->len);

	if (G_UNLIKELY(ft_id == GEANY_FILETYPES_NONE))
	{
		g_assert(styling_index < GCS_MAX);
		return &common_style_set.styling[styling_index];
	}
	else
	{
		StyleSet *set = &style_sets[ft_id];

		g_assert(styling_index < set->count);
		return &set->styling[styling_index];
	}
}


static void set_sci_style(ScintillaObject *sci, gint style, guint ft_id, guint styling_index)
{
	GeanyLexerStyle *style_ptr = get_style(ft_id, styling_index);

	SSM(sci, SCI_STYLESETFORE, style,	invert(style_ptr->foreground));
	SSM(sci, SCI_STYLESETBACK, style,	invert(style_ptr->background));
	SSM(sci, SCI_STYLESETBOLD, style,	style_ptr->bold);
	SSM(sci, SCI_STYLESETITALIC, style,	style_ptr->italic);
}


void highlighting_free_styles()
{
	guint i;

	for (i = 0; i < filetypes_array->len; i++)
		free_styleset(i);

	if (named_style_hash)
		g_hash_table_destroy(named_style_hash);

	if (style_sets)
		g_free(style_sets);
}


static GString *get_global_typenames(gint lang)
{
	GString *s = NULL;

	if (app->tm_workspace)
	{
		GPtrArray *tags_array = app->tm_workspace->global_tags;

		if (tags_array)
		{
			s = symbols_find_tags_as_string(tags_array, TM_GLOBAL_TYPE_MASK, lang);
		}
	}
	return s;
}


static gchar*
get_keyfile_whitespace_chars(GKeyFile *config, GKeyFile *configh)
{
	gchar *result;

	if (config == NULL || configh == NULL)
	{
		result = NULL;
	}
	else
	{
		result = g_key_file_get_string(configh, "settings", "whitespace_chars", NULL);
		if (result == NULL)
			result = g_key_file_get_string(config, "settings", "whitespace_chars", NULL);
	}
	if (result == NULL)
		result = g_strdup(GEANY_WHITESPACE_CHARS);
	return result;
}


static void add_named_style(GKeyFile *config, const gchar *key)
{
	const gchar group[] = "named_styles";
	gchar **list;
	gsize len;

	list = g_key_file_get_string_list(config, group, key, &len, NULL);
	/* we allow a named style to reference another style above it */
	if (list && len >= 1)
	{
		GeanyLexerStyle *style = g_new0(GeanyLexerStyle, 1);

		parse_keyfile_style(list, &gsd_default, style);
		g_hash_table_insert(named_style_hash, g_strdup(key), style);
	}
	g_strfreev(list);
}


static void get_named_styles(GKeyFile *config)
{
	const gchar group[] = "named_styles";
	gchar **keys = g_key_file_get_keys(config, group, NULL, NULL);
	gchar **ptr = keys;

	if (!ptr)
		return;

	while (1)
	{
		const gchar *key = *ptr;

		if (!key)
			break;

		/* don't replace already read default style with system one */
		if (!g_str_equal(key, "default"))
			add_named_style(config, key);

		ptr++;
	}
	g_strfreev(keys);
}


static GKeyFile *utils_key_file_new(const gchar *filename)
{
	GKeyFile *config = g_key_file_new();

	g_key_file_load_from_file(config, filename, G_KEY_FILE_KEEP_COMMENTS, NULL);
	return config;
}


static void load_named_styles(GKeyFile *config, GKeyFile *config_home)
{
	const gchar *scheme = editor_prefs.color_scheme;
	gboolean free_kf = FALSE;

	if (named_style_hash)
		g_hash_table_destroy(named_style_hash);	/* reloading */

	named_style_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	if (NZV(scheme))
	{
		gchar *path, *path_home;

		path = g_build_path(G_DIR_SEPARATOR_S, app->datadir, GEANY_COLORSCHEMES_SUBDIR, scheme, NULL);
		path_home = g_build_path(G_DIR_SEPARATOR_S, app->configdir, GEANY_COLORSCHEMES_SUBDIR, scheme, NULL);

		if (g_file_test(path, G_FILE_TEST_EXISTS) || g_file_test(path_home, G_FILE_TEST_EXISTS))
		{
			config = utils_key_file_new(path);
			config_home = utils_key_file_new(path_home);
			free_kf = TRUE;
		}
		/* if color scheme is missing, use default */
		g_free(path);
		g_free(path_home);
	}
	/* first set default to the "default" named style */
	add_named_style(config, "default");
	read_named_style("default", &gsd_default);	/* in case user overrides but not with both colors */
	add_named_style(config_home, "default");
	read_named_style("default", &gsd_default);

	get_named_styles(config);
	/* home overrides any system named style */
	get_named_styles(config_home);

	if (free_kf)
	{
		g_key_file_free(config);
		g_key_file_free(config_home);
	}
}


static void styleset_common_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	load_named_styles(config, config_home);

	get_keyfile_style(config, config_home, "default", &common_style_set.styling[GCS_DEFAULT]);
	get_keyfile_style(config, config_home, "selection", &common_style_set.styling[GCS_SELECTION]);
	get_keyfile_style(config, config_home, "brace_good", &common_style_set.styling[GCS_BRACE_GOOD]);
	get_keyfile_style(config, config_home, "brace_bad", &common_style_set.styling[GCS_BRACE_BAD]);
	get_keyfile_style(config, config_home, "margin_linenumber", &common_style_set.styling[GCS_MARGIN_LINENUMBER]);
	get_keyfile_style(config, config_home, "margin_folding", &common_style_set.styling[GCS_MARGIN_FOLDING]);
	get_keyfile_style(config, config_home, "current_line", &common_style_set.styling[GCS_CURRENT_LINE]);
	get_keyfile_style(config, config_home, "caret", &common_style_set.styling[GCS_CARET]);
	get_keyfile_style(config, config_home, "indent_guide", &common_style_set.styling[GCS_INDENT_GUIDE]);
	get_keyfile_style(config, config_home, "white_space", &common_style_set.styling[GCS_WHITE_SPACE]);
	get_keyfile_style(config, config_home, "marker_line", &common_style_set.styling[GCS_MARKER_LINE]);
	get_keyfile_style(config, config_home, "marker_search", &common_style_set.styling[GCS_MARKER_SEARCH]);
	get_keyfile_style(config, config_home, "marker_mark", &common_style_set.styling[GCS_MARKER_MARK]);

	get_keyfile_ints(config, config_home, "styling", "folding_style",
		1, 1, &common_style_set.fold_marker, &common_style_set.fold_lines);
	get_keyfile_ints(config, config_home, "styling", "folding_horiz_line",
		2, 0, &common_style_set.fold_draw_line, NULL);
	get_keyfile_ints(config, config_home, "styling", "caret_width",
		1, 0, &common_style_set.styling[GCS_CARET].background, NULL); /* caret.foreground used earlier */
	get_keyfile_int(config, config_home, "styling", "line_wrap_visuals",
		3, 0, &common_style_set.styling[GCS_LINE_WRAP_VISUALS]);
	get_keyfile_int(config, config_home, "styling", "line_wrap_indent",
		0, 0, &common_style_set.styling[GCS_LINE_WRAP_INDENT]);
	get_keyfile_int(config, config_home, "styling", "translucency",
		256, 256, &common_style_set.styling[GCS_TRANSLUCENCY]);
	get_keyfile_int(config, config_home, "styling", "marker_translucency",
		256, 256, &common_style_set.styling[GCS_MARKER_TRANSLUCENCY]);
	get_keyfile_int(config, config_home, "styling", "line_height",
		0, 0, &common_style_set.styling[GCS_LINE_HEIGHT]);

	get_keyfile_wordchars(config, config_home, &common_style_set.wordchars);
	whitespace_chars = get_keyfile_whitespace_chars(config, config_home);
}


static void styleset_common(ScintillaObject *sci, filetype_id ft_id)
{
	SSM(sci, SCI_STYLECLEARALL, 0, 0);

	SSM(sci, SCI_SETWORDCHARS, 0, (sptr_t) (ft_id == GEANY_FILETYPES_NONE ?
		common_style_set.wordchars : style_sets[ft_id].wordchars));
	/* have to set whitespace after setting wordchars */
	SSM(sci, SCI_SETWHITESPACECHARS, 0, (sptr_t) whitespace_chars);

	/* caret colour, style and width */
	SSM(sci, SCI_SETCARETFORE, invert(common_style_set.styling[GCS_CARET].foreground), 0);
	SSM(sci, SCI_SETCARETWIDTH, common_style_set.styling[GCS_CARET].background, 0);
	if (common_style_set.styling[GCS_CARET].bold)
		SSM(sci, SCI_SETCARETSTYLE, CARETSTYLE_BLOCK, 0);
	else
		SSM(sci, SCI_SETCARETSTYLE, CARETSTYLE_LINE, 0);

	/* line height */
	SSM(sci, SCI_SETEXTRAASCENT, common_style_set.styling[GCS_LINE_HEIGHT].foreground, 0);
	SSM(sci, SCI_SETEXTRADESCENT, common_style_set.styling[GCS_LINE_HEIGHT].background, 0);

	/* colourise the current line */
	SSM(sci, SCI_SETCARETLINEBACK, invert(common_style_set.styling[GCS_CURRENT_LINE].background), 0);
	/* bold=enable current line */
	SSM(sci, SCI_SETCARETLINEVISIBLE, common_style_set.styling[GCS_CURRENT_LINE].bold, 0);

	/* Translucency for current line and selection */
	SSM(sci, SCI_SETCARETLINEBACKALPHA, common_style_set.styling[GCS_TRANSLUCENCY].foreground, 0);
	SSM(sci, SCI_SETSELALPHA, common_style_set.styling[GCS_TRANSLUCENCY].background, 0);

	/* line wrapping visuals */
	SSM(sci, SCI_SETWRAPVISUALFLAGS,
		common_style_set.styling[GCS_LINE_WRAP_VISUALS].foreground, 0);
	SSM(sci, SCI_SETWRAPVISUALFLAGSLOCATION,
		common_style_set.styling[GCS_LINE_WRAP_VISUALS].background, 0);
	SSM(sci, SCI_SETWRAPSTARTINDENT, common_style_set.styling[GCS_LINE_WRAP_INDENT].foreground, 0);
	SSM(sci, SCI_SETWRAPINDENTMODE, common_style_set.styling[GCS_LINE_WRAP_INDENT].background, 0);

	/* Error indicator */
	SSM(sci, SCI_INDICSETSTYLE, GEANY_INDICATOR_ERROR, INDIC_SQUIGGLE);
	SSM(sci, SCI_INDICSETFORE, GEANY_INDICATOR_ERROR, invert(rotate_rgb(0xff0000)));

	/* Search indicator, used for 'Mark' matches */
	SSM(sci, SCI_INDICSETSTYLE, GEANY_INDICATOR_SEARCH, INDIC_ROUNDBOX);
	SSM(sci, SCI_INDICSETFORE, GEANY_INDICATOR_SEARCH,
		invert(common_style_set.styling[GCS_MARKER_SEARCH].background));
	SSM(sci, SCI_INDICSETALPHA, GEANY_INDICATOR_SEARCH, 60);

	/* define marker symbols
	 * 0 -> line marker */
	SSM(sci, SCI_MARKERDEFINE, 0, SC_MARK_SHORTARROW);
	SSM(sci, SCI_MARKERSETFORE, 0, invert(common_style_set.styling[GCS_MARKER_LINE].foreground));
	SSM(sci, SCI_MARKERSETBACK, 0, invert(common_style_set.styling[GCS_MARKER_LINE].background));
	SSM(sci, SCI_MARKERSETALPHA, 0, common_style_set.styling[GCS_MARKER_TRANSLUCENCY].foreground);

	/* 1 -> user marker */
	SSM(sci, SCI_MARKERDEFINE, 1, SC_MARK_PLUS);
	SSM(sci, SCI_MARKERSETFORE, 1, invert(common_style_set.styling[GCS_MARKER_MARK].foreground));
	SSM(sci, SCI_MARKERSETBACK, 1, invert(common_style_set.styling[GCS_MARKER_MARK].background));
	SSM(sci, SCI_MARKERSETALPHA, 1, common_style_set.styling[GCS_MARKER_TRANSLUCENCY].background);

	/* 2 -> folding marker, other folding settings */
	SSM(sci, SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
	SSM(sci, SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);

	/* drawing a horizontal line when text if folded */
	switch (common_style_set.fold_draw_line)
	{
		case 1:
		{
			SSM(sci, SCI_SETFOLDFLAGS, 4, 0);
			break;
		}
		case 2:
		{
			SSM(sci, SCI_SETFOLDFLAGS, 16, 0);
			break;
		}
		default:
		{
			SSM(sci, SCI_SETFOLDFLAGS, 0, 0);
			break;
		}
	}

	/* choose the folding style - boxes or circles, I prefer boxes, so it is default ;-) */
	switch (common_style_set.fold_marker)
	{
		case 2:
		{
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEROPEN, SC_MARK_CIRCLEMINUS);
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDER, SC_MARK_CIRCLEPLUS);
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEREND, SC_MARK_CIRCLEPLUSCONNECTED);
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEROPENMID, SC_MARK_CIRCLEMINUSCONNECTED);
			break;
		}
		default:
		{
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
			break;
		}
	}

	/* choose the folding style - straight or curved, I prefer straight, so it is default ;-) */
	switch (common_style_set.fold_lines)
	{
		case 2:
		{
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNERCURVE);
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNERCURVE);
			break;
		}
		default:
		{
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);
			SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
			break;
		}
	}

	SSM(sci, SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);

	SSM(sci, SCI_MARKERSETFORE, SC_MARKNUM_FOLDEROPEN, 0xffffff);
	SSM(sci, SCI_MARKERSETBACK, SC_MARKNUM_FOLDEROPEN, 0x000000);
	SSM(sci, SCI_MARKERSETFORE, SC_MARKNUM_FOLDER, 0xffffff);
	SSM(sci, SCI_MARKERSETBACK, SC_MARKNUM_FOLDER, 0x000000);
	SSM(sci, SCI_MARKERSETFORE, SC_MARKNUM_FOLDERSUB, 0xffffff);
	SSM(sci, SCI_MARKERSETBACK, SC_MARKNUM_FOLDERSUB, 0x000000);
	SSM(sci, SCI_MARKERSETFORE, SC_MARKNUM_FOLDERTAIL, 0xffffff);
	SSM(sci, SCI_MARKERSETBACK, SC_MARKNUM_FOLDERTAIL, 0x000000);
	SSM(sci, SCI_MARKERSETFORE, SC_MARKNUM_FOLDEREND, 0xffffff);
	SSM(sci, SCI_MARKERSETBACK, SC_MARKNUM_FOLDEREND, 0x000000);
	SSM(sci, SCI_MARKERSETFORE, SC_MARKNUM_FOLDEROPENMID, 0xffffff);
	SSM(sci, SCI_MARKERSETBACK, SC_MARKNUM_FOLDEROPENMID, 0x000000);
	SSM(sci, SCI_MARKERSETFORE, SC_MARKNUM_FOLDERMIDTAIL, 0xffffff);
	SSM(sci, SCI_MARKERSETBACK, SC_MARKNUM_FOLDERMIDTAIL, 0x000000);

	sci_set_property(sci, "fold", "1");
	sci_set_property(sci, "fold.compact", "0");
	sci_set_property(sci, "fold.comment", "1");
	sci_set_property(sci, "fold.preprocessor", "1");
	sci_set_property(sci, "fold.at.else", "1");


	/* bold (3rd argument) is whether to override default foreground selection */
	if (common_style_set.styling[GCS_SELECTION].bold)
		SSM(sci, SCI_SETSELFORE, 1, invert(common_style_set.styling[GCS_SELECTION].foreground));
	/* italic (4th argument) is whether to override default background selection */
	if (common_style_set.styling[GCS_SELECTION].italic)
		SSM(sci, SCI_SETSELBACK, 1, invert(common_style_set.styling[GCS_SELECTION].background));

	SSM(sci, SCI_SETSTYLEBITS, SSM(sci, SCI_GETSTYLEBITSNEEDED, 0, 0), 0);

	SSM(sci, SCI_SETFOLDMARGINCOLOUR, 1, invert(common_style_set.styling[GCS_MARGIN_FOLDING].background));
	/*SSM(sci, SCI_SETFOLDMARGINHICOLOUR, 1, invert(common_style_set.styling[GCS_MARGIN_FOLDING].background));*/
	set_sci_style(sci, STYLE_LINENUMBER, GEANY_FILETYPES_NONE, GCS_MARGIN_LINENUMBER);
	set_sci_style(sci, STYLE_BRACELIGHT, GEANY_FILETYPES_NONE, GCS_BRACE_GOOD);
	set_sci_style(sci, STYLE_BRACEBAD, GEANY_FILETYPES_NONE, GCS_BRACE_BAD);
	set_sci_style(sci, STYLE_INDENTGUIDE, GEANY_FILETYPES_NONE, GCS_INDENT_GUIDE);

	/* bold = common whitespace settings enabled */
	SSM(sci, SCI_SETWHITESPACEFORE, common_style_set.styling[GCS_WHITE_SPACE].bold,
		invert(common_style_set.styling[GCS_WHITE_SPACE].foreground));
	SSM(sci, SCI_SETWHITESPACEBACK, common_style_set.styling[GCS_WHITE_SPACE].italic,
		invert(common_style_set.styling[GCS_WHITE_SPACE].background));
}


/* Assign global typedefs and user secondary keywords */
static void assign_global_and_user_keywords(ScintillaObject *sci,
											const gchar *user_words, gint lang)
{
	GString *s;

	s = get_global_typenames(lang);
	if (G_UNLIKELY(s == NULL))
		s = g_string_sized_new(200);
	else
		g_string_append_c(s, ' '); /* append a space as delimiter to the existing list of words */

	g_string_append(s, user_words);

	sci_set_keywords(sci, 1, s->str);
	g_string_free(s, TRUE);
}


/* All stylesets except None should call this. */
static void
apply_filetype_properties(ScintillaObject *sci, gint lexer, filetype_id ft_id)
{
	g_assert(ft_id != GEANY_FILETYPES_NONE);

	SSM(sci, SCI_SETLEXER, lexer, 0);

	styleset_common(sci, ft_id);
}


#define foreach_range(i, size) \
		for (i = 0; i < size; i++)

/* names: the style names for the filetype. */
static void load_style_entries(GKeyFile *config, GKeyFile *config_home, gint filetype_idx,
		const gchar **names, gsize names_len)
{
	guint i;

	foreach_range(i, names_len)
	{
		const gchar *name = names[i];
		GeanyLexerStyle *style = &style_sets[filetype_idx].styling[i];

		get_keyfile_style(config, config_home, name, style);
	}
}


/* styles: the style IDs for the filetype.
 * STYLE_DEFAULT will be set to match the first style. */
static void apply_style_entries(ScintillaObject *sci, gint filetype_idx,
		gint *styles, gsize styles_len)
{
	guint i;

	g_return_if_fail(styles_len > 0);

	set_sci_style(sci, STYLE_DEFAULT, filetype_idx, 0);

	foreach_range(i, styles_len)
		set_sci_style(sci, styles[i], filetype_idx, i);
}


/* call new_styleset(filetype_idx, >= 20) before using this. */
static void
styleset_c_like_init(GKeyFile *config, GKeyFile *config_home, gint filetype_idx)
{
	const gchar *entries[] =
 	{
		"default",
		"comment",
		"commentline",
		"commentdoc",
		"number",
		"word",
		"word2",
		"string",
		"character",
		"uuid",
		"preprocessor",
		"operator",
		"identifier",
		"stringeol",
		"verbatim",
		"regex",
		"commentlinedoc",
		"commentdockeyword",
		"commentdockeyworderror",
		"globalclass"
	};

	new_styleset(filetype_idx, G_N_ELEMENTS(entries));
	load_style_entries(config, config_home, filetype_idx, entries, G_N_ELEMENTS(entries));
}


static void styleset_c_like(ScintillaObject *sci, gint ft_id)
{
	gint styles[] = {
		SCE_C_DEFAULT,
		SCE_C_COMMENT,
		SCE_C_COMMENTLINE,
		SCE_C_COMMENTDOC,
		SCE_C_NUMBER,
		SCE_C_WORD,
		SCE_C_WORD2,
		SCE_C_STRING,
		SCE_C_CHARACTER,
		SCE_C_UUID,
		SCE_C_PREPROCESSOR,
		SCE_C_OPERATOR,
		SCE_C_IDENTIFIER,
		SCE_C_STRINGEOL,
		SCE_C_VERBATIM,
		SCE_C_REGEX,
		SCE_C_COMMENTLINEDOC,
		SCE_C_COMMENTDOCKEYWORD,
		SCE_C_COMMENTDOCKEYWORDERROR,
		/* used for local structs and typedefs */
		SCE_C_GLOBALCLASS
	};

	apply_filetype_properties(sci, SCLEX_CPP, ft_id);

	apply_style_entries(sci, ft_id, styles, G_N_ELEMENTS(styles));
}


static void styleset_c_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	styleset_c_like_init(config, config_home, ft_id);

	style_sets[ft_id].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "secondary", ft_id, 1);
	get_keyfile_keywords(config, config_home, "docComment", ft_id, 2);
	style_sets[ft_id].keywords[3] = NULL;
}


static void styleset_c(ScintillaObject *sci, gint ft_id)
{
	styleset_c_like(sci, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	/* for SCI_SETKEYWORDS = 1, see below*/
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);

	/* assign global types, merge them with user defined keywords and set them */
	assign_global_and_user_keywords(sci, style_sets[ft_id].keywords[1],
		filetypes[ft_id]->lang);
}


static void styleset_pascal_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 15);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "comment2", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "preprocessor", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "preprocessor2", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "hexnumber", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "asm", &style_sets[ft_id].styling[14]);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_pascal(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_PASCAL, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_PAS_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_PAS_IDENTIFIER, ft_id, 1);
	set_sci_style(sci, SCE_PAS_COMMENT, ft_id, 2);
	set_sci_style(sci, SCE_PAS_COMMENT2, ft_id, 3);
	set_sci_style(sci, SCE_PAS_COMMENTLINE, ft_id, 4);
	set_sci_style(sci, SCE_PAS_PREPROCESSOR, ft_id, 5);
	set_sci_style(sci, SCE_PAS_PREPROCESSOR2, ft_id, 6);
	set_sci_style(sci, SCE_PAS_NUMBER, ft_id, 7);
	set_sci_style(sci, SCE_PAS_HEXNUMBER, ft_id, 8);
	set_sci_style(sci, SCE_PAS_WORD, ft_id, 9);
	set_sci_style(sci, SCE_PAS_STRING, ft_id, 10);
	set_sci_style(sci, SCE_PAS_STRINGEOL, ft_id, 11);
	set_sci_style(sci, SCE_PAS_CHARACTER, ft_id, 12);
	set_sci_style(sci, SCE_PAS_OPERATOR, ft_id, 13);
	set_sci_style(sci, SCE_PAS_ASM, ft_id, 14);
}


static void styleset_makefile_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 7);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "preprocessor", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "target", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "ideol", &style_sets[ft_id].styling[6]);

	style_sets[ft_id].keywords = NULL;
}


static void styleset_makefile(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_MAKEFILE, ft_id);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_MAKE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_MAKE_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_MAKE_PREPROCESSOR, ft_id, 2);
	set_sci_style(sci, SCE_MAKE_IDENTIFIER, ft_id, 3);
	set_sci_style(sci, SCE_MAKE_OPERATOR, ft_id, 4);
	set_sci_style(sci, SCE_MAKE_TARGET, ft_id, 5);
	set_sci_style(sci, SCE_MAKE_IDEOL, ft_id, 6);
}


static void styleset_diff_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 8);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "command", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "header", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "position", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "deleted", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "added", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "changed", &style_sets[ft_id].styling[7]);

	style_sets[ft_id].keywords = NULL;
}


static void styleset_diff(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_DIFF, ft_id);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_DIFF_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_DIFF_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_DIFF_COMMAND, ft_id, 2);
	set_sci_style(sci, SCE_DIFF_HEADER, ft_id, 3);
	set_sci_style(sci, SCE_DIFF_POSITION, ft_id, 4);
	set_sci_style(sci, SCE_DIFF_DELETED, ft_id, 5);
	set_sci_style(sci, SCE_DIFF_ADDED, ft_id, 6);
	set_sci_style(sci, SCE_DIFF_CHANGED, ft_id, 7);
}


static void styleset_latex_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 5);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "command", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "tag", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "math", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[4]);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_latex(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_LATEX, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_L_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_L_COMMAND, ft_id, 1);
	set_sci_style(sci, SCE_L_TAG, ft_id, 2);
	set_sci_style(sci, SCE_L_MATH, ft_id, 3);
	set_sci_style(sci, SCE_L_COMMENT, ft_id, 4);
}


static void styleset_php_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	style_sets[ft_id].styling = NULL;
	style_sets[ft_id].keywords = NULL;
}


static void styleset_php(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_HTML, ft_id);

	/* use the same colouring as for XML */
	styleset_markup(sci, TRUE);
}


static void styleset_html_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	style_sets[ft_id].styling = NULL;
	style_sets[ft_id].keywords = NULL;
}


static void styleset_html(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_HTML, ft_id);

	/* use the same colouring for HTML; XML and so on */
	styleset_markup(sci, TRUE);
}


static void styleset_markup_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(GEANY_FILETYPES_XML, 56);
	get_keyfile_style(config, config_home, "html_default", &style_sets[GEANY_FILETYPES_XML].styling[0]);
	get_keyfile_style(config, config_home, "html_tag", &style_sets[GEANY_FILETYPES_XML].styling[1]);
	get_keyfile_style(config, config_home, "html_tagunknown", &style_sets[GEANY_FILETYPES_XML].styling[2]);
	get_keyfile_style(config, config_home, "html_attribute", &style_sets[GEANY_FILETYPES_XML].styling[3]);
	get_keyfile_style(config, config_home, "html_attributeunknown", &style_sets[GEANY_FILETYPES_XML].styling[4]);
	get_keyfile_style(config, config_home, "html_number", &style_sets[GEANY_FILETYPES_XML].styling[5]);
	get_keyfile_style(config, config_home, "html_doublestring", &style_sets[GEANY_FILETYPES_XML].styling[6]);
	get_keyfile_style(config, config_home, "html_singlestring", &style_sets[GEANY_FILETYPES_XML].styling[7]);
	get_keyfile_style(config, config_home, "html_other", &style_sets[GEANY_FILETYPES_XML].styling[8]);
	get_keyfile_style(config, config_home, "html_comment", &style_sets[GEANY_FILETYPES_XML].styling[9]);
	get_keyfile_style(config, config_home, "html_entity", &style_sets[GEANY_FILETYPES_XML].styling[10]);
	get_keyfile_style(config, config_home, "html_tagend", &style_sets[GEANY_FILETYPES_XML].styling[11]);
	get_keyfile_style(config, config_home, "html_xmlstart", &style_sets[GEANY_FILETYPES_XML].styling[12]);
	get_keyfile_style(config, config_home, "html_xmlend", &style_sets[GEANY_FILETYPES_XML].styling[13]);
	get_keyfile_style(config, config_home, "html_script", &style_sets[GEANY_FILETYPES_XML].styling[14]);
	get_keyfile_style(config, config_home, "html_asp", &style_sets[GEANY_FILETYPES_XML].styling[15]);
	get_keyfile_style(config, config_home, "html_aspat", &style_sets[GEANY_FILETYPES_XML].styling[16]);
	get_keyfile_style(config, config_home, "html_cdata", &style_sets[GEANY_FILETYPES_XML].styling[17]);
	get_keyfile_style(config, config_home, "html_question", &style_sets[GEANY_FILETYPES_XML].styling[18]);
	get_keyfile_style(config, config_home, "html_value", &style_sets[GEANY_FILETYPES_XML].styling[19]);
	get_keyfile_style(config, config_home, "html_xccomment", &style_sets[GEANY_FILETYPES_XML].styling[20]);

	get_keyfile_style(config, config_home, "sgml_default", &style_sets[GEANY_FILETYPES_XML].styling[21]);
	get_keyfile_style(config, config_home, "sgml_comment", &style_sets[GEANY_FILETYPES_XML].styling[22]);
	get_keyfile_style(config, config_home, "sgml_special", &style_sets[GEANY_FILETYPES_XML].styling[23]);
	get_keyfile_style(config, config_home, "sgml_command", &style_sets[GEANY_FILETYPES_XML].styling[24]);
	get_keyfile_style(config, config_home, "sgml_doublestring", &style_sets[GEANY_FILETYPES_XML].styling[25]);
	get_keyfile_style(config, config_home, "sgml_simplestring", &style_sets[GEANY_FILETYPES_XML].styling[26]);
	get_keyfile_style(config, config_home, "sgml_1st_param", &style_sets[GEANY_FILETYPES_XML].styling[27]);
	get_keyfile_style(config, config_home, "sgml_entity", &style_sets[GEANY_FILETYPES_XML].styling[28]);
	get_keyfile_style(config, config_home, "sgml_block_default", &style_sets[GEANY_FILETYPES_XML].styling[29]);
	get_keyfile_style(config, config_home, "sgml_1st_param_comment", &style_sets[GEANY_FILETYPES_XML].styling[30]);
	get_keyfile_style(config, config_home, "sgml_error", &style_sets[GEANY_FILETYPES_XML].styling[31]);

	get_keyfile_style(config, config_home, "php_default", &style_sets[GEANY_FILETYPES_XML].styling[32]);
	get_keyfile_style(config, config_home, "php_simplestring", &style_sets[GEANY_FILETYPES_XML].styling[33]);
	get_keyfile_style(config, config_home, "php_hstring", &style_sets[GEANY_FILETYPES_XML].styling[34]);
	get_keyfile_style(config, config_home, "php_number", &style_sets[GEANY_FILETYPES_XML].styling[35]);
	get_keyfile_style(config, config_home, "php_word", &style_sets[GEANY_FILETYPES_XML].styling[36]);
	get_keyfile_style(config, config_home, "php_variable", &style_sets[GEANY_FILETYPES_XML].styling[37]);
	get_keyfile_style(config, config_home, "php_comment", &style_sets[GEANY_FILETYPES_XML].styling[38]);
	get_keyfile_style(config, config_home, "php_commentline", &style_sets[GEANY_FILETYPES_XML].styling[39]);
	get_keyfile_style(config, config_home, "php_operator", &style_sets[GEANY_FILETYPES_XML].styling[40]);
	get_keyfile_style(config, config_home, "php_hstring_variable", &style_sets[GEANY_FILETYPES_XML].styling[41]);
	get_keyfile_style(config, config_home, "php_complex_variable", &style_sets[GEANY_FILETYPES_XML].styling[42]);

	get_keyfile_style(config, config_home, "jscript_start", &style_sets[GEANY_FILETYPES_XML].styling[43]);
	get_keyfile_style(config, config_home, "jscript_default", &style_sets[GEANY_FILETYPES_XML].styling[44]);
	get_keyfile_style(config, config_home, "jscript_comment", &style_sets[GEANY_FILETYPES_XML].styling[45]);
	get_keyfile_style(config, config_home, "jscript_commentline", &style_sets[GEANY_FILETYPES_XML].styling[46]);
	get_keyfile_style(config, config_home, "jscript_commentdoc", &style_sets[GEANY_FILETYPES_XML].styling[47]);
	get_keyfile_style(config, config_home, "jscript_number", &style_sets[GEANY_FILETYPES_XML].styling[48]);
	get_keyfile_style(config, config_home, "jscript_word", &style_sets[GEANY_FILETYPES_XML].styling[49]);
	get_keyfile_style(config, config_home, "jscript_keyword", &style_sets[GEANY_FILETYPES_XML].styling[50]);
	get_keyfile_style(config, config_home, "jscript_doublestring", &style_sets[GEANY_FILETYPES_XML].styling[51]);
	get_keyfile_style(config, config_home, "jscript_singlestring", &style_sets[GEANY_FILETYPES_XML].styling[52]);
	get_keyfile_style(config, config_home, "jscript_symbols", &style_sets[GEANY_FILETYPES_XML].styling[53]);
	get_keyfile_style(config, config_home, "jscript_stringeol", &style_sets[GEANY_FILETYPES_XML].styling[54]);
	get_keyfile_style(config, config_home, "jscript_regex", &style_sets[GEANY_FILETYPES_XML].styling[55]);

	style_sets[GEANY_FILETYPES_XML].keywords = g_new(gchar*, 7);
	get_keyfile_keywords(config, config_home, "html", GEANY_FILETYPES_XML, 0);
	get_keyfile_keywords(config, config_home, "javascript", GEANY_FILETYPES_XML, 1);
	get_keyfile_keywords(config, config_home, "vbscript", GEANY_FILETYPES_XML, 2);
	get_keyfile_keywords(config, config_home, "python", GEANY_FILETYPES_XML, 3);
	get_keyfile_keywords(config, config_home, "php", GEANY_FILETYPES_XML, 4);
	get_keyfile_keywords(config, config_home, "sgml", GEANY_FILETYPES_XML, 5);
	style_sets[GEANY_FILETYPES_XML].keywords[6] = NULL;
}


static void styleset_markup(ScintillaObject *sci, gboolean set_keywords)
{
	guint i;
	const gchar *keywords;

	/* Used by several filetypes */
	if (style_sets[GEANY_FILETYPES_XML].styling == NULL)
		filetypes_load_config(GEANY_FILETYPES_XML, FALSE);

	/* manually initialise filetype Python for use with embedded Python */
	filetypes_load_config(GEANY_FILETYPES_PYTHON, FALSE);

	/* Set keywords. If we don't want to use keywords, we must at least unset maybe previously set
	 * keywords, e.g. when switching between filetypes. */
	for (i = 0; i < 5; i++)
	{
		keywords = (set_keywords) ? style_sets[GEANY_FILETYPES_XML].keywords[i] : "";
		sci_set_keywords(sci, i, keywords);
	}
	/* always set SGML keywords */
	sci_set_keywords(sci, 5, style_sets[GEANY_FILETYPES_XML].keywords[5]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_XML, 0);
	set_sci_style(sci, SCE_H_DEFAULT, GEANY_FILETYPES_XML, 0);
	set_sci_style(sci, SCE_H_TAG, GEANY_FILETYPES_XML, 1);
	set_sci_style(sci, SCE_H_TAGUNKNOWN, GEANY_FILETYPES_XML, 2);
	set_sci_style(sci, SCE_H_ATTRIBUTE, GEANY_FILETYPES_XML, 3);
	set_sci_style(sci, SCE_H_ATTRIBUTEUNKNOWN, GEANY_FILETYPES_XML, 4);
	set_sci_style(sci, SCE_H_NUMBER, GEANY_FILETYPES_XML, 5);
	set_sci_style(sci, SCE_H_DOUBLESTRING, GEANY_FILETYPES_XML, 6);
	set_sci_style(sci, SCE_H_SINGLESTRING, GEANY_FILETYPES_XML, 7);
	set_sci_style(sci, SCE_H_OTHER, GEANY_FILETYPES_XML, 8);
	set_sci_style(sci, SCE_H_COMMENT, GEANY_FILETYPES_XML, 9);
	set_sci_style(sci, SCE_H_ENTITY, GEANY_FILETYPES_XML, 10);
	set_sci_style(sci, SCE_H_TAGEND, GEANY_FILETYPES_XML, 11);
	SSM(sci, SCI_STYLESETEOLFILLED, SCE_H_XMLSTART, 1);
	set_sci_style(sci, SCE_H_XMLSTART, GEANY_FILETYPES_XML, 12);
	set_sci_style(sci, SCE_H_XMLEND, GEANY_FILETYPES_XML, 13);
	set_sci_style(sci, SCE_H_SCRIPT, GEANY_FILETYPES_XML, 14);
	SSM(sci, SCI_STYLESETEOLFILLED, SCE_H_ASP, 1);
	set_sci_style(sci, SCE_H_ASP, GEANY_FILETYPES_XML, 15);
	SSM(sci, SCI_STYLESETEOLFILLED, SCE_H_ASPAT, 1);
	set_sci_style(sci, SCE_H_ASPAT, GEANY_FILETYPES_XML, 16);
	set_sci_style(sci, SCE_H_CDATA, GEANY_FILETYPES_XML, 17);
	set_sci_style(sci, SCE_H_QUESTION, GEANY_FILETYPES_XML, 18);
	set_sci_style(sci, SCE_H_VALUE, GEANY_FILETYPES_XML, 19);
	set_sci_style(sci, SCE_H_XCCOMMENT, GEANY_FILETYPES_XML, 20);

	set_sci_style(sci, SCE_H_SGML_DEFAULT, GEANY_FILETYPES_XML, 21);
	set_sci_style(sci, SCE_H_SGML_COMMENT, GEANY_FILETYPES_XML, 22);
	set_sci_style(sci, SCE_H_SGML_SPECIAL, GEANY_FILETYPES_XML, 23);
	set_sci_style(sci, SCE_H_SGML_COMMAND, GEANY_FILETYPES_XML, 24);
	set_sci_style(sci, SCE_H_SGML_DOUBLESTRING, GEANY_FILETYPES_XML, 25);
	set_sci_style(sci, SCE_H_SGML_SIMPLESTRING, GEANY_FILETYPES_XML, 26);
	set_sci_style(sci, SCE_H_SGML_1ST_PARAM, GEANY_FILETYPES_XML, 27);
	set_sci_style(sci, SCE_H_SGML_ENTITY, GEANY_FILETYPES_XML, 28);
	set_sci_style(sci, SCE_H_SGML_BLOCK_DEFAULT, GEANY_FILETYPES_XML, 29);
	set_sci_style(sci, SCE_H_SGML_1ST_PARAM_COMMENT, GEANY_FILETYPES_XML, 30);
	set_sci_style(sci, SCE_H_SGML_ERROR, GEANY_FILETYPES_XML, 31);

	/* embedded JavaScript */
	set_sci_style(sci, SCE_HJ_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HJ_DEFAULT, GEANY_FILETYPES_XML, 44);
	set_sci_style(sci, SCE_HJ_COMMENT, GEANY_FILETYPES_XML, 45);
	set_sci_style(sci, SCE_HJ_COMMENTLINE, GEANY_FILETYPES_XML, 46);
	set_sci_style(sci, SCE_HJ_COMMENTDOC, GEANY_FILETYPES_XML, 47);
	set_sci_style(sci, SCE_HJ_NUMBER, GEANY_FILETYPES_XML, 48);
	set_sci_style(sci, SCE_HJ_WORD, GEANY_FILETYPES_XML, 49);
	set_sci_style(sci, SCE_HJ_KEYWORD, GEANY_FILETYPES_XML, 50);
	set_sci_style(sci, SCE_HJ_DOUBLESTRING, GEANY_FILETYPES_XML, 51);
	set_sci_style(sci, SCE_HJ_SINGLESTRING, GEANY_FILETYPES_XML, 52);
	set_sci_style(sci, SCE_HJ_SYMBOLS, GEANY_FILETYPES_XML, 53);
	set_sci_style(sci, SCE_HJ_STRINGEOL, GEANY_FILETYPES_XML, 54);
	set_sci_style(sci, SCE_HJ_REGEX, GEANY_FILETYPES_XML, 55);

	/* for HB, VBScript?, use the same styles as for JavaScript */
	set_sci_style(sci, SCE_HB_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HB_DEFAULT, GEANY_FILETYPES_XML, 44);
	set_sci_style(sci, SCE_HB_COMMENTLINE, GEANY_FILETYPES_XML, 46);
	set_sci_style(sci, SCE_HB_NUMBER, GEANY_FILETYPES_XML, 48);
	set_sci_style(sci, SCE_HB_WORD, GEANY_FILETYPES_XML, 50);	/* keywords */
	set_sci_style(sci, SCE_HB_STRING, GEANY_FILETYPES_XML, 51);
	set_sci_style(sci, SCE_HB_IDENTIFIER, GEANY_FILETYPES_XML, 53);
	set_sci_style(sci, SCE_HB_STRINGEOL, GEANY_FILETYPES_XML, 54);

	/* for HBA, VBScript?, use the same styles as for JavaScript */
	set_sci_style(sci, SCE_HBA_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HBA_DEFAULT, GEANY_FILETYPES_XML, 44);
	set_sci_style(sci, SCE_HBA_COMMENTLINE, GEANY_FILETYPES_XML, 46);
	set_sci_style(sci, SCE_HBA_NUMBER, GEANY_FILETYPES_XML, 48);
	set_sci_style(sci, SCE_HBA_WORD, GEANY_FILETYPES_XML, 50);	/* keywords */
	set_sci_style(sci, SCE_HBA_STRING, GEANY_FILETYPES_XML, 51);
	set_sci_style(sci, SCE_HBA_IDENTIFIER, GEANY_FILETYPES_XML, 53);
	set_sci_style(sci, SCE_HBA_STRINGEOL, GEANY_FILETYPES_XML, 54);

	/* for HJA, ASP Javascript, use the same styles as for JavaScript */
	set_sci_style(sci, SCE_HJA_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HJA_DEFAULT, GEANY_FILETYPES_XML, 44);
	set_sci_style(sci, SCE_HJA_COMMENT, GEANY_FILETYPES_XML, 45);
	set_sci_style(sci, SCE_HJA_COMMENTLINE, GEANY_FILETYPES_XML, 46);
	set_sci_style(sci, SCE_HJA_COMMENTDOC, GEANY_FILETYPES_XML, 47);
	set_sci_style(sci, SCE_HJA_NUMBER, GEANY_FILETYPES_XML, 48);
	set_sci_style(sci, SCE_HJA_WORD, GEANY_FILETYPES_XML, 49);
	set_sci_style(sci, SCE_HJA_KEYWORD, GEANY_FILETYPES_XML, 50);
	set_sci_style(sci, SCE_HJA_DOUBLESTRING, GEANY_FILETYPES_XML, 51);
	set_sci_style(sci, SCE_HJA_SINGLESTRING, GEANY_FILETYPES_XML, 52);
	set_sci_style(sci, SCE_HJA_SYMBOLS, GEANY_FILETYPES_XML, 53);
	set_sci_style(sci, SCE_HJA_STRINGEOL, GEANY_FILETYPES_XML, 54);

	/* for embedded Python we use the Python styles */
	set_sci_style(sci, SCE_HP_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HP_DEFAULT, GEANY_FILETYPES_PYTHON, 0);
	set_sci_style(sci, SCE_HP_COMMENTLINE, GEANY_FILETYPES_PYTHON, 1);
	set_sci_style(sci, SCE_HP_NUMBER, GEANY_FILETYPES_PYTHON, 2);
	set_sci_style(sci, SCE_HP_STRING, GEANY_FILETYPES_PYTHON, 3);
	set_sci_style(sci, SCE_HP_CHARACTER, GEANY_FILETYPES_PYTHON, 4);
	set_sci_style(sci, SCE_HP_WORD, GEANY_FILETYPES_PYTHON, 5);
	set_sci_style(sci, SCE_HP_TRIPLE, GEANY_FILETYPES_PYTHON, 6);
	set_sci_style(sci, SCE_HP_TRIPLEDOUBLE, GEANY_FILETYPES_PYTHON, 7);
	set_sci_style(sci, SCE_HP_CLASSNAME, GEANY_FILETYPES_PYTHON, 8);
	set_sci_style(sci, SCE_HP_DEFNAME, GEANY_FILETYPES_PYTHON, 9);
	set_sci_style(sci, SCE_HP_OPERATOR, GEANY_FILETYPES_PYTHON, 10);
	set_sci_style(sci, SCE_HP_IDENTIFIER, GEANY_FILETYPES_PYTHON, 11);

	/* for embedded HPA (what is this?) we use the Python styles */
	set_sci_style(sci, SCE_HPA_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HPA_DEFAULT, GEANY_FILETYPES_PYTHON, 0);
	set_sci_style(sci, SCE_HPA_COMMENTLINE, GEANY_FILETYPES_PYTHON, 1);
	set_sci_style(sci, SCE_HPA_NUMBER, GEANY_FILETYPES_PYTHON, 2);
	set_sci_style(sci, SCE_HPA_STRING, GEANY_FILETYPES_PYTHON, 3);
	set_sci_style(sci, SCE_HPA_CHARACTER, GEANY_FILETYPES_PYTHON, 4);
	set_sci_style(sci, SCE_HPA_WORD, GEANY_FILETYPES_PYTHON, 5);
	set_sci_style(sci, SCE_HPA_TRIPLE, GEANY_FILETYPES_PYTHON, 6);
	set_sci_style(sci, SCE_HPA_TRIPLEDOUBLE, GEANY_FILETYPES_PYTHON, 7);
	set_sci_style(sci, SCE_HPA_CLASSNAME, GEANY_FILETYPES_PYTHON, 8);
	set_sci_style(sci, SCE_HPA_DEFNAME, GEANY_FILETYPES_PYTHON, 9);
	set_sci_style(sci, SCE_HPA_OPERATOR, GEANY_FILETYPES_PYTHON, 10);
	set_sci_style(sci, SCE_HPA_IDENTIFIER, GEANY_FILETYPES_PYTHON, 11);

	/* PHP */
	set_sci_style(sci, SCE_HPHP_DEFAULT, GEANY_FILETYPES_XML, 32);
	set_sci_style(sci, SCE_HPHP_SIMPLESTRING, GEANY_FILETYPES_XML, 33);
	set_sci_style(sci, SCE_HPHP_HSTRING, GEANY_FILETYPES_XML, 34);
	set_sci_style(sci, SCE_HPHP_NUMBER, GEANY_FILETYPES_XML, 35);
	set_sci_style(sci, SCE_HPHP_WORD, GEANY_FILETYPES_XML, 36);
	set_sci_style(sci, SCE_HPHP_VARIABLE, GEANY_FILETYPES_XML, 37);
	set_sci_style(sci, SCE_HPHP_COMMENT, GEANY_FILETYPES_XML, 38);
	set_sci_style(sci, SCE_HPHP_COMMENTLINE, GEANY_FILETYPES_XML, 39);
	set_sci_style(sci, SCE_HPHP_OPERATOR, GEANY_FILETYPES_XML, 40);
	set_sci_style(sci, SCE_HPHP_HSTRING_VARIABLE, GEANY_FILETYPES_XML, 41);
	set_sci_style(sci, SCE_HPHP_COMPLEX_VARIABLE, GEANY_FILETYPES_XML, 42);

	/* note: normally this would be in the filetype file instead */
	sci_set_property(sci, "fold.html", "1");
	sci_set_property(sci, "fold.html.preprocessor", "0");
}


static void styleset_java_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	styleset_c_like_init(config, config_home, ft_id);

	style_sets[ft_id].keywords = g_new(gchar*, 5);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "secondary", ft_id, 1);
	get_keyfile_keywords(config, config_home, "doccomment", ft_id, 2);
	get_keyfile_keywords(config, config_home, "typedefs", ft_id, 3);
	style_sets[ft_id].keywords[4] = NULL;
}


static void styleset_java(ScintillaObject *sci, gint ft_id)
{
	styleset_c_like(sci, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);
	sci_set_keywords(sci, 4, style_sets[ft_id].keywords[3]);

	/* assign global types, merge them with user defined keywords and set them */
	assign_global_and_user_keywords(sci, style_sets[ft_id].keywords[1], filetypes[ft_id]->lang);
}


static void styleset_perl_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 35);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "error", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "preprocessor", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "scalar", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "pod", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "regex", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "array", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "hash", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "symboltable", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "backticks", &style_sets[ft_id].styling[16]);
	get_keyfile_style(config, config_home, "pod_verbatim", &style_sets[ft_id].styling[17]);
	get_keyfile_style(config, config_home, "reg_subst", &style_sets[ft_id].styling[18]);
	get_keyfile_style(config, config_home, "datasection", &style_sets[ft_id].styling[19]);
	get_keyfile_style(config, config_home, "here_delim", &style_sets[ft_id].styling[20]);
	get_keyfile_style(config, config_home, "here_q", &style_sets[ft_id].styling[21]);
	get_keyfile_style(config, config_home, "here_qq", &style_sets[ft_id].styling[22]);
	get_keyfile_style(config, config_home, "here_qx", &style_sets[ft_id].styling[23]);
	get_keyfile_style(config, config_home, "string_q", &style_sets[ft_id].styling[24]);
	get_keyfile_style(config, config_home, "string_qq", &style_sets[ft_id].styling[25]);
	get_keyfile_style(config, config_home, "string_qx", &style_sets[ft_id].styling[26]);
	get_keyfile_style(config, config_home, "string_qr", &style_sets[ft_id].styling[27]);
	get_keyfile_style(config, config_home, "string_qw", &style_sets[ft_id].styling[28]);
	get_keyfile_style(config, config_home, "variable_indexer", &style_sets[ft_id].styling[29]);
	get_keyfile_style(config, config_home, "punctuation", &style_sets[ft_id].styling[30]);
	get_keyfile_style(config, config_home, "longquote", &style_sets[ft_id].styling[31]);
	get_keyfile_style(config, config_home, "sub_prototype", &style_sets[ft_id].styling[32]);
	get_keyfile_style(config, config_home, "format_ident", &style_sets[ft_id].styling[33]);
	get_keyfile_style(config, config_home, "format", &style_sets[ft_id].styling[34]);


	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_perl(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_PERL, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_PL_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_PL_ERROR, ft_id, 1);
	set_sci_style(sci, SCE_PL_COMMENTLINE, ft_id, 2);
	set_sci_style(sci, SCE_PL_NUMBER, ft_id, 3);
	set_sci_style(sci, SCE_PL_WORD, ft_id, 4);
	set_sci_style(sci, SCE_PL_STRING, ft_id, 5);
	set_sci_style(sci, SCE_PL_CHARACTER, ft_id, 6);
	set_sci_style(sci, SCE_PL_PREPROCESSOR, ft_id, 7);
	set_sci_style(sci, SCE_PL_OPERATOR, ft_id, 8);
	set_sci_style(sci, SCE_PL_IDENTIFIER, ft_id, 9);
	set_sci_style(sci, SCE_PL_SCALAR, ft_id, 10);
	set_sci_style(sci, SCE_PL_POD, ft_id, 11);
	set_sci_style(sci, SCE_PL_REGEX, ft_id, 12);
	set_sci_style(sci, SCE_PL_ARRAY, ft_id, 13);
	set_sci_style(sci, SCE_PL_HASH, ft_id, 14);
	set_sci_style(sci, SCE_PL_SYMBOLTABLE, ft_id, 15);
	set_sci_style(sci, SCE_PL_BACKTICKS, ft_id, 16);
	set_sci_style(sci, SCE_PL_POD_VERB, ft_id, 17);
	set_sci_style(sci, SCE_PL_REGSUBST, ft_id, 18);
	set_sci_style(sci, SCE_PL_DATASECTION, ft_id, 19);
	set_sci_style(sci, SCE_PL_HERE_DELIM, ft_id, 20);
	set_sci_style(sci, SCE_PL_HERE_Q, ft_id, 21);
	set_sci_style(sci, SCE_PL_HERE_QQ, ft_id, 22);
	set_sci_style(sci, SCE_PL_HERE_QX, ft_id, 23);
	set_sci_style(sci, SCE_PL_STRING_Q, ft_id, 24);
	set_sci_style(sci, SCE_PL_STRING_QQ, ft_id, 25);
	set_sci_style(sci, SCE_PL_STRING_QX, ft_id, 26);
	set_sci_style(sci, SCE_PL_STRING_QR, ft_id, 27);
	set_sci_style(sci, SCE_PL_STRING_QW, ft_id, 28);
	set_sci_style(sci, SCE_PL_VARIABLE_INDEXER, ft_id, 29);
	set_sci_style(sci, SCE_PL_PUNCTUATION, ft_id, 30);
	set_sci_style(sci, SCE_PL_LONGQUOTE, ft_id, 31);
	set_sci_style(sci, SCE_PL_SUB_PROTOTYPE, ft_id, 32);
	set_sci_style(sci, SCE_PL_FORMAT_IDENT, ft_id, 33);
	set_sci_style(sci, SCE_PL_FORMAT, ft_id, 34);
}


static void styleset_python_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 16);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "triple", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "tripledouble", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "classname", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "defname", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "commentblock", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "word2", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "decorator", &style_sets[ft_id].styling[15]);

	style_sets[ft_id].keywords = g_new(gchar*, 3);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "identifiers", ft_id, 1);
	style_sets[ft_id].keywords[2] = NULL;
}


static void styleset_python(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_PYTHON, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_P_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_P_COMMENTLINE, ft_id, 1);
	set_sci_style(sci, SCE_P_NUMBER, ft_id, 2);
	set_sci_style(sci, SCE_P_STRING, ft_id, 3);
	set_sci_style(sci, SCE_P_CHARACTER, ft_id, 4);
	set_sci_style(sci, SCE_P_WORD, ft_id, 5);
	set_sci_style(sci, SCE_P_TRIPLE, ft_id, 6);
	set_sci_style(sci, SCE_P_TRIPLEDOUBLE, ft_id, 7);
	set_sci_style(sci, SCE_P_CLASSNAME, ft_id, 8);
	set_sci_style(sci, SCE_P_DEFNAME, ft_id, 9);
	set_sci_style(sci, SCE_P_OPERATOR, ft_id, 10);
	set_sci_style(sci, SCE_P_IDENTIFIER, ft_id, 11);
	set_sci_style(sci, SCE_P_COMMENTBLOCK, ft_id, 12);
	set_sci_style(sci, SCE_P_STRINGEOL, ft_id, 13);
	set_sci_style(sci, SCE_P_WORD2, ft_id, 14);
	set_sci_style(sci, SCE_P_DECORATOR, ft_id, 15);
}


static void styleset_cmake_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 15);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "stringdq", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "stringlq", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "stringrq", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "command", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "parameters", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "variable", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "userdefined", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "whiledef", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "foreachdef", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "ifdefinedef", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "macrodef", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "stringvar", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[14]);

	style_sets[ft_id].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "commands", ft_id, 0);
	get_keyfile_keywords(config, config_home, "parameters", ft_id, 1);
	get_keyfile_keywords(config, config_home, "userdefined", ft_id, 2);
	style_sets[ft_id].keywords[3] = NULL;
}


static void styleset_cmake(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_CMAKE, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_CMAKE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_CMAKE_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_CMAKE_STRINGDQ, ft_id, 2);
	set_sci_style(sci, SCE_CMAKE_STRINGLQ, ft_id, 3);
	set_sci_style(sci, SCE_CMAKE_STRINGRQ, ft_id, 4);
	set_sci_style(sci, SCE_CMAKE_COMMANDS, ft_id, 5);
	set_sci_style(sci, SCE_CMAKE_PARAMETERS, ft_id, 6);
	set_sci_style(sci, SCE_CMAKE_VARIABLE, ft_id, 7);
	set_sci_style(sci, SCE_CMAKE_USERDEFINED, ft_id, 8);
	set_sci_style(sci, SCE_CMAKE_WHILEDEF, ft_id, 9);
	set_sci_style(sci, SCE_CMAKE_FOREACHDEF, ft_id, 10);
	set_sci_style(sci, SCE_CMAKE_IFDEFINEDEF, ft_id, 11);
	set_sci_style(sci, SCE_CMAKE_MACRODEF, ft_id, 12);
	set_sci_style(sci, SCE_CMAKE_STRINGVAR, ft_id, 13);
	set_sci_style(sci, SCE_CMAKE_NUMBER, ft_id, 14);
}


static void styleset_r_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 12);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "kword", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "basekword", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "otherkword", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "string2", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "infix", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "infixeol", &style_sets[ft_id].styling[11]);

	style_sets[ft_id].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "package", ft_id, 1);
	get_keyfile_keywords(config, config_home, "package_other", ft_id, 2);
	style_sets[ft_id].keywords[3] = NULL;
}


static void styleset_r(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_R, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_R_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_R_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_R_KWORD, ft_id, 2);
	set_sci_style(sci, SCE_R_OPERATOR, ft_id, 3);
	set_sci_style(sci, SCE_R_BASEKWORD, ft_id, 4);
	set_sci_style(sci, SCE_R_OTHERKWORD, ft_id, 5);
	set_sci_style(sci, SCE_R_NUMBER, ft_id, 6);
	set_sci_style(sci, SCE_R_STRING, ft_id, 7);
	set_sci_style(sci, SCE_R_STRING2, ft_id, 8);
	set_sci_style(sci, SCE_R_IDENTIFIER, ft_id, 9);
	set_sci_style(sci, SCE_R_INFIX, ft_id, 10);
	set_sci_style(sci, SCE_R_INFIXEOL, ft_id, 11);
}


static void styleset_ruby_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 35);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "global", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "symbol", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "classname", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "defname", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "modulename", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "backticks", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "instancevar", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "classvar", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "datasection", &style_sets[ft_id].styling[16]);
	get_keyfile_style(config, config_home, "heredelim", &style_sets[ft_id].styling[17]);
	get_keyfile_style(config, config_home, "worddemoted", &style_sets[ft_id].styling[18]);
	get_keyfile_style(config, config_home, "stdin", &style_sets[ft_id].styling[19]);
	get_keyfile_style(config, config_home, "stdout", &style_sets[ft_id].styling[20]);
	get_keyfile_style(config, config_home, "stderr", &style_sets[ft_id].styling[21]);
	get_keyfile_style(config, config_home, "datasection", &style_sets[ft_id].styling[22]);
	get_keyfile_style(config, config_home, "regex", &style_sets[ft_id].styling[23]);
	get_keyfile_style(config, config_home, "here_q", &style_sets[ft_id].styling[24]);
	get_keyfile_style(config, config_home, "here_qq", &style_sets[ft_id].styling[25]);
	get_keyfile_style(config, config_home, "here_qx", &style_sets[ft_id].styling[26]);
	get_keyfile_style(config, config_home, "string_q", &style_sets[ft_id].styling[27]);
	get_keyfile_style(config, config_home, "string_qq", &style_sets[ft_id].styling[28]);
	get_keyfile_style(config, config_home, "string_qx", &style_sets[ft_id].styling[29]);
	get_keyfile_style(config, config_home, "string_qr", &style_sets[ft_id].styling[30]);
	get_keyfile_style(config, config_home, "string_qw", &style_sets[ft_id].styling[31]);
	get_keyfile_style(config, config_home, "upper_bound", &style_sets[ft_id].styling[32]);
	get_keyfile_style(config, config_home, "error", &style_sets[ft_id].styling[33]);
	get_keyfile_style(config, config_home, "pod", &style_sets[ft_id].styling[34]);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_ruby(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_RUBY, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_RB_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_RB_COMMENTLINE, ft_id, 1);
	set_sci_style(sci, SCE_RB_NUMBER, ft_id, 2);
	set_sci_style(sci, SCE_RB_STRING, ft_id, 3);
	set_sci_style(sci, SCE_RB_CHARACTER, ft_id, 4);
	set_sci_style(sci, SCE_RB_WORD, ft_id, 5);
	set_sci_style(sci, SCE_RB_GLOBAL, ft_id, 6);
	set_sci_style(sci, SCE_RB_SYMBOL, ft_id, 7);
	set_sci_style(sci, SCE_RB_CLASSNAME, ft_id, 8);
	set_sci_style(sci, SCE_RB_DEFNAME, ft_id, 9);
	set_sci_style(sci, SCE_RB_OPERATOR, ft_id, 10);
	set_sci_style(sci, SCE_RB_IDENTIFIER, ft_id, 11);
	set_sci_style(sci, SCE_RB_MODULE_NAME, ft_id, 12);
	set_sci_style(sci, SCE_RB_BACKTICKS, ft_id, 13);
	set_sci_style(sci, SCE_RB_INSTANCE_VAR, ft_id, 14);
	set_sci_style(sci, SCE_RB_CLASS_VAR, ft_id, 15);
	set_sci_style(sci, SCE_RB_DATASECTION, ft_id, 16);
	set_sci_style(sci, SCE_RB_HERE_DELIM, ft_id, 17);
	set_sci_style(sci, SCE_RB_WORD_DEMOTED, ft_id, 18);
	set_sci_style(sci, SCE_RB_STDIN, ft_id, 19);
	set_sci_style(sci, SCE_RB_STDOUT, ft_id, 20);
	set_sci_style(sci, SCE_RB_STDERR, ft_id, 21);
	set_sci_style(sci, SCE_RB_DATASECTION, ft_id, 22);
	set_sci_style(sci, SCE_RB_REGEX, ft_id, 23);
	set_sci_style(sci, SCE_RB_HERE_Q, ft_id, 24);
	set_sci_style(sci, SCE_RB_HERE_QQ, ft_id, 25);
	set_sci_style(sci, SCE_RB_HERE_QX, ft_id, 26);
	set_sci_style(sci, SCE_RB_STRING_Q, ft_id, 27);
	set_sci_style(sci, SCE_RB_STRING_QQ, ft_id, 28);
	set_sci_style(sci, SCE_RB_STRING_QX, ft_id, 29);
	set_sci_style(sci, SCE_RB_STRING_QR, ft_id, 30);
	set_sci_style(sci, SCE_RB_STRING_QW, ft_id, 31);
	set_sci_style(sci, SCE_RB_UPPER_BOUND, ft_id, 32);
	set_sci_style(sci, SCE_RB_ERROR, ft_id, 33);
	set_sci_style(sci, SCE_RB_POD, ft_id, 34);
}


static void styleset_sh_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 14);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "backticks", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "param", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "scalar", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "error", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "here_delim", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "here_q", &style_sets[ft_id].styling[13]);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_sh(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_BASH, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_SH_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_SH_COMMENTLINE, ft_id, 1);
	set_sci_style(sci, SCE_SH_NUMBER, ft_id, 2);
	set_sci_style(sci, SCE_SH_WORD, ft_id, 3);
	set_sci_style(sci, SCE_SH_STRING, ft_id, 4);
	set_sci_style(sci, SCE_SH_CHARACTER, ft_id, 5);
	set_sci_style(sci, SCE_SH_OPERATOR, ft_id, 6);
	set_sci_style(sci, SCE_SH_IDENTIFIER, ft_id, 7);
	set_sci_style(sci, SCE_SH_BACKTICKS, ft_id, 8);
	set_sci_style(sci, SCE_SH_PARAM, ft_id, 9);
	set_sci_style(sci, SCE_SH_SCALAR, ft_id, 10);
	set_sci_style(sci, SCE_SH_ERROR, ft_id, 11);
	set_sci_style(sci, SCE_SH_HERE_DELIM, ft_id, 12);
	set_sci_style(sci, SCE_SH_HERE_Q, ft_id, 13);
}


static void styleset_xml(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_XML, ft_id);

	/* use the same colouring for HTML; XML and so on */
	styleset_markup(sci, FALSE);
}


static void styleset_docbook_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 29);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "tag", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "tagunknown", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "attribute", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "attributeunknown", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "doublestring", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "singlestring", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "other", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "entity", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "tagend", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "xmlstart", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "xmlend", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "cdata", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "question", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "value", &style_sets[ft_id].styling[16]);
	get_keyfile_style(config, config_home, "xccomment", &style_sets[ft_id].styling[17]);
	get_keyfile_style(config, config_home, "sgml_default", &style_sets[ft_id].styling[18]);
	get_keyfile_style(config, config_home, "sgml_comment", &style_sets[ft_id].styling[19]);
	get_keyfile_style(config, config_home, "sgml_special", &style_sets[ft_id].styling[20]);
	get_keyfile_style(config, config_home, "sgml_command", &style_sets[ft_id].styling[21]);
	get_keyfile_style(config, config_home, "sgml_doublestring", &style_sets[ft_id].styling[22]);
	get_keyfile_style(config, config_home, "sgml_simplestring", &style_sets[ft_id].styling[23]);
	get_keyfile_style(config, config_home, "sgml_1st_param", &style_sets[ft_id].styling[24]);
	get_keyfile_style(config, config_home, "sgml_entity", &style_sets[ft_id].styling[25]);
	get_keyfile_style(config, config_home, "sgml_block_default", &style_sets[ft_id].styling[26]);
	get_keyfile_style(config, config_home, "sgml_1st_param_comment", &style_sets[ft_id].styling[27]);
	get_keyfile_style(config, config_home, "sgml_error", &style_sets[ft_id].styling[28]);

	style_sets[ft_id].keywords = g_new(gchar*, 3);
	get_keyfile_keywords(config, config_home, "elements", ft_id, 0);
	get_keyfile_keywords(config, config_home, "dtd", ft_id, 1);
	style_sets[ft_id].keywords[2] = NULL;
}


static void styleset_docbook(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_XML, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 5, style_sets[ft_id].keywords[1]);

	/* Unknown tags and attributes are highlighed in red.
	 * If a tag is actually OK, it should be added in lower case to the htmlKeyWords string. */

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_H_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_H_TAG, ft_id, 1);
	set_sci_style(sci, SCE_H_TAGUNKNOWN, ft_id, 2);
	set_sci_style(sci, SCE_H_ATTRIBUTE, ft_id, 3);
	set_sci_style(sci, SCE_H_ATTRIBUTEUNKNOWN, ft_id, 4);
	set_sci_style(sci, SCE_H_NUMBER, ft_id, 5);
	set_sci_style(sci, SCE_H_DOUBLESTRING, ft_id, 6);
	set_sci_style(sci, SCE_H_SINGLESTRING, ft_id, 7);
	set_sci_style(sci, SCE_H_OTHER, ft_id, 8);
	set_sci_style(sci, SCE_H_COMMENT, ft_id, 9);
	set_sci_style(sci, SCE_H_ENTITY, ft_id, 10);
	set_sci_style(sci, SCE_H_TAGEND, ft_id, 11);
	SSM(sci, SCI_STYLESETEOLFILLED, SCE_H_XMLSTART, 1);
	set_sci_style(sci, SCE_H_XMLSTART, ft_id, 12);
	set_sci_style(sci, SCE_H_XMLEND, ft_id, 13);
	set_sci_style(sci, SCE_H_CDATA, ft_id, 14);
	set_sci_style(sci, SCE_H_QUESTION, ft_id, 15);
	set_sci_style(sci, SCE_H_VALUE, ft_id, 16);
	set_sci_style(sci, SCE_H_XCCOMMENT, ft_id, 17);
	set_sci_style(sci, SCE_H_SGML_DEFAULT, ft_id, 18);
	set_sci_style(sci, SCE_H_DEFAULT, ft_id, 19);
	set_sci_style(sci, SCE_H_SGML_SPECIAL, ft_id, 20);
	set_sci_style(sci, SCE_H_SGML_COMMAND, ft_id, 21);
	set_sci_style(sci, SCE_H_SGML_DOUBLESTRING, ft_id, 22);
	set_sci_style(sci, SCE_H_SGML_SIMPLESTRING, ft_id, 23);
	set_sci_style(sci, SCE_H_SGML_1ST_PARAM, ft_id, 24);
	set_sci_style(sci, SCE_H_SGML_ENTITY, ft_id, 25);
	set_sci_style(sci, SCE_H_SGML_BLOCK_DEFAULT, ft_id, 26);
	set_sci_style(sci, SCE_H_SGML_1ST_PARAM_COMMENT, ft_id, 27);
	set_sci_style(sci, SCE_H_SGML_ERROR, ft_id, 28);
}


static void styleset_default(ScintillaObject *sci, gint ft_id)
{
	SSM(sci, SCI_SETLEXER, SCLEX_NULL, 0);

	/* we need to set STYLE_DEFAULT before we call SCI_STYLECLEARALL in styleset_common() */
	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_NONE, GCS_DEFAULT);

	styleset_common(sci, ft_id);
}


static void styleset_css_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 22);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "tag", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "class", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "pseudoclass", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "unknown_pseudoclass", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "unknown_identifier", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "doublestring", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "singlestring", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "attribute", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "value", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "id", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "identifier2", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "important", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "directive", &style_sets[ft_id].styling[16]);
	get_keyfile_style(config, config_home, "identifier3", &style_sets[ft_id].styling[17]);
	get_keyfile_style(config, config_home, "pseudoelement", &style_sets[ft_id].styling[18]);
	get_keyfile_style(config, config_home, "extended_identifier", &style_sets[ft_id].styling[19]);
	get_keyfile_style(config, config_home, "extended_pseudoclass", &style_sets[ft_id].styling[20]);
	get_keyfile_style(config, config_home, "extended_pseudoelement", &style_sets[ft_id].styling[21]);

	style_sets[ft_id].keywords = g_new(gchar*, 9);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "pseudoclasses", ft_id, 1);
	get_keyfile_keywords(config, config_home, "secondary", ft_id, 2);
	get_keyfile_keywords(config, config_home, "css3_properties", ft_id, 3);
	get_keyfile_keywords(config, config_home, "pseudo_elements", ft_id, 4);
	get_keyfile_keywords(config, config_home, "browser_css_properties", ft_id, 5);
	get_keyfile_keywords(config, config_home, "browser_pseudo_classes", ft_id, 6);
	get_keyfile_keywords(config, config_home, "browser_pseudo_elements", ft_id, 7);
	style_sets[ft_id].keywords[8] = NULL;
}


static void styleset_css(ScintillaObject *sci, gint ft_id)
{
	guint i;

	apply_filetype_properties(sci, SCLEX_CSS, ft_id);

	for (i = 0; i < 8; i++)
	{
		sci_set_keywords(sci, i, style_sets[ft_id].keywords[i]);
	}

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_CSS_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_CSS_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_CSS_TAG, ft_id, 2);
	set_sci_style(sci, SCE_CSS_CLASS, ft_id, 3);
	set_sci_style(sci, SCE_CSS_PSEUDOCLASS, ft_id, 4);
	set_sci_style(sci, SCE_CSS_UNKNOWN_PSEUDOCLASS, ft_id, 5);
	set_sci_style(sci, SCE_CSS_UNKNOWN_IDENTIFIER, ft_id, 6);
	set_sci_style(sci, SCE_CSS_OPERATOR, ft_id, 7);
	set_sci_style(sci, SCE_CSS_IDENTIFIER, ft_id, 8);
	set_sci_style(sci, SCE_CSS_DOUBLESTRING, ft_id, 9);
	set_sci_style(sci, SCE_CSS_SINGLESTRING, ft_id, 10);
	set_sci_style(sci, SCE_CSS_ATTRIBUTE, ft_id, 11);
	set_sci_style(sci, SCE_CSS_VALUE, ft_id, 12);
	set_sci_style(sci, SCE_CSS_ID, ft_id, 13);
	set_sci_style(sci, SCE_CSS_IDENTIFIER2, ft_id, 14);
	set_sci_style(sci, SCE_CSS_IMPORTANT, ft_id, 15);
	set_sci_style(sci, SCE_CSS_DIRECTIVE, ft_id, 16);
	set_sci_style(sci, SCE_CSS_IDENTIFIER3, ft_id, 17);
	set_sci_style(sci, SCE_CSS_PSEUDOELEMENT, ft_id, 18);
	set_sci_style(sci, SCE_CSS_EXTENDED_IDENTIFIER, ft_id, 19);
	set_sci_style(sci, SCE_CSS_EXTENDED_PSEUDOCLASS, ft_id, 20);
	set_sci_style(sci, SCE_CSS_EXTENDED_PSEUDOELEMENT, ft_id, 21);
}


static void styleset_nsis_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 19);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "stringdq", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "stringlq", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "stringrq", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "function", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "variable", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "label", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "userdefined", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "sectiondef", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "subsectiondef", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "ifdefinedef", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "macrodef", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "stringvar", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "sectiongroup", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "pageex", &style_sets[ft_id].styling[16]);
	get_keyfile_style(config, config_home, "functiondef", &style_sets[ft_id].styling[17]);
	get_keyfile_style(config, config_home, "commentbox", &style_sets[ft_id].styling[18]);

	style_sets[ft_id].keywords = g_new(gchar*, 5);
	get_keyfile_keywords(config, config_home, "functions", ft_id, 0);
	get_keyfile_keywords(config, config_home, "variables", ft_id, 1);
	get_keyfile_keywords(config, config_home, "lables", ft_id, 2);
	get_keyfile_keywords(config, config_home, "userdefined", ft_id, 3);
	style_sets[ft_id].keywords[4] = NULL;
}


static void styleset_nsis(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_NSIS, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);
	sci_set_keywords(sci, 3, style_sets[ft_id].keywords[3]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_NSIS_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_NSIS_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_NSIS_STRINGDQ, ft_id, 2);
	set_sci_style(sci, SCE_NSIS_STRINGLQ, ft_id, 3);
	set_sci_style(sci, SCE_NSIS_STRINGRQ, ft_id, 4);
	set_sci_style(sci, SCE_NSIS_FUNCTION, ft_id, 5);
	set_sci_style(sci, SCE_NSIS_VARIABLE, ft_id, 6);
	set_sci_style(sci, SCE_NSIS_LABEL, ft_id, 7);
	set_sci_style(sci, SCE_NSIS_USERDEFINED, ft_id, 8);
	set_sci_style(sci, SCE_NSIS_SECTIONDEF, ft_id, 9);
	set_sci_style(sci, SCE_NSIS_SUBSECTIONDEF, ft_id, 10);
	set_sci_style(sci, SCE_NSIS_IFDEFINEDEF, ft_id, 11);
	set_sci_style(sci, SCE_NSIS_MACRODEF, ft_id, 12);
	set_sci_style(sci, SCE_NSIS_STRINGVAR, ft_id, 13);
	set_sci_style(sci, SCE_NSIS_NUMBER, ft_id, 14);
	set_sci_style(sci, SCE_NSIS_SECTIONGROUP, ft_id, 15);
	set_sci_style(sci, SCE_NSIS_PAGEEX, ft_id, 16);
	set_sci_style(sci, SCE_NSIS_FUNCTIONDEF, ft_id, 17);
	set_sci_style(sci, SCE_NSIS_COMMENTBOX, ft_id, 18);
}


static void styleset_po_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 9);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "msgid", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "msgid_text", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "msgstr", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "msgstr_text", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "msgctxt", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "msgctxt_text", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "fuzzy", &style_sets[ft_id].styling[8]);

	style_sets[ft_id].keywords = NULL;
}


static void styleset_po(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_PO, ft_id);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_PO_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_PO_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_PO_MSGID, ft_id, 2);
	set_sci_style(sci, SCE_PO_MSGID_TEXT, ft_id, 3);
	set_sci_style(sci, SCE_PO_MSGSTR, ft_id, 4);
	set_sci_style(sci, SCE_PO_MSGSTR_TEXT, ft_id, 5);
	set_sci_style(sci, SCE_PO_MSGCTXT, ft_id, 6);
	set_sci_style(sci, SCE_PO_MSGCTXT_TEXT, ft_id, 7);
	set_sci_style(sci, SCE_PO_FUZZY, ft_id, 8);
}


static void styleset_conf_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 6);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "section", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "key", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "assignment", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "defval", &style_sets[ft_id].styling[5]);

	style_sets[ft_id].keywords = NULL;
}


static void styleset_conf(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_PROPERTIES, ft_id);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_PROPS_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_PROPS_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_PROPS_SECTION, ft_id, 2);
	set_sci_style(sci, SCE_PROPS_KEY, ft_id, 3);
	set_sci_style(sci, SCE_PROPS_ASSIGNMENT, ft_id, 4);
	set_sci_style(sci, SCE_PROPS_DEFVAL, ft_id, 5);
}


static void styleset_asm_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 15);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "cpuinstruction", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "mathinstruction", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "register", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "directive", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "directiveoperand", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "commentblock", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "extinstruction", &style_sets[ft_id].styling[14]);

	style_sets[ft_id].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "instructions", ft_id, 0);
	get_keyfile_keywords(config, config_home, "registers", ft_id, 1);
	get_keyfile_keywords(config, config_home, "directives", ft_id, 2);
	style_sets[ft_id].keywords[3] = NULL;
}


static void styleset_asm(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_ASM, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	/*sci_set_keywords(sci, 1, style_sets[ft_id].keywords[0]);*/
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 3, style_sets[ft_id].keywords[2]);
	/*sci_set_keywords(sci, 5, style_sets[ft_id].keywords[0]);*/

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_ASM_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_ASM_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_ASM_NUMBER, ft_id, 2);
	set_sci_style(sci, SCE_ASM_STRING, ft_id, 3);
	set_sci_style(sci, SCE_ASM_OPERATOR, ft_id, 4);
	set_sci_style(sci, SCE_ASM_IDENTIFIER, ft_id, 5);
	set_sci_style(sci, SCE_ASM_CPUINSTRUCTION, ft_id, 6);
	set_sci_style(sci, SCE_ASM_MATHINSTRUCTION, ft_id, 7);
	set_sci_style(sci, SCE_ASM_REGISTER, ft_id, 8);
	set_sci_style(sci, SCE_ASM_DIRECTIVE, ft_id, 9);
	set_sci_style(sci, SCE_ASM_DIRECTIVEOPERAND, ft_id, 10);
	set_sci_style(sci, SCE_ASM_COMMENTBLOCK, ft_id, 11);
	set_sci_style(sci, SCE_ASM_CHARACTER, ft_id, 12);
	set_sci_style(sci, SCE_ASM_STRINGEOL, ft_id, 13);
	set_sci_style(sci, SCE_ASM_EXTINSTRUCTION, ft_id, 14);
}


static void styleset_f77_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 15);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "string2", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "word2", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "word3", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "preprocessor", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "operator2", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "continuation", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "label", &style_sets[ft_id].styling[14]);

	style_sets[ft_id].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "intrinsic_functions", ft_id, 1);
	get_keyfile_keywords(config, config_home, "user_functions", ft_id, 2);
	style_sets[ft_id].keywords[3] = NULL;
}


static void styleset_f77(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_F77, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_F_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_F_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_F_NUMBER, ft_id, 2);
	set_sci_style(sci, SCE_F_STRING1, ft_id, 3);
	set_sci_style(sci, SCE_F_OPERATOR, ft_id, 4);
	set_sci_style(sci, SCE_F_IDENTIFIER, ft_id, 5);
	set_sci_style(sci, SCE_F_STRING2, ft_id, 6);
	set_sci_style(sci, SCE_F_WORD, ft_id, 7);
	set_sci_style(sci, SCE_F_WORD2, ft_id, 8);
	set_sci_style(sci, SCE_F_WORD3, ft_id, 9);
	set_sci_style(sci, SCE_F_PREPROCESSOR, ft_id, 10);
	set_sci_style(sci, SCE_F_OPERATOR2, ft_id, 11);
	set_sci_style(sci, SCE_F_CONTINUATION, ft_id, 12);
	set_sci_style(sci, SCE_F_STRINGEOL, ft_id, 13);
	set_sci_style(sci, SCE_F_LABEL, ft_id, 14);
}


static void styleset_fortran_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 15);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "string2", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "word2", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "word3", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "preprocessor", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "operator2", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "continuation", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "label", &style_sets[ft_id].styling[14]);

	style_sets[ft_id].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "intrinsic_functions", ft_id, 1);
	get_keyfile_keywords(config, config_home, "user_functions", ft_id, 2);
	style_sets[ft_id].keywords[3] = NULL;
}


static void styleset_fortran(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_FORTRAN, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_F_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_F_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_F_NUMBER, ft_id, 2);
	set_sci_style(sci, SCE_F_STRING1, ft_id, 3);
	set_sci_style(sci, SCE_F_OPERATOR, ft_id, 4);
	set_sci_style(sci, SCE_F_IDENTIFIER, ft_id, 5);
	set_sci_style(sci, SCE_F_STRING2, ft_id, 6);
	set_sci_style(sci, SCE_F_WORD, ft_id, 7);
	set_sci_style(sci, SCE_F_WORD2, ft_id, 8);
	set_sci_style(sci, SCE_F_WORD3, ft_id, 9);
	set_sci_style(sci, SCE_F_PREPROCESSOR, ft_id, 10);
	set_sci_style(sci, SCE_F_OPERATOR2, ft_id, 11);
	set_sci_style(sci, SCE_F_CONTINUATION, ft_id, 12);
	set_sci_style(sci, SCE_F_STRINGEOL, ft_id, 13);
	set_sci_style(sci, SCE_F_LABEL, ft_id, 14);
}


static void styleset_sql_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 15);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "commentdoc", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "word2", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "sqlplus", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "sqlplus_prompt", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "sqlplus_comment", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "quotedidentifier", &style_sets[ft_id].styling[14]);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_sql(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_SQL, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_SQL_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_SQL_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_SQL_COMMENTLINE, ft_id, 2);
	set_sci_style(sci, SCE_SQL_COMMENTDOC, ft_id, 3);
	set_sci_style(sci, SCE_SQL_NUMBER, ft_id, 4);
	set_sci_style(sci, SCE_SQL_WORD, ft_id, 5);
	set_sci_style(sci, SCE_SQL_WORD2, ft_id, 6);
	set_sci_style(sci, SCE_SQL_STRING, ft_id, 7);
	set_sci_style(sci, SCE_SQL_CHARACTER, ft_id, 8);
	set_sci_style(sci, SCE_SQL_OPERATOR, ft_id, 9);
	set_sci_style(sci, SCE_SQL_IDENTIFIER, ft_id, 10);
	set_sci_style(sci, SCE_SQL_SQLPLUS, ft_id, 11);
	set_sci_style(sci, SCE_SQL_SQLPLUS_PROMPT, ft_id, 12);
	set_sci_style(sci, SCE_SQL_SQLPLUS_COMMENT, ft_id, 13);
	set_sci_style(sci, SCE_SQL_QUOTEDIDENTIFIER, ft_id, 14);
}


static void styleset_markdown_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 17);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "strong", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "emphasis", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "header1", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "header2", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "header3", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "header4", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "header5", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "header6", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "ulist_item", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "olist_item", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "blockquote", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "strikeout", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "hrule", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "link", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "code", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "codebk", &style_sets[ft_id].styling[16]);

	style_sets[ft_id].keywords = NULL;
}


static void styleset_markdown(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_MARKDOWN, ft_id);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_MARKDOWN_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_MARKDOWN_LINE_BEGIN, ft_id, 0);
	set_sci_style(sci, SCE_MARKDOWN_PRECHAR, ft_id, 0);
	set_sci_style(sci, SCE_MARKDOWN_STRONG1, ft_id, 1);
	set_sci_style(sci, SCE_MARKDOWN_STRONG2, ft_id, 1);
	set_sci_style(sci, SCE_MARKDOWN_EM1, ft_id, 2);
	set_sci_style(sci, SCE_MARKDOWN_EM2, ft_id, 2);
	set_sci_style(sci, SCE_MARKDOWN_HEADER1, ft_id, 3);
	set_sci_style(sci, SCE_MARKDOWN_HEADER2, ft_id, 4);
	set_sci_style(sci, SCE_MARKDOWN_HEADER3, ft_id, 5);
	set_sci_style(sci, SCE_MARKDOWN_HEADER4, ft_id, 6);
	set_sci_style(sci, SCE_MARKDOWN_HEADER5, ft_id, 7);
	set_sci_style(sci, SCE_MARKDOWN_HEADER6, ft_id, 8);
	set_sci_style(sci, SCE_MARKDOWN_ULIST_ITEM, ft_id, 9);
	set_sci_style(sci, SCE_MARKDOWN_OLIST_ITEM, ft_id, 10);
	set_sci_style(sci, SCE_MARKDOWN_BLOCKQUOTE, ft_id, 11);
	set_sci_style(sci, SCE_MARKDOWN_STRIKEOUT, ft_id, 12);
	set_sci_style(sci, SCE_MARKDOWN_HRULE, ft_id, 13);
	set_sci_style(sci, SCE_MARKDOWN_LINK, ft_id, 14);
	set_sci_style(sci, SCE_MARKDOWN_CODE, ft_id, 15);
	set_sci_style(sci, SCE_MARKDOWN_CODE2, ft_id, 15);
	set_sci_style(sci, SCE_MARKDOWN_CODEBK, ft_id, 16);
}


static void styleset_haskell_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 17);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "commentblock", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "commentblock2", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "commentblock3", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "keyword", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "import", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "class", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "instance", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "capital", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "module", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "data", &style_sets[ft_id].styling[16]);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_haskell(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_HASKELL, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_HA_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_HA_COMMENTLINE, ft_id, 1);
	set_sci_style(sci, SCE_HA_COMMENTBLOCK, ft_id, 2);
	set_sci_style(sci, SCE_HA_COMMENTBLOCK2, ft_id, 3);
	set_sci_style(sci, SCE_HA_COMMENTBLOCK3, ft_id, 4);
	set_sci_style(sci, SCE_HA_NUMBER, ft_id, 5);
	set_sci_style(sci, SCE_HA_KEYWORD, ft_id, 6);
	set_sci_style(sci, SCE_HA_IMPORT, ft_id, 7);
	set_sci_style(sci, SCE_HA_STRING, ft_id, 8);
	set_sci_style(sci, SCE_HA_CHARACTER, ft_id, 9);
	set_sci_style(sci, SCE_HA_CLASS, ft_id, 10);
	set_sci_style(sci, SCE_HA_OPERATOR, ft_id, 11);
	set_sci_style(sci, SCE_HA_IDENTIFIER, ft_id, 12);
	set_sci_style(sci, SCE_HA_INSTANCE, ft_id, 13);
	set_sci_style(sci, SCE_HA_CAPITAL, ft_id, 14);
	set_sci_style(sci, SCE_HA_MODULE, ft_id, 15);
	set_sci_style(sci, SCE_HA_DATA, ft_id, 16);
}


static void styleset_caml_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 14);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "comment1", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "comment2", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "comment3", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "keyword", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "keyword2", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "char", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "tagname", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "linenum", &style_sets[ft_id].styling[13]);

	style_sets[ft_id].keywords = g_new(gchar*, 3);
	get_keyfile_keywords(config, config_home, "keywords", ft_id, 0);
	get_keyfile_keywords(config, config_home, "keywords_optional", ft_id, 1);
	style_sets[ft_id].keywords[2] = NULL;
}


static void styleset_caml(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_CAML, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_CAML_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_CAML_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_CAML_COMMENT1, ft_id, 2);
	set_sci_style(sci, SCE_CAML_COMMENT2, ft_id, 3);
	set_sci_style(sci, SCE_CAML_COMMENT3, ft_id, 4);
	set_sci_style(sci, SCE_CAML_NUMBER, ft_id, 5);
	set_sci_style(sci, SCE_CAML_KEYWORD, ft_id, 6);
	set_sci_style(sci, SCE_CAML_KEYWORD2, ft_id, 7);
	set_sci_style(sci, SCE_CAML_STRING, ft_id, 8);
	set_sci_style(sci, SCE_CAML_CHAR, ft_id, 9);
	set_sci_style(sci, SCE_CAML_OPERATOR, ft_id, 10);
	set_sci_style(sci, SCE_CAML_IDENTIFIER, ft_id, 11);
	set_sci_style(sci, SCE_CAML_TAGNAME, ft_id, 12);
	set_sci_style(sci, SCE_CAML_LINENUM, ft_id, 13);
}


static void styleset_tcl_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 16);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "wordinquote", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "inquote", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "substitution", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "modifier", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "expand", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "wordtcl", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "wordtk", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "worditcl", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "wordtkcmds", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "wordexpand", &style_sets[ft_id].styling[15]);

	style_sets[ft_id].keywords = g_new(gchar*, 6);
	get_keyfile_keywords(config, config_home, "tcl", ft_id, 0);
	get_keyfile_keywords(config, config_home, "tk", ft_id, 1);
	get_keyfile_keywords(config, config_home, "itcl", ft_id, 2);
	get_keyfile_keywords(config, config_home, "tkcommands", ft_id, 3);
	get_keyfile_keywords(config, config_home, "expand", ft_id, 4);
	style_sets[ft_id].keywords[5] = NULL;
}


static void styleset_tcl(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_TCL, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);
	sci_set_keywords(sci, 3, style_sets[ft_id].keywords[3]);
	sci_set_keywords(sci, 4, style_sets[ft_id].keywords[4]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_TCL_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_TCL_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_TCL_COMMENTLINE, ft_id, 2);
	set_sci_style(sci, SCE_TCL_NUMBER, ft_id, 3);
	set_sci_style(sci, SCE_TCL_OPERATOR, ft_id, 4);
	set_sci_style(sci, SCE_TCL_IDENTIFIER, ft_id, 5);
	set_sci_style(sci, SCE_TCL_WORD_IN_QUOTE, ft_id, 6);
	set_sci_style(sci, SCE_TCL_IN_QUOTE, ft_id, 7);
	set_sci_style(sci, SCE_TCL_SUBSTITUTION, ft_id, 8);
	set_sci_style(sci, SCE_TCL_MODIFIER, ft_id, 9);
	set_sci_style(sci, SCE_TCL_EXPAND, ft_id, 10);
	set_sci_style(sci, SCE_TCL_WORD, ft_id, 11);
	set_sci_style(sci, SCE_TCL_WORD2, ft_id, 12);
	set_sci_style(sci, SCE_TCL_WORD3, ft_id, 13);
	set_sci_style(sci, SCE_TCL_WORD4, ft_id, 14);
	set_sci_style(sci, SCE_TCL_WORD5, ft_id, 15);
}


static void styleset_matlab_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 9);
	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "command", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "keyword", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "doublequotedstring", &style_sets[ft_id].styling[8]);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_matlab(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_MATLAB, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_MATLAB_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_MATLAB_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_MATLAB_COMMAND, ft_id, 2);
	set_sci_style(sci, SCE_MATLAB_NUMBER, ft_id, 3);
	set_sci_style(sci, SCE_MATLAB_KEYWORD, ft_id, 4);
	set_sci_style(sci, SCE_MATLAB_STRING, ft_id, 5);
	set_sci_style(sci, SCE_MATLAB_OPERATOR, ft_id, 6);
	set_sci_style(sci, SCE_MATLAB_IDENTIFIER, ft_id, 7);
	set_sci_style(sci, SCE_MATLAB_DOUBLEQUOTESTRING, ft_id, 8);
}


static void styleset_d_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 18);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "commentdoc", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "commentdocnested", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "word2", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "word3", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "typedef", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "commentlinedoc", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "commentdockeyword", &style_sets[ft_id].styling[16]);
	get_keyfile_style(config, config_home, "commentdockeyworderror", &style_sets[ft_id].styling[17]);

	style_sets[ft_id].keywords = g_new(gchar*, 5);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "secondary", ft_id, 1);
	get_keyfile_keywords(config, config_home, "docComment", ft_id, 2);
	get_keyfile_keywords(config, config_home, "types", ft_id, 3);
	style_sets[ft_id].keywords[4] = NULL;
}


static void styleset_d(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_D, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);
	sci_set_keywords(sci, 3, style_sets[ft_id].keywords[3]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_D_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_D_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_D_COMMENTLINE, ft_id, 2);
	set_sci_style(sci, SCE_D_COMMENTDOC, ft_id, 3);
	set_sci_style(sci, SCE_D_COMMENTNESTED, ft_id, 4);
	set_sci_style(sci, SCE_D_NUMBER, ft_id, 5);
	set_sci_style(sci, SCE_D_WORD, ft_id, 6);
	set_sci_style(sci, SCE_D_WORD2, ft_id, 7);
	set_sci_style(sci, SCE_D_WORD3, ft_id, 8);
	set_sci_style(sci, SCE_D_TYPEDEF, ft_id, 9);
	set_sci_style(sci, SCE_D_STRING, ft_id, 10);	/* also for other strings below */
	set_sci_style(sci, SCE_D_STRINGEOL, ft_id, 11);
	set_sci_style(sci, SCE_D_CHARACTER, ft_id, 12);
	set_sci_style(sci, SCE_D_OPERATOR, ft_id, 13);
	set_sci_style(sci, SCE_D_IDENTIFIER, ft_id, 14);
	set_sci_style(sci, SCE_D_COMMENTLINEDOC, ft_id, 15);
	set_sci_style(sci, SCE_D_COMMENTDOCKEYWORD, ft_id, 16);
	set_sci_style(sci, SCE_D_COMMENTDOCKEYWORDERROR, ft_id, 17);

	/* copy existing styles */
	set_sci_style(sci, SCE_D_STRINGB, ft_id, 10);	/* `string` */
	set_sci_style(sci, SCE_D_STRINGR, ft_id, 10);	/* r"string" */
}


static void styleset_ferite_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	styleset_c_like_init(config, config_home, ft_id);

	style_sets[ft_id].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "types", ft_id, 1);
	get_keyfile_keywords(config, config_home, "docComment", ft_id, 2);
	style_sets[ft_id].keywords[3] = NULL;
}


static void styleset_ferite(ScintillaObject *sci, gint ft_id)
{
	styleset_c_like(sci, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);
}


static void styleset_vhdl_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 15);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "comment_line_bang", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "keyword", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "stdoperator", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "attribute", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "stdfunction", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "stdpackage", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "stdtype", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "userword", &style_sets[ft_id].styling[14]);

	style_sets[ft_id].keywords = g_new(gchar*, 8);
	get_keyfile_keywords(config, config_home, "keywords", ft_id, 0);
	get_keyfile_keywords(config, config_home, "operators", ft_id, 1);
	get_keyfile_keywords(config, config_home, "attributes", ft_id, 2);
	get_keyfile_keywords(config, config_home, "std_functions", ft_id, 3);
	get_keyfile_keywords(config, config_home, "std_packages", ft_id, 4);
	get_keyfile_keywords(config, config_home, "std_types", ft_id, 5);
	get_keyfile_keywords(config, config_home, "userwords", ft_id, 6);
	style_sets[ft_id].keywords[7] = NULL;
}


static void styleset_vhdl(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_VHDL, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);
	sci_set_keywords(sci, 3, style_sets[ft_id].keywords[3]);
	sci_set_keywords(sci, 4, style_sets[ft_id].keywords[4]);
	sci_set_keywords(sci, 5, style_sets[ft_id].keywords[5]);
	sci_set_keywords(sci, 6, style_sets[ft_id].keywords[6]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_VHDL_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_VHDL_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_VHDL_COMMENTLINEBANG, ft_id, 2);
	set_sci_style(sci, SCE_VHDL_NUMBER, ft_id, 3);
	set_sci_style(sci, SCE_VHDL_STRING, ft_id, 4);
	set_sci_style(sci, SCE_VHDL_OPERATOR, ft_id, 5);
	set_sci_style(sci, SCE_VHDL_IDENTIFIER, ft_id, 6);
	set_sci_style(sci, SCE_VHDL_STRINGEOL, ft_id, 7);
	set_sci_style(sci, SCE_VHDL_KEYWORD, ft_id, 8);
	set_sci_style(sci, SCE_VHDL_STDOPERATOR, ft_id, 9);
	set_sci_style(sci, SCE_VHDL_ATTRIBUTE, ft_id, 10);
	set_sci_style(sci, SCE_VHDL_STDFUNCTION, ft_id, 11);
	set_sci_style(sci, SCE_VHDL_STDPACKAGE, ft_id, 12);
	set_sci_style(sci, SCE_VHDL_STDTYPE, ft_id, 13);
	set_sci_style(sci, SCE_VHDL_USERWORD, ft_id, 14);
}


static void styleset_yaml_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 10);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "keyword", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "reference", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "document", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "text", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "error", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[9]);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_yaml(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_YAML, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_YAML_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_YAML_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_YAML_IDENTIFIER, ft_id, 2);
	set_sci_style(sci, SCE_YAML_KEYWORD, ft_id, 3);
	set_sci_style(sci, SCE_YAML_NUMBER, ft_id, 4);
	set_sci_style(sci, SCE_YAML_REFERENCE, ft_id, 5);
	set_sci_style(sci, SCE_YAML_DOCUMENT, ft_id, 6);
	set_sci_style(sci, SCE_YAML_TEXT, ft_id, 7);
	set_sci_style(sci, SCE_YAML_ERROR, ft_id, 8);
	set_sci_style(sci, SCE_YAML_OPERATOR, ft_id, 9);
}


static void styleset_js_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	styleset_c_like_init(config, config_home, ft_id);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_js(ScintillaObject *sci, gint ft_id)
{
	styleset_c_like(sci, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
}


static void styleset_lua_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 20);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "commentdoc", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "literalstring", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "preprocessor", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "function_basic", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "function_other", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "coroutines", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "word5", &style_sets[ft_id].styling[16]);
	get_keyfile_style(config, config_home, "word6", &style_sets[ft_id].styling[17]);
	get_keyfile_style(config, config_home, "word7", &style_sets[ft_id].styling[18]);
	get_keyfile_style(config, config_home, "word8", &style_sets[ft_id].styling[19]);

	style_sets[ft_id].keywords = g_new(gchar*, 9);
	get_keyfile_keywords(config, config_home, "keywords", ft_id, 0);
	get_keyfile_keywords(config, config_home, "function_basic", ft_id, 1);
	get_keyfile_keywords(config, config_home, "function_other", ft_id, 2);
	get_keyfile_keywords(config, config_home, "coroutines", ft_id, 3);
	get_keyfile_keywords(config, config_home, "user1", ft_id, 4);
	get_keyfile_keywords(config, config_home, "user2", ft_id, 5);
	get_keyfile_keywords(config, config_home, "user3", ft_id, 6);
	get_keyfile_keywords(config, config_home, "user4", ft_id, 7);
	style_sets[ft_id].keywords[8] = NULL;
}


static void styleset_lua(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_LUA, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);
	sci_set_keywords(sci, 3, style_sets[ft_id].keywords[3]);
	sci_set_keywords(sci, 4, style_sets[ft_id].keywords[4]);
	sci_set_keywords(sci, 5, style_sets[ft_id].keywords[5]);
	sci_set_keywords(sci, 6, style_sets[ft_id].keywords[6]);
	sci_set_keywords(sci, 7, style_sets[ft_id].keywords[7]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_LUA_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_LUA_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_LUA_COMMENTLINE, ft_id, 2);
	set_sci_style(sci, SCE_LUA_COMMENTDOC, ft_id, 3);
	set_sci_style(sci, SCE_LUA_NUMBER, ft_id, 4);
	set_sci_style(sci, SCE_LUA_WORD, ft_id, 5);
	set_sci_style(sci, SCE_LUA_STRING, ft_id, 6);
	set_sci_style(sci, SCE_LUA_CHARACTER, ft_id, 7);
	set_sci_style(sci, SCE_LUA_LITERALSTRING, ft_id, 8);
	set_sci_style(sci, SCE_LUA_PREPROCESSOR, ft_id, 9);
	set_sci_style(sci, SCE_LUA_OPERATOR, ft_id, 10);
	set_sci_style(sci, SCE_LUA_IDENTIFIER, ft_id, 11);
	set_sci_style(sci, SCE_LUA_STRINGEOL, ft_id, 12);
	set_sci_style(sci, SCE_LUA_WORD2, ft_id, 13);
	set_sci_style(sci, SCE_LUA_WORD3, ft_id, 14);
	set_sci_style(sci, SCE_LUA_WORD4, ft_id, 15);
	set_sci_style(sci, SCE_LUA_WORD5, ft_id, 16);
	set_sci_style(sci, SCE_LUA_WORD6, ft_id, 17);
	set_sci_style(sci, SCE_LUA_WORD7, ft_id, 18);
	set_sci_style(sci, SCE_LUA_WORD8, ft_id, 19);
}


static void styleset_basic_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 19);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "comment", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "preprocessor", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "operator", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "date", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "word2", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "word3", &style_sets[ft_id].styling[11]);
	get_keyfile_style(config, config_home, "word4", &style_sets[ft_id].styling[12]);
	get_keyfile_style(config, config_home, "constant", &style_sets[ft_id].styling[13]);
	get_keyfile_style(config, config_home, "asm", &style_sets[ft_id].styling[14]);
	get_keyfile_style(config, config_home, "label", &style_sets[ft_id].styling[15]);
	get_keyfile_style(config, config_home, "error", &style_sets[ft_id].styling[16]);
	get_keyfile_style(config, config_home, "hexnumber", &style_sets[ft_id].styling[17]);
	get_keyfile_style(config, config_home, "binnumber", &style_sets[ft_id].styling[18]);

	style_sets[ft_id].keywords = g_new(gchar*, 5);
	get_keyfile_keywords(config, config_home, "keywords", ft_id, 0);
	get_keyfile_keywords(config, config_home, "preprocessor", ft_id, 1);
	get_keyfile_keywords(config, config_home, "user1", ft_id, 2);
	get_keyfile_keywords(config, config_home, "user2", ft_id, 3);
	style_sets[ft_id].keywords[4] = NULL;
}


static void styleset_basic(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_FREEBASIC, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 2, style_sets[ft_id].keywords[2]);
	sci_set_keywords(sci, 3, style_sets[ft_id].keywords[3]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_B_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_B_COMMENT, ft_id, 1);
	set_sci_style(sci, SCE_B_NUMBER, ft_id, 2);
	set_sci_style(sci, SCE_B_KEYWORD, ft_id, 3);
	set_sci_style(sci, SCE_B_STRING, ft_id, 4);
	set_sci_style(sci, SCE_B_PREPROCESSOR, ft_id, 5);
	set_sci_style(sci, SCE_B_OPERATOR, ft_id, 6);
	set_sci_style(sci, SCE_B_IDENTIFIER, ft_id, 7);
	set_sci_style(sci, SCE_B_DATE, ft_id, 8);
	set_sci_style(sci, SCE_B_STRINGEOL, ft_id, 9);
	set_sci_style(sci, SCE_B_KEYWORD2, ft_id, 10);
	set_sci_style(sci, SCE_B_KEYWORD3, ft_id, 11);
	set_sci_style(sci, SCE_B_KEYWORD4, ft_id, 12);
	set_sci_style(sci, SCE_B_CONSTANT, ft_id, 13);
	set_sci_style(sci, SCE_B_ASM, ft_id, 14); /* (still?) unused by the lexer */
	set_sci_style(sci, SCE_B_LABEL, ft_id, 15);
	set_sci_style(sci, SCE_B_ERROR, ft_id, 16);
	set_sci_style(sci, SCE_B_HEXNUMBER, ft_id, 17);
	set_sci_style(sci, SCE_B_BINNUMBER, ft_id, 18);
}


static void styleset_actionscript_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	styleset_c_like_init(config, config_home, ft_id);

	style_sets[ft_id].keywords = g_new(gchar *, 4);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "secondary", ft_id, 1);
	get_keyfile_keywords(config, config_home, "classes", ft_id, 2);
	style_sets[ft_id].keywords[3] = NULL;
}


static void styleset_actionscript(ScintillaObject *sci, gint ft_id)
{
	styleset_c_like(sci, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[2]);
	sci_set_keywords(sci, 3, style_sets[ft_id].keywords[1]);
}


static void styleset_haxe_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	styleset_c_like_init(config, config_home, ft_id);

	style_sets[ft_id].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	get_keyfile_keywords(config, config_home, "secondary", ft_id, 1);
	get_keyfile_keywords(config, config_home, "classes", ft_id, 2);
	style_sets[ft_id].keywords[3] = NULL;
}


static void styleset_haxe(ScintillaObject *sci, gint ft_id)
{
	styleset_c_like(sci, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);
	sci_set_keywords(sci, 1, style_sets[ft_id].keywords[1]);
	sci_set_keywords(sci, 3, style_sets[ft_id].keywords[2]);
}


static void styleset_ada_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_styleset(ft_id, 12);

	get_keyfile_style(config, config_home, "default", &style_sets[ft_id].styling[0]);
	get_keyfile_style(config, config_home, "word", &style_sets[ft_id].styling[1]);
	get_keyfile_style(config, config_home, "identifier", &style_sets[ft_id].styling[2]);
	get_keyfile_style(config, config_home, "number", &style_sets[ft_id].styling[3]);
	get_keyfile_style(config, config_home, "delimiter", &style_sets[ft_id].styling[4]);
	get_keyfile_style(config, config_home, "character", &style_sets[ft_id].styling[5]);
	get_keyfile_style(config, config_home, "charactereol", &style_sets[ft_id].styling[6]);
	get_keyfile_style(config, config_home, "string", &style_sets[ft_id].styling[7]);
	get_keyfile_style(config, config_home, "stringeol", &style_sets[ft_id].styling[8]);
	get_keyfile_style(config, config_home, "label", &style_sets[ft_id].styling[9]);
	get_keyfile_style(config, config_home, "commentline", &style_sets[ft_id].styling[10]);
	get_keyfile_style(config, config_home, "illegal", &style_sets[ft_id].styling[11]);

	style_sets[ft_id].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "primary", ft_id, 0);
	style_sets[ft_id].keywords[1] = NULL;
}


static void styleset_ada(ScintillaObject *sci, gint ft_id)
{
	apply_filetype_properties(sci, SCLEX_ADA, ft_id);

	sci_set_keywords(sci, 0, style_sets[ft_id].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_ADA_DEFAULT, ft_id, 0);
	set_sci_style(sci, SCE_ADA_WORD, ft_id, 1);
	set_sci_style(sci, SCE_ADA_IDENTIFIER, ft_id, 2);
	set_sci_style(sci, SCE_ADA_NUMBER, ft_id, 3);
	set_sci_style(sci, SCE_ADA_DELIMITER, ft_id, 4);
	set_sci_style(sci, SCE_ADA_CHARACTER, ft_id, 5);
	set_sci_style(sci, SCE_ADA_CHARACTEREOL, ft_id, 6);
	set_sci_style(sci, SCE_ADA_STRING, ft_id, 7);
	set_sci_style(sci, SCE_ADA_STRINGEOL, ft_id, 8);
	set_sci_style(sci, SCE_ADA_LABEL, ft_id, 9);
	set_sci_style(sci, SCE_ADA_COMMENTLINE, ft_id, 10);
	set_sci_style(sci, SCE_ADA_ILLEGAL, ft_id, 11);
}


static void get_key_values(GKeyFile *config, const gchar *group, gchar **keys, gchar **values)
{
	while (*keys)
	{
		gchar *str = g_key_file_get_string(config, group, *keys, NULL);

		if (str)
			setptr(*values, str);

		keys++;
		values++;
	}
}


#define foreach_strv(strptr, strv) \
	for (strptr = strv; *strptr; strptr++)

static void read_properties(GeanyFiletype *ft, GKeyFile *config, GKeyFile *configh)
{
	gchar group[] = "lexer_properties";
	gchar **keys = g_key_file_get_keys(config, group, NULL, NULL);
	gchar **keysh = g_key_file_get_keys(configh, group, NULL, NULL);

	/* move/merge keysh into keys */
	if (!keys)
		keys = keysh;
	else if (keysh)
	{
		gchar **strv = g_new0(gchar*, g_strv_length(keys) + g_strv_length(keysh) + 1);
		gchar **read, **write = strv;

		/* may have same key in each, but home will override so it's OK */
		foreach_strv(read, keys)
			*write++ = *read;
		foreach_strv(read, keysh)
			*write++ = *read;
		g_free(keys);
		g_free(keysh);
		keys = strv;
	}

	if (keys)
	{
		gchar **values = g_new0(gchar*, g_strv_length(keys) + 1);

		style_sets[ft->id].property_keys = keys;
		style_sets[ft->id].property_values = values;

		get_key_values(config, group, keys, values);
		get_key_values(configh, group, keys, values);
	}
}


/* lang_name is the name used for the styleset_foo_init function, e.g. foo. */
#define init_styleset_case(ft_id, init_styleset_func) \
	case (ft_id): \
		init_styleset_func(filetype_idx, config, configh); \
		break

/* Called by filetypes_load_config(). */
void highlighting_init_styles(gint filetype_idx, GKeyFile *config, GKeyFile *configh)
{
	GeanyFiletype *ft = filetypes[filetype_idx];
	gint lexer_id = ft->lexer_filetype ? ft->lexer_filetype->id : ft->id;

	if (!style_sets)
		style_sets = g_new0(StyleSet, filetypes_array->len);

	/* Clear old information if necessary - e.g. when reloading config */
	free_styleset(filetype_idx);

	/* All stylesets depend on filetypes.common */
	if (filetype_idx != GEANY_FILETYPES_NONE)
		filetypes_load_config(GEANY_FILETYPES_NONE, FALSE);

	switch (lexer_id)
	{
		init_styleset_case(GEANY_FILETYPES_NONE,	styleset_common_init);
		init_styleset_case(GEANY_FILETYPES_ADA,		styleset_ada_init);
		init_styleset_case(GEANY_FILETYPES_ASM,		styleset_asm_init);
		init_styleset_case(GEANY_FILETYPES_BASIC,	styleset_basic_init);
		init_styleset_case(GEANY_FILETYPES_C,		styleset_c_init);
		init_styleset_case(GEANY_FILETYPES_CAML,	styleset_caml_init);
		init_styleset_case(GEANY_FILETYPES_CMAKE,	styleset_cmake_init);
		init_styleset_case(GEANY_FILETYPES_CONF,	styleset_conf_init);
		init_styleset_case(GEANY_FILETYPES_CSS,		styleset_css_init);
		init_styleset_case(GEANY_FILETYPES_D,		styleset_d_init);
		init_styleset_case(GEANY_FILETYPES_DIFF,	styleset_diff_init);
		init_styleset_case(GEANY_FILETYPES_DOCBOOK,	styleset_docbook_init);
		init_styleset_case(GEANY_FILETYPES_FERITE,	styleset_ferite_init);
		init_styleset_case(GEANY_FILETYPES_F77,		styleset_f77_init);
		init_styleset_case(GEANY_FILETYPES_FORTRAN,	styleset_fortran_init);
		init_styleset_case(GEANY_FILETYPES_HASKELL,	styleset_haskell_init);
		init_styleset_case(GEANY_FILETYPES_HAXE,	styleset_haxe_init);
		init_styleset_case(GEANY_FILETYPES_AS,		styleset_actionscript_init);
		init_styleset_case(GEANY_FILETYPES_HTML,	styleset_html_init);
		init_styleset_case(GEANY_FILETYPES_JAVA,	styleset_java_init);
		init_styleset_case(GEANY_FILETYPES_JS,		styleset_js_init);
		init_styleset_case(GEANY_FILETYPES_LATEX,	styleset_latex_init);
		init_styleset_case(GEANY_FILETYPES_LUA,		styleset_lua_init);
		init_styleset_case(GEANY_FILETYPES_MAKE,	styleset_makefile_init);
		init_styleset_case(GEANY_FILETYPES_MATLAB,	styleset_matlab_init);
		init_styleset_case(GEANY_FILETYPES_MARKDOWN,	styleset_markdown_init);
		init_styleset_case(GEANY_FILETYPES_NSIS,	styleset_nsis_init);
		init_styleset_case(GEANY_FILETYPES_PASCAL,	styleset_pascal_init);
		init_styleset_case(GEANY_FILETYPES_PERL,	styleset_perl_init);
		init_styleset_case(GEANY_FILETYPES_PHP,		styleset_php_init);
		init_styleset_case(GEANY_FILETYPES_PO,		styleset_po_init);
		init_styleset_case(GEANY_FILETYPES_PYTHON,	styleset_python_init);
		init_styleset_case(GEANY_FILETYPES_R,		styleset_r_init);
		init_styleset_case(GEANY_FILETYPES_RUBY,	styleset_ruby_init);
		init_styleset_case(GEANY_FILETYPES_SH,		styleset_sh_init);
		init_styleset_case(GEANY_FILETYPES_SQL,		styleset_sql_init);
		init_styleset_case(GEANY_FILETYPES_TCL,		styleset_tcl_init);
		init_styleset_case(GEANY_FILETYPES_VHDL,	styleset_vhdl_init);
		init_styleset_case(GEANY_FILETYPES_XML,		styleset_markup_init);
		init_styleset_case(GEANY_FILETYPES_YAML,	styleset_yaml_init);
	}
	read_properties(ft, config, configh);

	/* should be done in filetypes.c really: */
	if (filetype_idx != GEANY_FILETYPES_NONE)
		get_keyfile_wordchars(config, configh, &style_sets[filetype_idx].wordchars);
}


/* lang_name is the name used for the styleset_foo function, e.g. foo. */
#define styleset_case(ft_id, styleset_func) \
	case (ft_id): \
		styleset_func(sci, ft->id); \
		break

/** Setup highlighting and other visual settings.
 * @param sci Scintilla widget.
 * @param ft Filetype settings to use. */
void highlighting_set_styles(ScintillaObject *sci, GeanyFiletype *ft)
{
	gint lexer_id = ft->lexer_filetype ? ft->lexer_filetype->id : ft->id;

	filetypes_load_config(ft->id, FALSE);	/* load filetypes.ext */

	switch (lexer_id)
	{
		styleset_case(GEANY_FILETYPES_ADA,		styleset_ada);
		styleset_case(GEANY_FILETYPES_ASM,		styleset_asm);
		styleset_case(GEANY_FILETYPES_BASIC,	styleset_basic);
		styleset_case(GEANY_FILETYPES_C,		styleset_c);
		styleset_case(GEANY_FILETYPES_CAML,		styleset_caml);
		styleset_case(GEANY_FILETYPES_CMAKE,	styleset_cmake);
		styleset_case(GEANY_FILETYPES_CONF,		styleset_conf);
		styleset_case(GEANY_FILETYPES_CSS,		styleset_css);
		styleset_case(GEANY_FILETYPES_D,		styleset_d);
		styleset_case(GEANY_FILETYPES_DIFF,		styleset_diff);
		styleset_case(GEANY_FILETYPES_DOCBOOK,	styleset_docbook);
		styleset_case(GEANY_FILETYPES_FERITE,	styleset_ferite);
		styleset_case(GEANY_FILETYPES_F77,		styleset_f77);
		styleset_case(GEANY_FILETYPES_FORTRAN,	styleset_fortran);
		styleset_case(GEANY_FILETYPES_HASKELL,	styleset_haskell);
		styleset_case(GEANY_FILETYPES_HAXE,		styleset_haxe);
		styleset_case(GEANY_FILETYPES_AS,		styleset_actionscript);
		styleset_case(GEANY_FILETYPES_HTML,		styleset_html);
		styleset_case(GEANY_FILETYPES_JAVA,		styleset_java);
		styleset_case(GEANY_FILETYPES_JS,		styleset_js);
		styleset_case(GEANY_FILETYPES_LATEX,	styleset_latex);
		styleset_case(GEANY_FILETYPES_LUA,		styleset_lua);
		styleset_case(GEANY_FILETYPES_MAKE,		styleset_makefile);
		styleset_case(GEANY_FILETYPES_MARKDOWN,	styleset_markdown);
		styleset_case(GEANY_FILETYPES_MATLAB,	styleset_matlab);
		styleset_case(GEANY_FILETYPES_NSIS,		styleset_nsis);
		styleset_case(GEANY_FILETYPES_PASCAL,	styleset_pascal);
		styleset_case(GEANY_FILETYPES_PERL,		styleset_perl);
		styleset_case(GEANY_FILETYPES_PHP,		styleset_php);
		styleset_case(GEANY_FILETYPES_PO,		styleset_po);
		styleset_case(GEANY_FILETYPES_PYTHON,	styleset_python);
		styleset_case(GEANY_FILETYPES_R,		styleset_r);
		styleset_case(GEANY_FILETYPES_RUBY,		styleset_ruby);
		styleset_case(GEANY_FILETYPES_SH,		styleset_sh);
		styleset_case(GEANY_FILETYPES_SQL,		styleset_sql);
		styleset_case(GEANY_FILETYPES_TCL,		styleset_tcl);
		styleset_case(GEANY_FILETYPES_VHDL,		styleset_vhdl);
		styleset_case(GEANY_FILETYPES_XML,		styleset_xml);
		styleset_case(GEANY_FILETYPES_YAML,		styleset_yaml);
		case GEANY_FILETYPES_NONE:
		default:
			styleset_default(sci, ft->id);
	}
	/* [lexer_properties] settings */
	if (style_sets[ft->id].property_keys)
	{
		gchar **prop = style_sets[ft->id].property_keys;
		gchar **val = style_sets[ft->id].property_values;

		while (*prop)
		{
			sci_set_property(sci, *prop, *val);
			prop++;
			val++;
		}
	}
}


/** Retrieve a style @a style_id for the filetype @a ft_id.
 * If the style was not already initialised
 * (e.g. by by opening a file of this type), it will be initialised. The returned pointer is
 * owned by Geany and must not be freed.
 * @param ft_id Filetype ID, e.g. @c GEANY_FILETYPES_DIFF.
 * @param style_id A Scintilla lexer style, e.g. @c SCE_DIFF_ADDED. See scintilla/include/SciLexer.h.
 * @return A pointer to the style struct.
 * @see Scintilla messages @c SCI_STYLEGETFORE, etc, for use with ScintillaFuncs::send_message(). */
const GeanyLexerStyle *highlighting_get_style(gint ft_id, gint style_id)
{
	if (ft_id < 0 || ft_id >= (gint)filetypes_array->len)
		return NULL;

	/* ensure filetype loaded */
	filetypes_load_config(ft_id, FALSE);

	/* TODO: style_id might not be the real array index (Scintilla styles are not always synced
	 * with array indices) */
	return get_style(ft_id, style_id);
}


static void
on_color_scheme_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
	gchar *fname;
	gchar *path;

	/* check if default item */
	if (user_data)
	{
		setptr(editor_prefs.color_scheme, NULL);
		filetypes_reload();
		return;
	}
	fname = ui_menu_item_get_text(menuitem);
	setptr(fname, utils_get_locale_from_utf8(fname));

	/* fname is just the basename from the menu item, so prepend the custom files path */
	path = g_build_path(G_DIR_SEPARATOR_S, app->configdir, GEANY_COLORSCHEMES_SUBDIR, fname, NULL);
	if (!g_file_test(path, G_FILE_TEST_EXISTS))
	{
		/* try the system path */
		g_free(path);
		path = g_build_path(G_DIR_SEPARATOR_S, app->datadir, GEANY_COLORSCHEMES_SUBDIR, fname, NULL);
	}
	if (g_file_test(path, G_FILE_TEST_EXISTS))
	{
		setptr(editor_prefs.color_scheme, fname);
		fname = NULL;
		filetypes_reload();
	}
	else
	{
		setptr(fname, utils_get_utf8_from_locale(fname));
		ui_set_statusbar(TRUE, _("Could not find file '%s'."), fname);
	}
	g_free(path);
	g_free(fname);
}


static void add_color_scheme_item(const gchar *fname, GtkWidget *menu)
{
	GtkWidget *tmp_button;
	gchar *label;

	g_return_if_fail(fname);
	g_return_if_fail(menu);

	label = utils_get_utf8_from_locale(fname);

	tmp_button = gtk_menu_item_new_with_label(label);
	gtk_widget_show(tmp_button);
	gtk_container_add(GTK_CONTAINER(menu), tmp_button);
	g_signal_connect(tmp_button, "activate", G_CALLBACK(on_color_scheme_clicked), NULL);

	g_free(label);
}


static gboolean add_color_scheme_items(GtkWidget *menu)
{
	GSList *list = utils_get_config_files(GEANY_COLORSCHEMES_SUBDIR);
	GSList *node;

	foreach_slist(node, list)
	{
		gchar *fname = node->data;

		if (g_str_has_suffix(fname, ".conf"))
			add_color_scheme_item(fname, menu);
		g_free(fname);
	}
	g_slist_free(list);
	return list != NULL;
}


static void create_color_scheme_menu(void)
{
	GtkWidget *item, *menu, *root;

	menu = ui_lookup_widget(main_widgets.window, "menu_view_editor1_menu");
	item = ui_image_menu_item_new(GTK_STOCK_SELECT_COLOR, _("_Color Schemes"));
	gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), item);
	root = item;

	menu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);

	item = gtk_menu_item_new_with_mnemonic(_("_Default"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_color_scheme_clicked), GINT_TO_POINTER(TRUE));

	/* for now we don't show the color scheme menu unless there are files */
	if (add_color_scheme_items(menu))
		gtk_widget_show_all(root);
}


void highlighting_init(void)
{
	create_color_scheme_menu();
}


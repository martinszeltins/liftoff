/* liftoff-window.c
 *
 * Copyright 2025 martins
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include "liftoff-window.h"
#include <glib/gi18n.h>
#include <sys/stat.h>

struct _LiftoffWindow
{
	AdwApplicationWindow  parent_instance;

	/* Template widgets */
	GtkEntry            *name_entry;
	GtkEntry            *exec_entry;
	GtkEntry            *icon_entry;
	GtkEntry            *categories_entry;
	GtkButton           *icon_button;
	GtkButton           *create_button;
	GtkTextView         *preview_text;

	char                *executable_path;
};

G_DEFINE_FINAL_TYPE (LiftoffWindow, liftoff_window, ADW_TYPE_APPLICATION_WINDOW)

enum {
	PROP_0,
	PROP_EXECUTABLE_PATH,
	N_PROPS
};

static GParamSpec *properties [N_PROPS];

/* Forward declarations */
static void on_icon_selected (GObject      *source,
                               GAsyncResult *result,
                               gpointer      user_data);

static void
update_preview (LiftoffWindow *self)
{
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	const char *name, *exec, *icon, *categories;
	GtkTextTag *header_tag, *key_tag, *value_tag;

	buffer = gtk_text_view_get_buffer (self->preview_text);
	gtk_text_buffer_set_text (buffer, "", -1);

	/* Create text tags for syntax highlighting */
	header_tag = gtk_text_buffer_create_tag (buffer, NULL,
	                                          "foreground", "#1c71d8",
	                                          "weight", PANGO_WEIGHT_BOLD,
	                                          NULL);
	key_tag = gtk_text_buffer_create_tag (buffer, NULL,
	                                       "foreground", "#c061cb",
	                                       "weight", PANGO_WEIGHT_SEMIBOLD,
	                                       NULL);
	value_tag = gtk_text_buffer_create_tag (buffer, NULL,
	                                         "foreground", "#26a269",
	                                         NULL);

	name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
	exec = gtk_editable_get_text (GTK_EDITABLE (self->exec_entry));
	icon = gtk_editable_get_text (GTK_EDITABLE (self->icon_entry));
	categories = gtk_editable_get_text (GTK_EDITABLE (self->categories_entry));

	/* Insert [Desktop Entry] with header tag */
	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_buffer_insert_with_tags (buffer, &iter, "[Desktop Entry]\n", -1, header_tag, NULL);

	/* Helper macro to insert key=value lines */
	#define INSERT_LINE(key, val) do { \
		gtk_text_buffer_get_end_iter (buffer, &iter); \
		gtk_text_buffer_insert_with_tags (buffer, &iter, key, -1, key_tag, NULL); \
		gtk_text_buffer_get_end_iter (buffer, &iter); \
		gtk_text_buffer_insert (buffer, &iter, "=", -1); \
		if (val && *val) { \
			gtk_text_buffer_get_end_iter (buffer, &iter); \
			gtk_text_buffer_insert_with_tags (buffer, &iter, val, -1, value_tag, NULL); \
		} \
		gtk_text_buffer_get_end_iter (buffer, &iter); \
		gtk_text_buffer_insert (buffer, &iter, "\n", -1); \
	} while(0)

	INSERT_LINE("Type", "Application");
	INSERT_LINE("Version", "1.0");
	INSERT_LINE("Name", name ? name : "");
	INSERT_LINE("Exec", exec ? exec : "");
	
	if (icon && *icon)
		INSERT_LINE("Icon", icon);
	
	if (categories && *categories)
		INSERT_LINE("Categories", categories);
	
	INSERT_LINE("Terminal", "false");

	#undef INSERT_LINE
}

static void
on_entry_changed (GtkEditable *editable,
                  gpointer     user_data)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (user_data);
	update_preview (self);
}

static char *
detect_interpreter (const char *path)
{
	if (g_str_has_suffix (path, ".sh"))
		return g_strdup ("bash");
	else if (g_str_has_suffix (path, ".js"))
		return g_strdup ("node");
	else if (g_str_has_suffix (path, ".py"))
		return g_strdup ("python3");
	else if (g_str_has_suffix (path, ".rb"))
		return g_strdup ("ruby");
	else if (g_str_has_suffix (path, ".pl"))
		return g_strdup ("perl");
	
	return NULL;
}

static void
setup_exec_field (LiftoffWindow *self)
{
	char *interpreter;
	char *exec_string;
	char *basename;
	char *dot;

	if (self->executable_path)
	{
		interpreter = detect_interpreter (self->executable_path);
		
		if (interpreter)
		{
			exec_string = g_strdup_printf ("%s %s", interpreter, self->executable_path);
			gtk_editable_set_text (GTK_EDITABLE (self->exec_entry), exec_string);
			g_free (exec_string);
			g_free (interpreter);
		}
		else
		{
			gtk_editable_set_text (GTK_EDITABLE (self->exec_entry), self->executable_path);
		}

		/* Auto-fill name from filename */
		basename = g_path_get_basename (self->executable_path);
		dot = strrchr (basename, '.');
		if (dot)
			*dot = '\0';
		
		/* Capitalize first letter */
		if (basename[0])
			basename[0] = g_ascii_toupper (basename[0]);

		gtk_editable_set_text (GTK_EDITABLE (self->name_entry), basename);
		g_free (basename);
	}
}

static void
on_icon_button_clicked (GtkButton *button,
                        gpointer   user_data)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (user_data);
	GtkFileDialog *dialog;
	GtkFileFilter *filter;
	GListStore *filters;

	dialog = gtk_file_dialog_new ();
	gtk_file_dialog_set_title (dialog, _("Select Icon"));

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Image Files"));
	gtk_file_filter_add_mime_type (filter, "image/png");
	gtk_file_filter_add_mime_type (filter, "image/svg+xml");
	gtk_file_filter_add_mime_type (filter, "image/jpeg");

	filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
	g_list_store_append (filters, filter);
	gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
	g_object_unref (filters);
	g_object_unref (filter);

	gtk_file_dialog_open (dialog,
	                      GTK_WINDOW (self),
	                      NULL,
	                      (GAsyncReadyCallback) on_icon_selected,
	                      self);
}

static void
on_icon_selected (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (user_data);
	GtkFileDialog *dialog = GTK_FILE_DIALOG (source);
	GFile *file;
	GError *error = NULL;

	file = gtk_file_dialog_open_finish (dialog, result, &error);
	
	if (file)
	{
		char *path = g_file_get_path (file);
		gtk_editable_set_text (GTK_EDITABLE (self->icon_entry), path);
		g_free (path);
		g_object_unref (file);
	}
	else if (error && !g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
	{
		g_warning ("Failed to open file: %s", error->message);
	}
	
	if (error)
		g_error_free (error);
}

static void
on_create_button_clicked (GtkButton *button,
                          gpointer   user_data)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (user_data);
	const char *name, *exec, *icon, *categories;
	GString *content;
	char *desktop_dir, *filename, *filepath;
	char *sanitized_name;
	char *msg;
	AdwDialog *alert_dialog;
	GError *error = NULL;
	int i;

	name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
	exec = gtk_editable_get_text (GTK_EDITABLE (self->exec_entry));
	icon = gtk_editable_get_text (GTK_EDITABLE (self->icon_entry));
	categories = gtk_editable_get_text (GTK_EDITABLE (self->categories_entry));

	if (!name || !*name)
	{
		alert_dialog = adw_alert_dialog_new (_("Error"), _("Name is required"));
		adw_alert_dialog_add_response (ADW_ALERT_DIALOG (alert_dialog), "ok", _("OK"));
		adw_dialog_present (alert_dialog, GTK_WIDGET (self));
		return;
	}

	if (!exec || !*exec)
	{
		alert_dialog = adw_alert_dialog_new (_("Error"), _("Exec command is required"));
		adw_alert_dialog_add_response (ADW_ALERT_DIALOG (alert_dialog), "ok", _("OK"));
		adw_dialog_present (alert_dialog, GTK_WIDGET (self));
		return;
	}

	content = g_string_new ("");
	g_string_append (content, "[Desktop Entry]\n");
	g_string_append (content, "Type=Application\n");
	g_string_append (content, "Version=1.0\n");
	g_string_append_printf (content, "Name=%s\n", name);
	g_string_append_printf (content, "Exec=%s\n", exec);
	
	if (icon && *icon)
		g_string_append_printf (content, "Icon=%s\n", icon);
	
	if (categories && *categories)
		g_string_append_printf (content, "Categories=%s\n", categories);
	
	g_string_append (content, "Terminal=false\n");

	/* Save to ~/.local/share/applications/ */
	desktop_dir = g_build_filename (g_get_user_data_dir (), "applications", NULL);
	g_mkdir_with_parents (desktop_dir, 0755);

	/* Create filename from name */
	sanitized_name = g_strdup (name);
	for (i = 0; sanitized_name[i]; i++)
	{
		if (!g_ascii_isalnum (sanitized_name[i]) && sanitized_name[i] != '-')
			sanitized_name[i] = '-';
	}

	filename = g_strdup_printf ("%s.desktop", sanitized_name);
	filepath = g_build_filename (desktop_dir, filename, NULL);

	if (g_file_set_contents (filepath, content->str, -1, &error))
	{
		/* Make it executable */
		chmod (filepath, 0755);

		msg = g_strdup_printf (_("Desktop file created successfully at:\n%s"), filepath);
		alert_dialog = adw_alert_dialog_new (_("Success"), msg);
		adw_alert_dialog_add_response (ADW_ALERT_DIALOG (alert_dialog), "ok", _("OK"));
		adw_dialog_present (alert_dialog, GTK_WIDGET (self));
		g_free (msg);
	}
	else
	{
		msg = g_strdup_printf (_("Failed to create desktop file: %s"), error->message);
		alert_dialog = adw_alert_dialog_new (_("Error"), msg);
		adw_alert_dialog_add_response (ADW_ALERT_DIALOG (alert_dialog), "ok", _("OK"));
		adw_dialog_present (alert_dialog, GTK_WIDGET (self));
		g_free (msg);
		g_error_free (error);
	}

	g_string_free (content, TRUE);
	g_free (desktop_dir);
	g_free (filename);
	g_free (filepath);
	g_free (sanitized_name);
}

LiftoffWindow *
liftoff_window_new (GtkApplication *application,
                    const char     *executable_path)
{
	return g_object_new (LIFTOFF_TYPE_WINDOW,
	                     "application", application,
	                     "executable-path", executable_path,
	                     NULL);
}

static void
liftoff_window_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (object);

	switch (prop_id)
	{
	case PROP_EXECUTABLE_PATH:
		g_value_set_string (value, self->executable_path);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
liftoff_window_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (object);

	switch (prop_id)
	{
	case PROP_EXECUTABLE_PATH:
		g_free (self->executable_path);
		self->executable_path = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
liftoff_window_finalize (GObject *object)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (object);

	g_free (self->executable_path);

	G_OBJECT_CLASS (liftoff_window_parent_class)->finalize (object);
}

static void
liftoff_window_constructed (GObject *object)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (object);

	G_OBJECT_CLASS (liftoff_window_parent_class)->constructed (object);

	/* Setup initial state after properties are set */
	setup_exec_field (self);
	update_preview (self);
}

static void
liftoff_window_class_init (LiftoffWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = liftoff_window_get_property;
	object_class->set_property = liftoff_window_set_property;
	object_class->finalize = liftoff_window_finalize;
	object_class->constructed = liftoff_window_constructed;

	properties [PROP_EXECUTABLE_PATH] =
		g_param_spec_string ("executable-path",
		                     "Executable Path",
		                     "The path to the executable file",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	gtk_widget_class_set_template_from_resource (widget_class, "/lv/martinsz/Liftoff/liftoff-window.ui");
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, name_entry);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, exec_entry);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, icon_entry);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, categories_entry);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, icon_button);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, create_button);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, preview_text);

	gtk_widget_class_bind_template_callback (widget_class, on_entry_changed);
	gtk_widget_class_bind_template_callback (widget_class, on_icon_button_clicked);
	gtk_widget_class_bind_template_callback (widget_class, on_create_button_clicked);
}

static void
liftoff_window_init (LiftoffWindow *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

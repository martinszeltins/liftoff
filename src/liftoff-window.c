#include "config.h"
#include "liftoff-window.h"
#include "liftoff-appimage.h"
#include <glib/gi18n.h>
#include <sys/stat.h>

struct _LiftoffWindow
{
	AdwApplicationWindow  parent_instance;

	/* Template widgets */
	GtkEntry            *name_entry;
	GtkEntry            *exec_entry;
	GtkEntry            *icon_entry;
	GtkImage            *icon_preview;
	GtkEntry            *categories_entry;
	GtkEntry            *wmclass_entry;
	GtkButton           *icon_button;
	GtkButton           *create_button;
	GtkTextView         *preview_text;
	AdwSwitchRow        *startup_switch;

	char                *executable_path;
	char                *icon_source;
	GCancellable        *icon_cancellable;
	guint                extract_generation;
	gboolean             wmclass_user_edited;
	gboolean             icon_user_set;
	gboolean             setting_icon;
	gboolean             closing;
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
static void liftoff_window_maybe_extract_icon (LiftoffWindow *self);

static void
update_preview (LiftoffWindow *self)
{
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	const char *name, *exec, *icon, *categories, *wmclass;
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
	wmclass = gtk_editable_get_text (GTK_EDITABLE (self->wmclass_entry));

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
	
	if (wmclass && *wmclass)
		INSERT_LINE("StartupWMClass", wmclass);
	
	INSERT_LINE("Terminal", "false");

	#undef INSERT_LINE
}

static void
update_icon_preview (LiftoffWindow *self)
{
	const char *path;
	g_autoptr(GError) error = NULL;
	g_autoptr(GdkTexture) texture = NULL;

	path = gtk_editable_get_text (GTK_EDITABLE (self->icon_entry));
	if (path != NULL && path[0] != '\0' && g_file_test (path, G_FILE_TEST_IS_REGULAR))
		texture = gdk_texture_new_from_filename (path, &error);

	if (texture != NULL)
	{
		gtk_image_set_from_paintable (self->icon_preview, GDK_PAINTABLE (texture));
		gtk_widget_set_visible (GTK_WIDGET (self->icon_preview), TRUE);
	}
	else
	{
		gtk_image_clear (self->icon_preview);
		gtk_widget_set_visible (GTK_WIDGET (self->icon_preview), FALSE);
	}
}

static void
on_entry_changed (GtkEditable *editable,
                  gpointer     user_data)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (user_data);
	
	/* If name changed and wmclass hasn't been manually edited, update it */
	if (GTK_EDITABLE (editable) == GTK_EDITABLE (self->name_entry) && !self->wmclass_user_edited)
	{
		const char *name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
		gtk_editable_set_text (GTK_EDITABLE (self->wmclass_entry), name);
	}
	
	/* Mark wmclass as user-edited if they change it directly */
	if (GTK_EDITABLE (editable) == GTK_EDITABLE (self->wmclass_entry))
	{
		self->wmclass_user_edited = TRUE;
	}

	if (GTK_EDITABLE (editable) == GTK_EDITABLE (self->icon_entry))
	{
		if (!self->setting_icon)
			self->icon_user_set = TRUE;
		update_icon_preview (self);
	}

	if (GTK_EDITABLE (editable) == GTK_EDITABLE (self->exec_entry))
		liftoff_window_maybe_extract_icon (self);
	
	update_preview (self);
}

static char *
detect_interpreter (const char *path)
{
	/* Check if path is a directory */
	if (g_file_test (path, G_FILE_TEST_IS_DIR))
		return g_strdup ("nautilus");
	
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

typedef struct
{
	char *path;
	guint generation;
} IconExtractData;

static void
icon_extract_data_free (gpointer data)
{
	IconExtractData *info = data;

	g_free (info->path);
	g_free (info);
}

static void
liftoff_window_cancel_icon_extract (LiftoffWindow *self)
{
	self->extract_generation++;
	if (self->icon_cancellable != NULL)
		g_cancellable_cancel (self->icon_cancellable);
}

static char *
command_to_path (const char *command)
{
	g_autofree char *copy = NULL;
	char *end;
	char *space;
	char quote;

	if (command == NULL)
		return NULL;

	copy = g_strdup (command);
	g_strstrip (copy);
	if (copy[0] == '\0')
		return NULL;

	if (g_file_test (copy, G_FILE_TEST_IS_REGULAR))
		return g_steal_pointer (&copy);

	quote = copy[0];
	if (quote == '"' || quote == '\'')
	{
		end = strchr (copy + 1, quote);
		if (end != NULL)
		{
			g_autofree char *quoted = NULL;

			quoted = g_strndup (copy + 1, (gsize) (end - (copy + 1)));
			if (g_file_test (quoted, G_FILE_TEST_IS_REGULAR))
				return g_steal_pointer (&quoted);
		}

		return NULL;
	}

	space = strchr (copy, ' ');
	if (space != NULL)
	{
		g_autofree char *first = NULL;

		first = g_strndup (copy, (gsize) (space - copy));
		if (g_file_test (first, G_FILE_TEST_IS_REGULAR))
			return g_steal_pointer (&first);
	}

	return NULL;
}

static void
extract_icon_thread (GTask        *task,
                     gpointer      source_object,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
	IconExtractData *info = task_data;
	g_autoptr(GError) error = NULL;
	g_autofree char *data_dir = NULL;
	g_autofree char *dest_dir = NULL;
	char *icon;

	(void) source_object;

	data_dir = liftoff_host_data_dir ();
	dest_dir = g_build_filename (data_dir, "liftoff", "icons", NULL);
	icon = liftoff_appimage_extract_icon (info->path, dest_dir, cancellable, &error);
	if (icon == NULL)
	{
		if (error == NULL)
			g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not extract icon");
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	g_task_return_pointer (task, icon, g_free);
}

static void
on_icon_extract_done (GObject      *source,
                      GAsyncResult *result,
                      gpointer      user_data)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (source);
	IconExtractData *info = g_task_get_task_data (G_TASK (result));
	g_autoptr(GError) error = NULL;
	g_autofree char *icon_path = NULL;

	(void) user_data;

	icon_path = g_task_propagate_pointer (G_TASK (result), &error);

	if (self->closing || info->generation != self->extract_generation || self->icon_user_set)
		return;

	if (icon_path == NULL)
	{
		if (error != NULL &&
		    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
		    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
		    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
			g_warning ("Could not extract AppImage icon: %s", error->message);
		return;
	}

	self->setting_icon = TRUE;
	gtk_editable_set_text (GTK_EDITABLE (self->icon_entry), icon_path);
	self->setting_icon = FALSE;
}

static void
liftoff_window_extract_icon_async (LiftoffWindow *self,
                                   const char    *path)
{
	IconExtractData *info;
	GTask *task;

	liftoff_window_cancel_icon_extract (self);
	g_clear_object (&self->icon_cancellable);
	self->icon_cancellable = g_cancellable_new ();

	info = g_new0 (IconExtractData, 1);
	info->path = g_strdup (path);
	info->generation = self->extract_generation;

	task = g_task_new (self, self->icon_cancellable, on_icon_extract_done, NULL);
	g_task_set_task_data (task, info, icon_extract_data_free);
	g_task_run_in_thread (task, extract_icon_thread);
	g_object_unref (task);
}

static void
liftoff_window_maybe_extract_icon (LiftoffWindow *self)
{
	const char *command;
	g_autofree char *path = NULL;

	if (self->icon_user_set || self->closing)
		return;

	command = gtk_editable_get_text (GTK_EDITABLE (self->exec_entry));
	path = command_to_path (command);
	if (path == NULL)
		return;

	if (g_strcmp0 (path, self->icon_source) == 0)
		return;

	g_free (self->icon_source);
	self->icon_source = g_strdup (path);

	if (!liftoff_path_is_appimage (path))
	{
		liftoff_window_cancel_icon_extract (self);
		return;
	}

	liftoff_window_extract_icon_async (self, path);
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
	const char *name, *exec, *icon, *categories, *wmclass;
	GString *content;
	char *desktop_dir, *filename, *filepath, *autostart_dir, *autostart_path;
	char *sanitized_name;
	char *msg;
	char *data_home = NULL;
	char *config_home = NULL;
	AdwDialog *alert_dialog;
	GError *error = NULL;
	gboolean run_on_startup;
	int i;

	name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
	exec = gtk_editable_get_text (GTK_EDITABLE (self->exec_entry));
	icon = gtk_editable_get_text (GTK_EDITABLE (self->icon_entry));
	categories = gtk_editable_get_text (GTK_EDITABLE (self->categories_entry));
	wmclass = gtk_editable_get_text (GTK_EDITABLE (self->wmclass_entry));
	run_on_startup = adw_switch_row_get_active (self->startup_switch);

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
	
	if (wmclass && *wmclass)
		g_string_append_printf (content, "StartupWMClass=%s\n", wmclass);
	
	g_string_append (content, "Terminal=false\n");

	/* Host paths stay visible to the desktop outside the Flatpak sandbox. */
	data_home = liftoff_host_data_dir ();
	desktop_dir = g_build_filename (data_home, "applications", NULL);
	g_mkdir_with_parents (desktop_dir, 0755);

	/* Create filename from wmclass if provided, otherwise from name */
	if (wmclass && *wmclass)
	{
		sanitized_name = g_strdup (wmclass);
	}
	else
	{
		sanitized_name = g_strdup (name);
	}
	
	for (i = 0; sanitized_name[i]; i++)
	{
		if (!g_ascii_isalnum (sanitized_name[i]) && sanitized_name[i] != '-')
			sanitized_name[i] = '-';
	}

	filename = g_strdup_printf ("%s.desktop", sanitized_name);
	filepath = g_build_filename (desktop_dir, filename, NULL);

	if (g_file_set_contents (filepath, content->str, -1, &error))
	{
		GtkApplication *app;
		
		/* Make it executable */
		chmod (filepath, 0755);

		/* If run on startup is enabled, also create autostart file */
		if (run_on_startup)
		{
			config_home = liftoff_host_config_dir ();
			autostart_dir = g_build_filename (config_home, "autostart", NULL);
			g_mkdir_with_parents (autostart_dir, 0755);
			autostart_path = g_build_filename (autostart_dir, filename, NULL);
			
			g_file_set_contents (autostart_path, content->str, -1, NULL);
			chmod (autostart_path, 0755);
			
			g_free (autostart_dir);
			g_free (autostart_path);
		}

		/* Exit the application on success */
		app = GTK_APPLICATION (gtk_window_get_application (GTK_WINDOW (self)));
		g_application_quit (G_APPLICATION (app));
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
	g_free (data_home);
	g_free (config_home);
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
liftoff_window_dispose (GObject *object)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (object);

	self->closing = TRUE;
	if (self->icon_cancellable != NULL)
		g_cancellable_cancel (self->icon_cancellable);
	g_clear_object (&self->icon_cancellable);

	G_OBJECT_CLASS (liftoff_window_parent_class)->dispose (object);
}

static void
liftoff_window_finalize (GObject *object)
{
	LiftoffWindow *self = LIFTOFF_WINDOW (object);

	g_free (self->executable_path);
	g_free (self->icon_source);

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
	object_class->dispose = liftoff_window_dispose;
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
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, icon_preview);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, categories_entry);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, wmclass_entry);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, icon_button);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, create_button);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, preview_text);
	gtk_widget_class_bind_template_child (widget_class, LiftoffWindow, startup_switch);

	gtk_widget_class_bind_template_callback (widget_class, on_entry_changed);
	gtk_widget_class_bind_template_callback (widget_class, on_icon_button_clicked);
	gtk_widget_class_bind_template_callback (widget_class, on_create_button_clicked);
}

static void
liftoff_window_init (LiftoffWindow *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
	
	/* Initialize wmclass_user_edited flag */
	self->wmclass_user_edited = FALSE;
}

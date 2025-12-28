#include "config.h"
#include <glib/gi18n.h>
#include "liftoff-application.h"
#include "liftoff-window.h"

struct _LiftoffApplication
{
	AdwApplication parent_instance;
	char *executable_path;
};

G_DEFINE_FINAL_TYPE (LiftoffApplication, liftoff_application, ADW_TYPE_APPLICATION)

LiftoffApplication *
liftoff_application_new (const char        *application_id,
                         GApplicationFlags  flags)
{
	g_return_val_if_fail (application_id != NULL, NULL);

	return g_object_new (LIFTOFF_TYPE_APPLICATION,
	                     "application-id", application_id,
	                     "flags", flags | G_APPLICATION_HANDLES_COMMAND_LINE,
	                     "resource-base-path", "/lv/martinsz/Liftoff",
	                     NULL);
}

static void
liftoff_application_activate (GApplication *app)
{
	LiftoffApplication *self = LIFTOFF_APPLICATION (app);
	GtkWindow *window;

	g_assert (LIFTOFF_IS_APPLICATION (app));

	window = gtk_application_get_active_window (GTK_APPLICATION (app));

	if (window == NULL)
		window = g_object_new (LIFTOFF_TYPE_WINDOW,
		                       "application", app,
		                       "executable-path", self->executable_path,
		                       NULL);

	gtk_window_present (window);
}

static int
liftoff_application_command_line (GApplication            *app,
                                   GApplicationCommandLine *cmdline)
{
	LiftoffApplication *self = LIFTOFF_APPLICATION (app);
	gchar **argv;
	gint argc;

	argv = g_application_command_line_get_arguments (cmdline, &argc);

	if (argc > 1)
	{
		g_free (self->executable_path);
		self->executable_path = g_strdup (argv[1]);
	}

	g_strfreev (argv);

	g_application_activate (app);

	return 0;
}

static void
liftoff_application_finalize (GObject *object)
{
	LiftoffApplication *self = LIFTOFF_APPLICATION (object);

	g_free (self->executable_path);

	G_OBJECT_CLASS (liftoff_application_parent_class)->finalize (object);
}

static void
liftoff_application_class_init (LiftoffApplicationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

	object_class->finalize = liftoff_application_finalize;

	app_class->activate = liftoff_application_activate;
	app_class->command_line = liftoff_application_command_line;
}

static void
liftoff_application_about_action (GSimpleAction *action,
                                  GVariant      *parameter,
                                  gpointer       user_data)
{
	static const char *developers[] = {"martins", NULL};
	LiftoffApplication *self = user_data;
	GtkWindow *window = NULL;

	g_assert (LIFTOFF_IS_APPLICATION (self));

	window = gtk_application_get_active_window (GTK_APPLICATION (self));

	adw_show_about_dialog (GTK_WIDGET (window),
	                       "application-name", "liftoff",
	                       "application-icon", "lv.martinsz.Liftoff",
	                       "developer-name", "martins",
	                       "translator-credits", _("translator-credits"),
	                       "version", "0.1.0",
	                       "developers", developers,
	                       "copyright", "© 2025 martins",
	                       NULL);
}

static void
liftoff_application_quit_action (GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
	LiftoffApplication *self = user_data;

	g_assert (LIFTOFF_IS_APPLICATION (self));

	g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
	{ "quit", liftoff_application_quit_action },
	{ "about", liftoff_application_about_action },
};

static void
liftoff_application_init (LiftoffApplication *self)
{
	g_action_map_add_action_entries (G_ACTION_MAP (self),
	                                 app_actions,
	                                 G_N_ELEMENTS (app_actions),
	                                 self);
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.quit",
	                                       (const char *[]) { "<control>q", NULL });
}

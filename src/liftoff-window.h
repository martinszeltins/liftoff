#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define LIFTOFF_TYPE_WINDOW (liftoff_window_get_type())

G_DECLARE_FINAL_TYPE (LiftoffWindow, liftoff_window, LIFTOFF, WINDOW, AdwApplicationWindow)

LiftoffWindow *liftoff_window_new (GtkApplication *application,
                                    const char     *executable_path);

G_END_DECLS

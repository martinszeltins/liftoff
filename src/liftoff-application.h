#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define LIFTOFF_TYPE_APPLICATION (liftoff_application_get_type())

G_DECLARE_FINAL_TYPE (LiftoffApplication, liftoff_application, LIFTOFF, APPLICATION, AdwApplication)

LiftoffApplication *liftoff_application_new (const char        *application_id,
                                             GApplicationFlags  flags);

G_END_DECLS

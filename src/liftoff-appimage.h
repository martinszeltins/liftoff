#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean liftoff_path_is_appimage (const char *path);

char *liftoff_host_data_dir (void);
char *liftoff_host_config_dir (void);

char *liftoff_appimage_extract_icon (const char   *appimage_path,
                                     const char   *dest_dir,
                                     GCancellable *cancellable,
                                     GError      **error);

G_END_DECLS

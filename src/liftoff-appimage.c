#include "liftoff-appimage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib/gstdio.h>

/* Read this far past the ELF for the SquashFS superblock. */
#define LIFTOFF_SQUASHFS_SCAN_BYTES (4 * 1024 * 1024)
#define LIFTOFF_ICON_MAX_BYTES (20 * 1024 * 1024)
#define LIFTOFF_MAX_SYMLINKS 8

static guint16
read_le16 (const guint8 *p)
{
	return (guint16) p[0] | ((guint16) p[1] << 8);
}

static guint64
read_le64 (const guint8 *p)
{
	return (guint64) p[0]
		| ((guint64) p[1] << 8)
		| ((guint64) p[2] << 16)
		| ((guint64) p[3] << 24)
		| ((guint64) p[4] << 32)
		| ((guint64) p[5] << 40)
		| ((guint64) p[6] << 48)
		| ((guint64) p[7] << 56);
}

static char *
host_xdg_dir (const char *env_name,
              const char *fallback_a,
              const char *fallback_b)
{
	const char *host;

	if (g_getenv ("FLATPAK_ID") != NULL)
	{
		host = g_getenv (env_name);
		if (host != NULL && host[0] != '\0')
			return g_strdup (host);

		return g_build_filename (g_get_home_dir (), fallback_a, fallback_b, NULL);
	}

	if (g_strcmp0 (env_name, "HOST_XDG_DATA_HOME") == 0)
		return g_strdup (g_get_user_data_dir ());

	return g_strdup (g_get_user_config_dir ());
}

char *
liftoff_host_data_dir (void)
{
	return host_xdg_dir ("HOST_XDG_DATA_HOME", ".local", "share");
}

char *
liftoff_host_config_dir (void)
{
	return host_xdg_dir ("HOST_XDG_CONFIG_HOME", ".config", NULL);
}

static gboolean
read_exact (GInputStream *in,
            guint64       pos,
            guint8       *buf,
            gsize         len,
            GCancellable *cancellable,
            GError      **error)
{
	gsize nread = 0;

	if (pos > (guint64) G_MAXINT64)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "File offset is too large");
		return FALSE;
	}

	if (!g_seekable_seek (G_SEEKABLE (in), (goffset) pos, G_SEEK_SET, cancellable, error))
		return FALSE;

	while (nread < len)
	{
		gssize r;

		r = g_input_stream_read (in, buf + nread, len - nread, cancellable, error);
		if (r < 0)
			return FALSE;
		if (r == 0)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unexpected end of file");
			return FALSE;
		}
		nread += (gsize) r;
	}

	return TRUE;
}

static gboolean
find_squashfs_offset (const char   *path,
                      guint64      *offset_out,
                      GCancellable *cancellable,
                      GError      **error)
{
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInputStream) in = NULL;
	guint8 hdr[64];
	guint8 phdr[56];
	guint64 e_phoff;
	guint64 e_shoff;
	guint64 elf_end = 0;
	guint64 sh_bytes;
	guint64 sh_end;
	guint16 e_phentsize;
	guint16 e_phnum;
	guint16 e_shentsize;
	guint16 e_shnum;
	guint16 i;
	guint8 *buf = NULL;
	gssize nread;
	gssize at;
	gboolean found = FALSE;

	file = g_file_new_for_path (path);
	in = g_file_read (file, cancellable, error);
	if (in == NULL)
		return FALSE;

	if (!read_exact (G_INPUT_STREAM (in), 0, hdr, sizeof hdr, cancellable, error))
		return FALSE;

	if (hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F')
		return FALSE;
	/* ELFCLASS64, ELFDATA2LSB. Type 2 AppImages are 64-bit little-endian. */
	if (hdr[4] != 2 || hdr[5] != 1)
		return FALSE;

	e_phoff = read_le64 (hdr + 32);
	e_shoff = read_le64 (hdr + 40);
	e_phentsize = read_le16 (hdr + 54);
	e_phnum = read_le16 (hdr + 56);
	e_shentsize = read_le16 (hdr + 58);
	e_shnum = read_le16 (hdr + 60);

	if (e_phentsize < sizeof phdr || e_phnum == 0 || e_phnum > 256)
		return FALSE;

	for (i = 0; i < e_phnum; i++)
	{
		guint64 p_offset;
		guint64 p_filesz;
		guint64 end;
		guint64 pos;

		if (i > 0 && e_phoff > G_MAXUINT64 - ((guint64) i * e_phentsize))
			return FALSE;
		pos = e_phoff + ((guint64) i * e_phentsize);

		if (!read_exact (G_INPUT_STREAM (in), pos, phdr, sizeof phdr, cancellable, error))
			return FALSE;

		p_offset = read_le64 (phdr + 8);
		p_filesz = read_le64 (phdr + 32);
		if (p_filesz > G_MAXUINT64 - p_offset)
			continue;
		end = p_offset + p_filesz;
		if (end > elf_end)
			elf_end = end;
	}

	if (e_shnum > 0 && e_shentsize > 0 && e_shoff > 0)
	{
		if ((guint64) e_shnum > G_MAXUINT64 / e_shentsize)
			return FALSE;
		sh_bytes = (guint64) e_shentsize * e_shnum;
		if (e_shoff > G_MAXUINT64 - sh_bytes)
			return FALSE;
		sh_end = e_shoff + sh_bytes;
		if (sh_end > elf_end)
			elf_end = sh_end;
	}

	if (elf_end == 0)
		return FALSE;

	buf = g_malloc (LIFTOFF_SQUASHFS_SCAN_BYTES);
	if (elf_end > (guint64) G_MAXINT64 ||
	    !g_seekable_seek (G_SEEKABLE (in), (goffset) elf_end, G_SEEK_SET, cancellable, error))
	{
		g_free (buf);
		return FALSE;
	}

	nread = g_input_stream_read (G_INPUT_STREAM (in), buf, LIFTOFF_SQUASHFS_SCAN_BYTES, cancellable, error);
	if (nread < 0)
	{
		g_free (buf);
		return FALSE;
	}

	for (at = 0; at + 4 <= nread; at++)
	{
		if (buf[at] == 'h' && buf[at + 1] == 's' && buf[at + 2] == 'q' && buf[at + 3] == 's')
		{
			*offset_out = elf_end + (guint64) at;
			found = TRUE;
			break;
		}
	}

	g_free (buf);
	return found;
}

gboolean
liftoff_path_is_appimage (const char *path)
{
	guint64 offset = 0;
	g_autoptr(GError) error = NULL;

	if (path == NULL || path[0] == '\0')
		return FALSE;
	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
		return FALSE;

	return find_squashfs_offset (path, &offset, NULL, &error);
}

static gboolean
is_icon_ext (const char *ext)
{
	return g_ascii_strcasecmp (ext, "png") == 0
		|| g_ascii_strcasecmp (ext, "svg") == 0
		|| g_ascii_strcasecmp (ext, "xpm") == 0;
}

static const char *sniff_icon_ext (const char *path);

static gboolean
is_icon_filename (const char *name)
{
	const char *dot;

	dot = strrchr (name, '.');
	if (dot == NULL || dot == name || dot[1] == '\0')
		return FALSE;

	return is_icon_ext (dot + 1);
}

static int
icon_ext_rank (const char *name)
{
	const char *dot;

	dot = strrchr (name, '.');
	if (dot == NULL)
		return 0;
	if (g_ascii_strcasecmp (dot + 1, "png") == 0)
		return 3;
	if (g_ascii_strcasecmp (dot + 1, "svg") == 0)
		return 2;
	if (g_ascii_strcasecmp (dot + 1, "xpm") == 0)
		return 1;
	return 0;
}

static void
set_command_error (GError     **error,
                   const char  *fallback,
                   const char  *stderr_text)
{
	g_autofree char *detail = NULL;
	const char *start;
	gsize len;

	start = stderr_text != NULL ? stderr_text : "";
	while (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t')
		start++;

	if (*start == '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", fallback);
		return;
	}

	len = strlen (start);
	if (len > 500)
		len = 500;
	detail = g_strndup (start, len);
	g_strchomp (detail);
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", detail);
}

static gboolean
run_unsquashfs (const char * const *argv,
                char              **stdout_text,
                GCancellable       *cancellable,
                GError            **error)
{
	g_autoptr(GSubprocess) proc = NULL;
	g_autoptr(GBytes) out_bytes = NULL;
	g_autoptr(GBytes) err_bytes = NULL;
	g_autofree char *stderr_text = NULL;
	const guint8 *data;
	gsize len = 0;

	proc = g_subprocess_newv (argv,
	                          G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
	                          error);
	if (proc == NULL)
		return FALSE;

	if (!g_subprocess_communicate (proc, NULL, cancellable, &out_bytes, &err_bytes, error))
		return FALSE;

	if (err_bytes != NULL)
	{
		data = g_bytes_get_data (err_bytes, &len);
		stderr_text = g_strndup ((const char *) data, len);
	}

	if (!g_subprocess_get_successful (proc))
	{
		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			return FALSE;
		set_command_error (error, "unsquashfs failed", stderr_text);
		return FALSE;
	}

	if (stdout_text != NULL)
	{
		len = 0;
		data = out_bytes != NULL ? g_bytes_get_data (out_bytes, &len) : NULL;
		*stdout_text = g_strndup (data != NULL ? (const char *) data : "", len);
	}

	return TRUE;
}

static GPtrArray *
unsquashfs_argv (const char *unsquashfs,
                 guint64     offset,
                 const char *image,
                 const char *dest)
{
	GPtrArray *argv;
	char *offset_str;

	argv = g_ptr_array_new_with_free_func (g_free);
	offset_str = g_strdup_printf ("%" G_GUINT64_FORMAT, offset);

	g_ptr_array_add (argv, g_strdup (unsquashfs));
	g_ptr_array_add (argv, g_strdup ("-q"));
	g_ptr_array_add (argv, g_strdup ("-n"));
	g_ptr_array_add (argv, g_strdup ("-no-xattrs"));
	g_ptr_array_add (argv, g_strdup ("-o"));
	g_ptr_array_add (argv, offset_str);
	if (dest != NULL)
	{
		g_ptr_array_add (argv, g_strdup ("-f"));
		g_ptr_array_add (argv, g_strdup ("-d"));
		g_ptr_array_add (argv, g_strdup (dest));
	}
	else
	{
		g_ptr_array_add (argv, g_strdup ("-l"));
	}
	g_ptr_array_add (argv, g_strdup (image));

	return argv;
}

static gboolean
argv_finish (GPtrArray     *argv,
             char         **stdout_text,
             GCancellable  *cancellable,
             GError       **error)
{
	gboolean ok;

	g_ptr_array_add (argv, NULL);
	ok = run_unsquashfs ((const char * const *) argv->pdata, stdout_text, cancellable, error);
	g_ptr_array_free (argv, TRUE);
	return ok;
}

static GHashTable *
list_image (const char   *unsquashfs,
            guint64       offset,
            const char   *image,
            GCancellable *cancellable,
            GError      **error)
{
	g_autoptr(GPtrArray) argv = NULL;
	g_autofree char *text = NULL;
	g_auto(GStrv) lines = NULL;
	GHashTable *files;
	guint i;

	argv = unsquashfs_argv (unsquashfs, offset, image, NULL);
	if (!argv_finish (g_steal_pointer (&argv), &text, cancellable, error))
		return NULL;

	files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
	{
		char *line = lines[i];
		char *rel;

		g_strchomp (line);
		if (!g_str_has_prefix (line, "squashfs-root/"))
			continue;

		rel = g_strdup (line + strlen ("squashfs-root/"));
		if (rel[0] == '\0')
		{
			g_free (rel);
			continue;
		}

		if (!g_hash_table_add (files, rel))
			g_free (rel);
	}

	return files;
}

static gboolean
path_is_dir (GHashTable *files,
             const char *rel)
{
	GHashTableIter iter;
	gpointer key;
	gsize len;

	len = strlen (rel);
	g_hash_table_iter_init (&iter, files);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		const char *other = key;

		if (g_str_has_prefix (other, rel) && other[len] == '/')
			return TRUE;
	}

	return FALSE;
}

static gboolean
extract_rel (const char   *unsquashfs,
             guint64       offset,
             const char   *image,
             const char   *dest,
             const char   *rel,
             GCancellable *cancellable,
             GError      **error)
{
	GPtrArray *argv;

	if (rel[0] == '-' || strstr (rel, "/-") != NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Refusing to extract an unsafe path");
		return FALSE;
	}

	argv = unsquashfs_argv (unsquashfs, offset, image, dest);
	g_ptr_array_add (argv, g_strdup (rel));
	return argv_finish (argv, NULL, cancellable, error);
}

static char *
symlink_target_rel (const char *temp_root,
                    const char *current_rel,
                    const char *link_text)
{
	g_autofree char *root_canon = NULL;
	g_autofree char *current_abs = NULL;
	g_autofree char *parent = NULL;
	g_autofree char *joined = NULL;
	g_autofree char *canon = NULL;
	const char *rel;
	gsize root_len;

	root_canon = g_canonicalize_filename (temp_root, NULL);
	current_abs = g_build_filename (root_canon, current_rel, NULL);

	if (g_path_is_absolute (link_text))
	{
		/* Absolute links are inside the image, not the host filesystem. */
		while (*link_text == '/')
			link_text++;
		if (*link_text == '\0')
			return NULL;
		joined = g_build_filename (root_canon, link_text, NULL);
	}
	else
	{
		parent = g_path_get_dirname (current_abs);
		joined = g_build_filename (parent, link_text, NULL);
	}

	canon = g_canonicalize_filename (joined, NULL);
	root_len = strlen (root_canon);
	if (strncmp (canon, root_canon, root_len) != 0)
		return NULL;
	if (canon[root_len] != '\0' && canon[root_len] != '/')
		return NULL;

	rel = canon + root_len;
	if (*rel == '/')
		rel++;
	if (*rel == '\0')
		return NULL;

	return g_strdup (rel);
}

static char *
materialize_file (const char   *unsquashfs,
                  guint64       offset,
                  const char   *image,
                  const char   *temp_root,
                  GHashTable   *files,
                  const char   *start_rel,
                  GCancellable *cancellable,
                  GError      **error)
{
	g_autofree char *current = NULL;
	int depth;

	if (start_rel == NULL || !g_hash_table_contains (files, start_rel) || path_is_dir (files, start_rel))
		return NULL;

	current = g_strdup (start_rel);

	for (depth = 0; depth < LIFTOFF_MAX_SYMLINKS; depth++)
	{
		g_autofree char *abs = NULL;
		g_autofree char *target = NULL;
		g_autofree char *next = NULL;

		if (!g_hash_table_contains (files, current) || path_is_dir (files, current))
			return NULL;

		if (!extract_rel (unsquashfs, offset, image, temp_root, current, cancellable, error))
			return NULL;

		abs = g_build_filename (temp_root, current, NULL);
		if (g_file_test (abs, G_FILE_TEST_IS_SYMLINK))
		{
			target = g_file_read_link (abs, error);
			if (target == NULL)
				return NULL;

			next = symlink_target_rel (temp_root, current, target);
			if (next == NULL)
				return NULL;

			g_free (current);
			current = g_steal_pointer (&next);
			continue;
		}

		if (g_file_test (abs, G_FILE_TEST_IS_REGULAR))
			return g_steal_pointer (&abs);

		return NULL;
	}

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Too many icon symlinks");
	return NULL;
}

static char *
materialize_icon (const char   *unsquashfs,
                  guint64       offset,
                  const char   *image,
                  const char   *temp_root,
                  GHashTable   *files,
                  const char   *start_rel,
                  GCancellable *cancellable,
                  GError      **error)
{
	g_autofree char *abs = NULL;
	g_autofree char *base = NULL;

	abs = materialize_file (unsquashfs, offset, image, temp_root, files, start_rel, cancellable, error);
	if (abs == NULL)
		return NULL;

	base = g_path_get_basename (abs);
	if (!is_icon_filename (base) && sniff_icon_ext (abs) == NULL)
		return NULL;

	return g_steal_pointer (&abs);
}

static int
desktop_score (const char *rel,
               const char *stem)
{
	g_autofree char *name = NULL;
	gsize len;

	if (strchr (rel, '/') != NULL)
		return -1;
	if (!g_str_has_suffix (rel, ".desktop"))
		return -1;

	len = strlen (rel) - strlen (".desktop");
	name = g_strndup (rel, len);
	if (stem != NULL && g_ascii_strcasecmp (name, stem) == 0)
		return 2;

	return 1;
}

static char *
icon_stem (const char *appimage_path)
{
	g_autofree char *base = NULL;
	gsize len;

	base = g_path_get_basename (appimage_path);
	len = strlen (base);
	if (len > 9 && g_ascii_strcasecmp (base + len - 9, ".appimage") == 0)
		base[len - 9] = '\0';

	if (base[0] == '\0' || g_strcmp0 (base, ".") == 0 || g_strcmp0 (base, "..") == 0)
		return g_strdup ("appimage");

	return g_steal_pointer (&base);
}

static char *
read_desktop_icon_field (const char   *unsquashfs,
                         guint64       offset,
                         const char   *image,
                         const char   *temp_root,
                         GHashTable   *files,
                         const char   *stem,
                         GCancellable *cancellable,
                         GError      **error)
{
	GHashTableIter iter;
	gpointer key;
	const char *best = NULL;
	int best_score = -1;
	g_autofree char *abs = NULL;
	g_autoptr(GKeyFile) keyfile = NULL;
	g_autoptr(GError) local_error = NULL;

	g_hash_table_iter_init (&iter, files);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		const char *rel = key;
		int score;

		score = desktop_score (rel, stem);
		if (score > best_score || (score == best_score && score > 0 && strcmp (rel, best) < 0))
		{
			best = rel;
			best_score = score;
		}
	}

	if (best == NULL || best_score < 1)
		return NULL;

	abs = materialize_file (unsquashfs, offset, image, temp_root, files, best, cancellable, error);
	if (abs == NULL)
		return NULL;

	keyfile = g_key_file_new ();
	if (!g_key_file_load_from_file (keyfile, abs, G_KEY_FILE_NONE, &local_error))
		return NULL;

	return g_key_file_get_string (keyfile, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, NULL);
}

static gboolean
hicolor_match (const char *rel,
               const char *icon_name,
               int        *size_out)
{
	const char *prefix = "usr/share/icons/hicolor/";
	const char *size_end;
	const char *file;
	g_autofree char *size_tok = NULL;
	g_autofree char *base_name = NULL;
	gsize name_len;

	if (!g_str_has_prefix (rel, prefix))
		return FALSE;

	size_end = strchr (rel + strlen (prefix), '/');
	if (size_end == NULL || !g_str_has_prefix (size_end + 1, "apps/"))
		return FALSE;

	file = size_end + 1 + strlen ("apps/");
	if (strchr (file, '/') != NULL || !is_icon_filename (file))
		return FALSE;

	name_len = strlen (icon_name);
	if (strncmp (file, icon_name, name_len) != 0 || file[name_len] != '.')
		return FALSE;

	base_name = g_strndup (file, name_len);
	if (g_strcmp0 (base_name, icon_name) != 0)
		return FALSE;

	size_tok = g_strndup (rel + strlen (prefix), (gsize) (size_end - (rel + strlen (prefix))));
	if (g_strcmp0 (size_tok, "scalable") == 0)
		*size_out = 0;
	else
		*size_out = (int) g_ascii_strtoll (size_tok, NULL, 10);

	return TRUE;
}

static char *
strip_icon_name (const char *icon_field)
{
	g_autofree char *base = NULL;
	const char *dot;

	base = g_path_get_basename (icon_field);
	dot = strrchr (base, '.');
	if (dot != NULL && is_icon_ext (dot + 1))
		return g_strndup (base, (gsize) (dot - base));

	return g_steal_pointer (&base);
}

static const char *
best_hicolor_icon (GHashTable *files,
                   const char *icon_name)
{
	GHashTableIter iter;
	gpointer key;
	const char *best = NULL;
	int best_size = -1;
	int best_rank = -1;

	g_hash_table_iter_init (&iter, files);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		const char *rel = key;
		int size = 0;
		int rank;

		if (!hicolor_match (rel, icon_name, &size))
			continue;

		rank = icon_ext_rank (rel);
		if (size > best_size || (size == best_size && rank > best_rank))
		{
			best = rel;
			best_size = size;
			best_rank = rank;
		}
	}

	return best;
}

static const char *
best_root_icon (GHashTable *files,
                const char *icon_name)
{
	const char *exts[] = { "png", "svg", "xpm", NULL };
	guint i;

	for (i = 0; exts[i] != NULL; i++)
	{
		g_autofree char *rel = NULL;
		gpointer orig = NULL;

		rel = g_strdup_printf ("%s.%s", icon_name, exts[i]);
		if (g_hash_table_lookup_extended (files, rel, &orig, NULL))
			return orig;
	}

	return NULL;
}

static char *
find_named_icon (const char   *unsquashfs,
                 guint64       offset,
                 const char   *image,
                 const char   *temp_root,
                 GHashTable   *files,
                 const char   *icon_field,
                 GCancellable *cancellable,
                 GError      **error)
{
	g_autofree char *icon_name = NULL;
	const char *rel;
	const char *chosen;

	if (icon_field == NULL || icon_field[0] == '\0')
		return NULL;

	if (strchr (icon_field, '/') != NULL)
	{
		rel = icon_field[0] == '/' ? icon_field + 1 : icon_field;
		if (g_hash_table_contains (files, rel))
			return materialize_icon (unsquashfs, offset, image, temp_root, files, rel, cancellable, error);
	}

	icon_name = strip_icon_name (icon_field);
	chosen = best_hicolor_icon (files, icon_name);
	if (chosen == NULL)
		chosen = best_root_icon (files, icon_name);
	if (chosen == NULL)
		return NULL;

	return materialize_icon (unsquashfs, offset, image, temp_root, files, chosen, cancellable, error);
}

static void
remove_tree (const char *path)
{
	GDir *dir;
	const char *name;

	if (g_file_test (path, G_FILE_TEST_IS_SYMLINK) || !g_file_test (path, G_FILE_TEST_IS_DIR))
	{
		g_unlink (path);
		return;
	}

	dir = g_dir_open (path, 0, NULL);
	if (dir != NULL)
	{
		while ((name = g_dir_read_name (dir)) != NULL)
		{
			g_autofree char *child = NULL;

			child = g_build_filename (path, name, NULL);
			remove_tree (child);
		}
		g_dir_close (dir);
	}

	g_rmdir (path);
}

static const char *
sniff_icon_ext (const char *path)
{
	FILE *f;
	char buf[512];
	size_t n;
	size_t i = 0;

	f = fopen (path, "rb");
	if (f == NULL)
		return NULL;

	n = fread (buf, 1, sizeof buf - 1, f);
	fclose (f);
	buf[n] = '\0';

	if (n >= 8 && memcmp (buf, "\x89PNG\r\n\x1a\n", 8) == 0)
		return "png";
	if (n >= 9 && memcmp (buf, "/* XPM */", 9) == 0)
		return "xpm";

	while (i < n && g_ascii_isspace (buf[i]))
		i++;
	if (g_str_has_prefix (buf + i, "<?xml") || g_str_has_prefix (buf + i, "<svg"))
		return "svg";

	return NULL;
}

static char *
copy_icon (const char   *src,
           const char   *dest_dir,
           const char   *appimage_path,
           GError      **error)
{
	g_autofree char *stem = NULL;
	g_autofree char *base = NULL;
	g_autofree char *filename = NULL;
	g_autofree char *dest = NULL;
	g_autofree char *contents = NULL;
	const char *dot;
	const char *ext;
	GStatBuf st;
	gsize length = 0;
	int saved;

	if (g_stat (src, &st) != 0)
	{
		saved = errno;
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Could not read extracted icon");
		return NULL;
	}
	if (st.st_size <= 0 || st.st_size > LIFTOFF_ICON_MAX_BYTES)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Extracted icon has an unexpected size");
		return NULL;
	}

	base = g_path_get_basename (src);
	dot = strrchr (base, '.');
	if (dot != NULL && is_icon_ext (dot + 1))
		ext = dot + 1;
	else
		ext = sniff_icon_ext (src);

	if (ext == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Extracted icon has an unsupported type");
		return NULL;
	}

	if (g_mkdir_with_parents (dest_dir, 0755) != 0)
	{
		saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Could not create icon directory: %s", g_strerror (saved));
		return NULL;
	}

	stem = icon_stem (appimage_path);
	filename = g_strdup_printf ("%s.%s", stem, ext);
	dest = g_build_filename (dest_dir, filename, NULL);

	if (!g_file_get_contents (src, &contents, &length, error))
		return NULL;
	if (!g_file_set_contents (dest, contents, (gssize) length, error))
		return NULL;

	return g_steal_pointer (&dest);
}

char *
liftoff_appimage_extract_icon (const char   *appimage_path,
                               const char   *dest_dir,
                               GCancellable *cancellable,
                               GError      **error)
{
	guint64 offset = 0;
	g_autofree char *unsquashfs = NULL;
	g_autofree char *tmp = NULL;
	g_autofree char *stem = NULL;
	g_autofree char *icon_field = NULL;
	g_autofree char *resolved = NULL;
	g_autoptr(GHashTable) files = NULL;
	char *result = NULL;

	g_return_val_if_fail (appimage_path != NULL, NULL);
	g_return_val_if_fail (dest_dir != NULL, NULL);

	if (!find_squashfs_offset (appimage_path, &offset, cancellable, error))
	{
		if (error != NULL && *error == NULL)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Not an AppImage");
		return NULL;
	}

	unsquashfs = g_find_program_in_path ("unsquashfs");
	if (unsquashfs == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "unsquashfs was not found");
		return NULL;
	}

	tmp = g_dir_make_tmp ("liftoff-appimage-XXXXXX", error);
	if (tmp == NULL)
		return NULL;

	files = list_image (unsquashfs, offset, appimage_path, cancellable, error);
	if (files == NULL)
		goto cleanup;

	resolved = materialize_icon (unsquashfs, offset, appimage_path, tmp, files, ".DirIcon", cancellable, error);
	if (resolved == NULL && error != NULL && *error != NULL)
		goto cleanup;

	if (resolved == NULL)
	{
		stem = icon_stem (appimage_path);
		icon_field = read_desktop_icon_field (unsquashfs, offset, appimage_path, tmp, files, stem, cancellable, error);
		if (icon_field == NULL && error != NULL && *error != NULL)
			goto cleanup;
		if (icon_field != NULL)
		{
			resolved = find_named_icon (unsquashfs, offset, appimage_path, tmp, files, icon_field, cancellable, error);
			if (resolved == NULL && error != NULL && *error != NULL)
				goto cleanup;
		}
	}

	if (resolved == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No icon found in AppImage");
		goto cleanup;
	}

	result = copy_icon (resolved, dest_dir, appimage_path, error);

cleanup:
	if (tmp != NULL)
		remove_tree (tmp);
	return result;
}

#ifdef LIFTOFF_APPIMAGE_TEST
int
main (int    argc,
      char **argv)
{
	g_autofree char *data_dir = NULL;
	g_autofree char *dest_dir = NULL;
	g_autofree char *icon = NULL;
	g_autoptr(GError) error = NULL;

	if (argc != 2)
	{
		g_printerr ("usage: %s APPIMAGE\n", argv[0]);
		return 2;
	}

	if (!liftoff_path_is_appimage (argv[1]))
	{
		g_printerr ("not an AppImage\n");
		return 1;
	}

	data_dir = liftoff_host_data_dir ();
	dest_dir = g_build_filename (data_dir, "liftoff", "icons", NULL);
	icon = liftoff_appimage_extract_icon (argv[1], dest_dir, NULL, &error);
	if (icon == NULL)
	{
		g_printerr ("%s\n", error != NULL ? error->message : "failed");
		return 1;
	}

	g_print ("%s\n", icon);
	return 0;
}
#endif

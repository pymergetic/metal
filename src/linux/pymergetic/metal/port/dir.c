/*
 * Port — linux bind implementation. opendir()/readdir(); is_dir comes
 * from a stat() on each entry rather than trusting dirent->d_type, since
 * POSIX allows d_type == DT_UNKNOWN on some filesystems.
 */
#include "pymergetic/metal/port/dir.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int pm_metal_port_dir_list(const char *host_path, void (*visit)(const char *name, int is_dir, void *ctx),
			    void *ctx)
{
	DIR *d = opendir(host_path);

	if (!d) {
		return -1;
	}

	if (visit) {
		struct dirent *ent;

		while ((ent = readdir(d)) != NULL) {
			if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
				continue;
			}

			char full[PATH_MAX];
			struct stat st;
			int is_dir = 0;

			if (snprintf(full, sizeof(full), "%s/%s", host_path, ent->d_name) < (int)sizeof(full)
			    && stat(full, &st) == 0) {
				is_dir = S_ISDIR(st.st_mode);
			}
			visit(ent->d_name, is_dir, ctx);
		}
	}

	closedir(d);
	return 0;
}

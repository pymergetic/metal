/*
 * Shell builtin — `cd`. See cd.h.
 */
#include "pymergetic/metal/shell/commands/cd.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/port/dir.h"
#include "pymergetic/metal/util/log.h"

int pm_metal_shell_cmd_cd(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	const char *target = argc < 2 ? "/" : argv[1];
	char resolved[PM_METAL_SHELL_PATH_MAX];

	pm_metal_shell_resolve_path(ctx->cwd, target, resolved, sizeof(resolved));

	/* "/bin/pm" is virtual (the native command registry, see shell.h) —
	 * nothing to check on the real filesystem. Everything else must be
	 * a real, listable vfs_root directory. */
	if (strcmp(resolved, PM_METAL_SHELL_BIN_DIR "/pm")) {
		char host_path[PM_METAL_SHELL_PATH_MAX];

		if (pm_metal_runtime_resolve_path(resolved, host_path, sizeof(host_path)) != 0
		    || pm_metal_port_dir_list(host_path, NULL, NULL) != 0) {
			pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "cd: no such directory: %s",
						 resolved);
			return -1;
		}
	}

	snprintf(ctx->cwd, sizeof(ctx->cwd), "%s", resolved);
	return 0;
}

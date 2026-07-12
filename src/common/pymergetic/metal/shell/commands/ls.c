/*
 * Shell builtin — `ls`. See ls.h.
 */
#include "pymergetic/metal/shell/commands/ls.h"

#include <string.h>

#include "pymergetic/metal/port/dir.h"
#include "pymergetic/metal/util/log.h"

static void pm_metal_shell_cmd_ls_visit_entry(const char *name, int is_dir, void *raw)
{
	pm_metal_shell_ctx_t *ctx = raw;

	pm_metal_util_log_write_raw(ctx->sink->out, "%s%s", name, is_dir ? "/" : "");
}

static void pm_metal_shell_cmd_ls_visit_command(const pm_metal_shell_command_t *cmd, void *raw)
{
	pm_metal_shell_ctx_t *ctx = raw;

	pm_metal_util_log_write_raw(ctx->sink->out, "%s", cmd->name);
}

int pm_metal_shell_cmd_ls(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	const char *target = argc < 2 ? ctx->cwd : argv[1];
	char resolved[PM_METAL_SHELL_PATH_MAX];

	pm_metal_shell_resolve_path(ctx->cwd, target, resolved, sizeof(resolved));

	if (!strcmp(resolved, PM_METAL_SHELL_BIN_DIR "/pm")) {
		pm_metal_shell_list_commands(pm_metal_shell_cmd_ls_visit_command, ctx);
		return 0;
	}

	char host_path[PM_METAL_SHELL_PATH_MAX];

	if (pm_metal_runtime_resolve_path(resolved, host_path, sizeof(host_path)) != 0
	    || pm_metal_port_dir_list(host_path, pm_metal_shell_cmd_ls_visit_entry, ctx) != 0) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "ls: no such directory: %s", resolved);
		return -1;
	}
	return 0;
}

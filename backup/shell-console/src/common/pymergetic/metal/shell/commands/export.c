/*
 * Shell builtin — `export`. See export.h.
 */
#include "pymergetic/metal/shell/commands/export.h"

#include <string.h>

#include "pymergetic/metal/util/log.h"

int pm_metal_shell_cmd_export(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	if (argc < 2 || !strchr(argv[1], '=')) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "usage: export KEY=VALUE");
		return -1;
	}
	if (pm_metal_shell_env_set(ctx, argv[1]) != 0) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "export: env table full");
		return -1;
	}
	return 0;
}

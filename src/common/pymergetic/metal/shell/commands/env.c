/*
 * Shell builtin — `env`. See env.h.
 */
#include "pymergetic/metal/shell/commands/env.h"

#include "pymergetic/metal/util/log.h"

int pm_metal_shell_cmd_env(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	(void)argc;
	(void)argv;

	int i;

	for (i = 0; i < ctx->env_count; i++) {
		pm_metal_util_log_write_raw(ctx->sink->out, "%s", ctx->env[i]);
	}
	return 0;
}

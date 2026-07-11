/*
 * Shell builtin — `pwd`. See pwd.h.
 */
#include "pymergetic/metal/shell/commands/pwd.h"

#include "pymergetic/metal/util/log.h"

int pm_metal_shell_cmd_pwd(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	(void)argc;
	(void)argv;
	pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_INFO, "%s", ctx->cwd);
	return 0;
}

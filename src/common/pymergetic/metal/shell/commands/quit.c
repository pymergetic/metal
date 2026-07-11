/*
 * Shell builtin — `quit`/`exit`. See quit.h.
 */
#include "pymergetic/metal/shell/commands/quit.h"

#include "pymergetic/metal/shell/handles.h"
#include "pymergetic/metal/util/log.h"

int pm_metal_shell_cmd_quit(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	(void)argc;
	(void)argv;
	pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_INFO, "shutting down...");
	if (g_pm_metal_shell_quit_cb) {
		g_pm_metal_shell_quit_cb();
	}
	return 0;
}

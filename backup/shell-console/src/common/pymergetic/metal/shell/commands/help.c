/*
 * Shell builtin — `help`. See help.h.
 */
#include "pymergetic/metal/shell/commands/help.h"

#include "pymergetic/metal/util/log.h"

static void pm_metal_shell_cmd_help_visit(const pm_metal_shell_command_t *cmd, void *visit_ctx)
{
	pm_metal_shell_ctx_t *ctx = visit_ctx;

	pm_metal_util_log_write_raw(ctx->sink->out, "  %-8s %s", cmd->name, cmd->help);
}

int pm_metal_shell_cmd_help(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	(void)argc;
	(void)argv;

	pm_metal_util_log_write_raw(ctx->sink->out, "commands:");
	pm_metal_shell_list_commands(pm_metal_shell_cmd_help_visit, ctx);
	pm_metal_util_log_write_raw(ctx->sink->out,
				     "a name shadowed by " PM_METAL_SHELL_BIN_DIR
				     "/<name>.wasm still reaches its builtin via " PM_METAL_SHELL_BIN_PREFIX
				     "<name>");
	return 0;
}

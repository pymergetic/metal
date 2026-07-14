/*
 * Shell builtin — `exit`. See exit.h.
 */
#include "pymergetic/metal/shell/commands/exit.h"

#include "pymergetic/metal/shell/commands/quit.h"

int pm_metal_shell_cmd_exit(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	return pm_metal_shell_cmd_quit(ctx, argc, argv);
}

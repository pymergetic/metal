/*
 * Shell builtin — `focus`. See focus.h.
 */
#include "pymergetic/metal/shell/commands/focus.h"

#include <string.h>

#include "pymergetic/metal/console/viewport.h"
#include "pymergetic/metal/shell/handles.h"
#include "pymergetic/metal/util/log.h"

int pm_metal_shell_cmd_focus(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	if (argc < 2) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "usage: focus <id|kernel>");
		return -1;
	}

	if (!strcmp(argv[1], "kernel")) {
		pm_metal_viewport_focus(PM_METAL_VIEWPORT_LOCAL, g_pm_metal_shell_kernel_sink);
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_INFO, "focus: kernel");
		return 0;
	}

	int id = pm_metal_shell_parse_id(argv[1]);

	if (id < 0 || !g_pm_metal_shell_handles[id - 1].active) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "no such handle: %s", argv[1]);
		return -1;
	}
	pm_metal_viewport_focus(PM_METAL_VIEWPORT_LOCAL, &g_pm_metal_shell_handles[id - 1].sink);
	/* Lands in the newly-focused pane once it flushes, or kernel's own
	 * backlog if that focus switch is ever undone — never lost either
	 * way, see console/viewport.h. */
	pm_metal_util_log_write(g_pm_metal_shell_handles[id - 1].sink.out, PM_METAL_LOG_INFO, "focus: handle=%d", id);
	return 0;
}

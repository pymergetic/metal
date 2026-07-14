/*
 * Shell builtin — `unload`. See unload.h.
 */
#include "pymergetic/metal/shell/commands/unload.h"

#include "pymergetic/metal/console/viewport.h"
#include "pymergetic/metal/shell/handles.h"
#include "pymergetic/metal/util/log.h"

int pm_metal_shell_cmd_unload(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	if (argc < 2) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "usage: unload <id>");
		return -1;
	}

	int id = pm_metal_shell_parse_id(argv[1]);

	if (id < 0 || !g_pm_metal_shell_handles[id - 1].active) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "no such handle: %s", argv[1]);
		return -1;
	}

	pm_metal_shell_handle_t *mh = &g_pm_metal_shell_handles[id - 1];

	if (pm_metal_runtime_unload(mh->handle) != 0) {
		/* Refused whenever *any* process (see runtime/process.h) is
		 * still executing against this handle — runtime.c's own
		 * refcount already covers this regardless of how many
		 * concurrent `run`s there are, nothing to join here first. */
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_WARN,
					 "unload refused (by design) — handle=%d busy, retry once its run finishes",
					 id);
		return -1;
	}
	pm_metal_viewport_unregister(PM_METAL_VIEWPORT_LOCAL, &mh->sink);
	pm_metal_console_close(&mh->sink);
	mh->active = 0;
	pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_INFO, "unloaded handle=%d", id);
	return 0;
}

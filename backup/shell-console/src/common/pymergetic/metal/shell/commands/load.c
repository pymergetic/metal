/*
 * Shell builtin — `load`. See load.h.
 */
#include "pymergetic/metal/shell/commands/load.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/console/viewport.h"
#include "pymergetic/metal/shell/handles.h"
#include "pymergetic/metal/util/log.h"

int pm_metal_shell_cmd_load(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	if (argc < 2) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "usage: load <path>");
		return -1;
	}

	char resolved[PM_METAL_SHELL_PATH_MAX];

	pm_metal_shell_resolve_path(ctx->cwd, argv[1], resolved, sizeof(resolved));

	pm_metal_runtime_handle_t h;

	if (pm_metal_runtime_load_file(resolved, &h) != 0) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "load failed: %s", resolved);
		return -1;
	}
	if (h.id == 0 || h.id > PM_METAL_RUNTIME_MAX_HANDLES) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR,
					 "load: handle id %u outside tracked range, unloading", h.id);
		pm_metal_runtime_unload(h);
		return -1;
	}

	pm_metal_shell_handle_t *mh = &g_pm_metal_shell_handles[h.id - 1];

	memset(mh, 0, sizeof(*mh));
	mh->handle = h;

	char label[PM_METAL_CONSOLE_LABEL_MAX];

	snprintf(label, sizeof(label), "handle-%u", h.id);
	if (pm_metal_console_open(PM_METAL_CONSOLE_HANDLE, h, label, &mh->sink) != 0
	    || pm_metal_viewport_register(PM_METAL_VIEWPORT_LOCAL, &mh->sink) != 0) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR,
					 "load: console open failed for handle=%u", h.id);
		pm_metal_runtime_unload(h);
		return -1;
	}
	mh->active = 1;

	pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_INFO, "loaded %s -> handle=%u", resolved, h.id);
	return 0;
}

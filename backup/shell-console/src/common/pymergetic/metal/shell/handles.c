/*
 * Shell — handle table storage + init/shutdown. See handles.h and
 * shell/commands.h (handles_init()/handles_shutdown() are declared there —
 * this is simply where their bodies live now, per commands.h's own
 * `impl:` tag).
 */
#include "pymergetic/metal/shell/handles.h"

#include <stdlib.h>
#include <string.h>

#include "pymergetic/metal/console/viewport.h"
#include "pymergetic/metal/shell/commands.h"

pm_metal_shell_handle_t g_pm_metal_shell_handles[PM_METAL_RUNTIME_MAX_HANDLES];
pm_metal_console_sink_t *g_pm_metal_shell_kernel_sink;
void (*g_pm_metal_shell_quit_cb)(void);

int pm_metal_shell_parse_id(const char *tok)
{
	char *end;
	long v = strtol(tok, &end, 10);

	if (end == tok || *end != '\0' || v <= 0 || v > PM_METAL_RUNTIME_MAX_HANDLES) {
		return -1;
	}
	return (int)v;
}

void pm_metal_shell_handles_init(pm_metal_console_sink_t *kernel_sink, void (*quit_cb)(void))
{
	memset(g_pm_metal_shell_handles, 0, sizeof(g_pm_metal_shell_handles));
	g_pm_metal_shell_kernel_sink = kernel_sink;
	g_pm_metal_shell_quit_cb = quit_cb;
}

void pm_metal_shell_handles_shutdown(void)
{
	int i;

	/* No per-handle process join needed here — the caller is required
	 * (see commands.h) to have already called pm_metal_process_shutdown()
	 * before this, which already waited out every in-flight process
	 * against every handle. unload() below would otherwise be refused
	 * exactly like `unload` builtin's own "busy" case (see
	 * docs/RUNTIME.md "Concurrency"). */
	for (i = 0; i < PM_METAL_RUNTIME_MAX_HANDLES; i++) {
		if (!g_pm_metal_shell_handles[i].active) {
			continue;
		}

		pm_metal_shell_handle_t *mh = &g_pm_metal_shell_handles[i];

		pm_metal_runtime_unload(mh->handle);
		pm_metal_viewport_unregister(PM_METAL_VIEWPORT_LOCAL, &mh->sink);
		pm_metal_console_close(&mh->sink);
		mh->active = 0;
	}

	pm_metal_viewport_shutdown(PM_METAL_VIEWPORT_LOCAL);
}

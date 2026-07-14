/*
 * Console — zephyr bind. Stub — deferred with the rest of zephyr (see
 * docs/RUNTIME.md "Bring-up plan" §5). A real implementation needs a
 * bidirectional byte-pipe primitive; Zephyr's own `k_pipe` is the natural
 * fit (unlike linux's pipe(2), it has no fd — callers would go through
 * k_pipe_get()/put() directly rather than FILE-pointer/fd fields, so this
 * stub intentionally does not fill in any of the sink's fd/FILE-pointer
 * fields: no partial/fake implementation to trip up an unwary caller).
 */
#include "pymergetic/metal/console/console.h"

#include <string.h>

int pm_metal_console_open(pm_metal_console_kind_t kind, pm_metal_runtime_handle_t handle,
			   const char *label, pm_metal_console_sink_t *out)
{
	(void)kind;
	(void)handle;
	(void)label;
	if (out) {
		memset(out, 0, sizeof(*out));
	}
	return -1;
}

void pm_metal_console_close(pm_metal_console_sink_t *sink)
{
	(void)sink;
}

void pm_metal_console_stop_feed(pm_metal_console_sink_t *sink)
{
	(void)sink;
}

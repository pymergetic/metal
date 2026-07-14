/*
 * Console — linux bind. Two real pipe(2) pairs per sink — see console.h.
 */
#include "pymergetic/metal/console/console.h"

#include <string.h>
#include <unistd.h>

int pm_metal_console_open(pm_metal_console_kind_t kind, pm_metal_runtime_handle_t handle,
			   const char *label, pm_metal_console_sink_t *out)
{
	if (!out) {
		return -1;
	}

	memset(out, 0, sizeof(*out));
	out->kind = kind;
	out->handle = handle;
	out->producer_out_fd = -1;
	out->consumer_in_fd = -1;
	out->viewport_drain_fd = -1;
	out->viewport_feed_fd = -1;
	if (label) {
		snprintf(out->label, sizeof(out->label), "%s", label);
	}

	/* out_pipe: producer (kernel fprintf / guest WASI stdout+stderr)
	 * writes fd[1], viewport drains fd[0]. in_pipe: viewport feeds
	 * fd[1], consumer (kernel dispatcher fgets / guest WASI stdin)
	 * reads fd[0]. */
	int out_pipe[2];
	int in_pipe[2];

	if (pipe(out_pipe) != 0) {
		return -1;
	}
	if (pipe(in_pipe) != 0) {
		close(out_pipe[0]);
		close(out_pipe[1]);
		return -1;
	}

	out->viewport_drain_fd = out_pipe[0];
	out->producer_out_fd = out_pipe[1];
	out->consumer_in_fd = in_pipe[0];
	out->viewport_feed_fd = in_pipe[1];

	out->out = fdopen(out->producer_out_fd, "w");
	out->in = fdopen(out->consumer_in_fd, "r");
	if (!out->out || !out->in) {
		pm_metal_console_close(out);
		return -1;
	}

	/* Line-buffered: a producer's fprintf() should reach the viewport's
	 * poll() promptly without needing an explicit fflush() at every call
	 * site (pm_metal_util_log_write() already fflush()es itself, but
	 * plain fprintf(sink->out, ...) call sites should not have to). */
	setvbuf(out->out, NULL, _IOLBF, 0);

	return 0;
}

void pm_metal_console_close(pm_metal_console_sink_t *sink)
{
	if (!sink) {
		return;
	}

	if (sink->out) {
		fclose(sink->out); /* also closes producer_out_fd */
	} else if (sink->producer_out_fd >= 0) {
		close(sink->producer_out_fd);
	}
	if (sink->in) {
		fclose(sink->in); /* also closes consumer_in_fd */
	} else if (sink->consumer_in_fd >= 0) {
		close(sink->consumer_in_fd);
	}
	if (sink->viewport_drain_fd >= 0) {
		close(sink->viewport_drain_fd);
	}
	if (sink->viewport_feed_fd >= 0) {
		close(sink->viewport_feed_fd);
	}

	memset(sink, 0, sizeof(*sink));
}

void pm_metal_console_stop_feed(pm_metal_console_sink_t *sink)
{
	if (!sink || sink->viewport_feed_fd < 0) {
		return;
	}
	close(sink->viewport_feed_fd);
	sink->viewport_feed_fd = -1;
}

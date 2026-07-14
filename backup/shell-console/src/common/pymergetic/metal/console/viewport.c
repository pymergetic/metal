/*
 * Console — LOCAL viewport, common half. Registration/focus/filter
 * bookkeeping, the backlog ring, the "[LEVEL] " line filter, and the
 * escape-byte (Ctrl-A) focus switch — all pure struct-and-mutex logic,
 * zero raw fd I/O, so one shared implementation for every target. The
 * remaining bind half (src/<plat>/.../console/viewport.c) is just the
 * poll(2)+read(2)/write(2) loop that feeds bytes into the functions here
 * — see viewport_local.h and docs/CONSOLE.md "What's common vs bind".
 *
 * NETWORK is a stub on every target today (see viewport.h) — this file
 * only has real state for LOCAL.
 *
 * Every entry point below is individually self-locking against
 * g_pm_metal_viewport_local_lock — register()/unregister()/focus()/
 * set_filter() and the pump()-support functions in viewport_local.h may
 * all be called concurrently from different threads (e.g. a kernel
 * command dispatcher thread mutating registrations while the main
 * thread's pump() loop keeps running).
 *
 * Escape byte: once focus is on a handle, ordinary typed input goes
 * straight to *that module's* WASI stdin — including the literal text
 * "list" or "quit", which a guest reading stdin would just receive as
 * input, not a command; there would be no way back to the kernel pane at
 * all otherwise. PM_METAL_VIEWPORT_ESCAPE_BYTE (Ctrl-A, ASCII 0x01) is
 * reserved: pm_metal_viewport_local_consume_escape(), called by the bind
 * pump() on whatever it just read from real stdin, switches focus
 * straight back to slot 0 (whichever sink was registered first — by this
 * codebase's convention, src/linux/main.c always registers the kernel
 * sink before any handle's, so "slot 0" and "kernel" are the same thing
 * in practice) and reports the byte consumed, never forwarded to any
 * sink; anything *after* it in the same chunk is the bind layer's to
 * forward normally, to the (now newly-focused) sink.
 */
#include "pymergetic/metal/console/viewport_local.h"

#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/port/lock.h"
#include "pymergetic/metal/port/term.h"

#define PM_METAL_VIEWPORT_RING_BYTES 4096
#define PM_METAL_VIEWPORT_PENDING_MAX 512
#define PM_METAL_VIEWPORT_ESCAPE_BYTE 0x01 /* Ctrl-A — see file header */

typedef struct pm_metal_viewport_local_slot {
	pm_metal_console_sink_t *sink;
	pm_metal_log_level_t filter;
	char ring[PM_METAL_VIEWPORT_RING_BYTES]; /* backlog for while unfocused — oldest bytes dropped on overflow */
	uint32_t ring_len;
	char pending[PM_METAL_VIEWPORT_PENDING_MAX]; /* one in-progress line, until '\n' */
	uint32_t pending_len;
} pm_metal_viewport_local_slot_t;

static struct {
	int initialized;
	pm_metal_viewport_local_slot_t slots[PM_METAL_VIEWPORT_LOCAL_MAX_SINKS];
	int focus_index; /* -1 = none focused yet */
	pm_metal_log_level_t default_filter;
} g_pm_metal_viewport_local;

/* Guards every field above — see file header. Not used for NETWORK (stub,
 * no state). Initialized once by the first init(PM_METAL_VIEWPORT_LOCAL)
 * and left alive across shutdown()s — cheap, and simpler than re-init()ing
 * a mutex on every init()/shutdown() cycle (unlike runtime.c's, this one
 * never needs destroy() since the process owns it for its whole life). */
static pm_metal_port_mutex_t g_pm_metal_viewport_local_lock;
static int g_pm_metal_viewport_local_lock_ready;

static int pm_metal_viewport_local_slot_index(pm_metal_console_sink_t *sink)
{
	int i;

	for (i = 0; i < PM_METAL_VIEWPORT_LOCAL_MAX_SINKS; i++) {
		if (g_pm_metal_viewport_local.slots[i].sink == sink) {
			return i;
		}
	}
	return -1;
}

/* "[LEVEL] " prefix pm_metal_util_log_write() writes — see log.h/log_impl.h.
 * Anything else (no brackets, or a bracketed word that isn't a known level
 * name) passes through unfiltered: it cannot honestly be classified, so it
 * is never hidden by guesswork — see viewport.h's doc comment. */
static int pm_metal_viewport_local_line_passes(const char *line, size_t len, pm_metal_log_level_t floor)
{
	if (len < 3 || line[0] != '[') {
		return 1;
	}

	size_t close = 1;
	while (close < len && line[close] != ']') {
		close++;
	}
	if (close >= len || close + 1 >= len || line[close + 1] != ' ') {
		return 1;
	}

	size_t name_len = close - 1;
	pm_metal_log_level_t lvl;

	for (lvl = 0; lvl < PM_METAL_LOG_LEVEL_COUNT; lvl++) {
		const char *name = pm_metal_util_log_level_name(lvl);

		if (strlen(name) == name_len && strncmp(line + 1, name, name_len) == 0) {
			return lvl >= floor;
		}
	}

	return 1;
}

static void pm_metal_viewport_local_ring_append(pm_metal_viewport_local_slot_t *slot, const char *data,
						 size_t len)
{
	if (len >= sizeof(slot->ring)) {
		data += len - sizeof(slot->ring);
		len = sizeof(slot->ring);
		slot->ring_len = 0;
	} else if (slot->ring_len + len > sizeof(slot->ring)) {
		uint32_t drop = (uint32_t)(slot->ring_len + len - sizeof(slot->ring));

		memmove(slot->ring, slot->ring + drop, slot->ring_len - drop);
		slot->ring_len -= drop;
	}
	memcpy(slot->ring + slot->ring_len, data, len);
	slot->ring_len += (uint32_t)len;
}

static void pm_metal_viewport_local_emit(pm_metal_viewport_local_slot_t *slot, int focused, const char *line,
					  size_t len)
{
	if (!pm_metal_viewport_local_line_passes(line, len, slot->filter)) {
		return;
	}
	if (focused) {
		pm_metal_port_term_write(line, len);
	} else {
		pm_metal_viewport_local_ring_append(slot, line, len);
	}
}

/* Splits `chunk` on '\n' and filters/emits each complete line; an
 * in-progress final line with no '\n' yet stays in slot->pending until the
 * next chunk completes it (dropped-not-crashed if a single line exceeds
 * PM_METAL_VIEWPORT_PENDING_MAX — pathological producer, not this code's
 * problem to grow unbounded for). */
static void pm_metal_viewport_local_feed(pm_metal_viewport_local_slot_t *slot, int focused, const char *chunk,
					  size_t chunk_len)
{
	size_t i;

	for (i = 0; i < chunk_len; i++) {
		char c = chunk[i];

		if (slot->pending_len < sizeof(slot->pending)) {
			slot->pending[slot->pending_len++] = c;
		}
		if (c == '\n') {
			pm_metal_viewport_local_emit(slot, focused, slot->pending, slot->pending_len);
			slot->pending_len = 0;
		}
	}
}

int pm_metal_viewport_init(pm_metal_viewport_kind_t kind)
{
	if (kind == PM_METAL_VIEWPORT_NETWORK) {
		return 0; /* stub — see viewport.h */
	}

	if (!g_pm_metal_viewport_local_lock_ready) {
		pm_metal_port_mutex_init(&g_pm_metal_viewport_local_lock);
		g_pm_metal_viewport_local_lock_ready = 1;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);
	memset(&g_pm_metal_viewport_local, 0, sizeof(g_pm_metal_viewport_local));
	g_pm_metal_viewport_local.focus_index = -1;
	g_pm_metal_viewport_local.default_filter = PM_METAL_LOG_TRACE; /* show everything by default */
	g_pm_metal_viewport_local.initialized = 1;
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
	return 0;
}

void pm_metal_viewport_shutdown(pm_metal_viewport_kind_t kind)
{
	if (kind == PM_METAL_VIEWPORT_NETWORK) {
		return;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);
	memset(&g_pm_metal_viewport_local, 0, sizeof(g_pm_metal_viewport_local));
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
}

int pm_metal_viewport_register(pm_metal_viewport_kind_t kind, pm_metal_console_sink_t *sink)
{
	if (kind == PM_METAL_VIEWPORT_NETWORK) {
		return 0; /* stub */
	}
	if (!sink) {
		return -1;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);

	if (!g_pm_metal_viewport_local.initialized) {
		pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
		return -1;
	}
	if (pm_metal_viewport_local_slot_index(sink) >= 0) {
		pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
		return 0; /* already registered */
	}

	int i;
	int rc = -1; /* PM_METAL_VIEWPORT_LOCAL_MAX_SINKS reached, unless found below */

	for (i = 0; i < PM_METAL_VIEWPORT_LOCAL_MAX_SINKS; i++) {
		if (!g_pm_metal_viewport_local.slots[i].sink) {
			memset(&g_pm_metal_viewport_local.slots[i], 0,
				sizeof(g_pm_metal_viewport_local.slots[i]));
			g_pm_metal_viewport_local.slots[i].sink = sink;
			g_pm_metal_viewport_local.slots[i].filter = g_pm_metal_viewport_local.default_filter;
			rc = 0;
			break;
		}
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
	return rc;
}

void pm_metal_viewport_unregister(pm_metal_viewport_kind_t kind, pm_metal_console_sink_t *sink)
{
	if (kind == PM_METAL_VIEWPORT_NETWORK) {
		return;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);

	int idx = pm_metal_viewport_local_slot_index(sink);

	if (idx >= 0) {
		if (g_pm_metal_viewport_local.focus_index == idx) {
			g_pm_metal_viewport_local.focus_index = -1;
		}
		memset(&g_pm_metal_viewport_local.slots[idx], 0, sizeof(g_pm_metal_viewport_local.slots[idx]));
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
}

int pm_metal_viewport_focus(pm_metal_viewport_kind_t kind, pm_metal_console_sink_t *sink)
{
	if (kind == PM_METAL_VIEWPORT_NETWORK) {
		return 0; /* no focus concept — see viewport.h */
	}

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);

	int idx = pm_metal_viewport_local_slot_index(sink);

	if (idx < 0) {
		pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
		return -1;
	}
	g_pm_metal_viewport_local.focus_index = idx;

	pm_metal_viewport_local_slot_t *slot = &g_pm_metal_viewport_local.slots[idx];

	if (slot->ring_len > 0) {
		pm_metal_port_term_write(slot->ring, slot->ring_len);
		slot->ring_len = 0;
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
	return 0;
}

void pm_metal_viewport_set_filter(pm_metal_viewport_kind_t kind, pm_metal_console_sink_t *sink,
				   pm_metal_log_level_t floor)
{
	if (kind == PM_METAL_VIEWPORT_NETWORK) {
		return; /* stub */
	}

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);
	if (!sink) {
		g_pm_metal_viewport_local.default_filter = floor;
	} else {
		int idx = pm_metal_viewport_local_slot_index(sink);

		if (idx >= 0) {
			g_pm_metal_viewport_local.slots[idx].filter = floor;
		}
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
}

/* ---- pump()-support (viewport_local.h) — called by the bind pump() loop ---- */

int pm_metal_viewport_local_is_initialized(void)
{
	int rc;

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);
	rc = g_pm_metal_viewport_local.initialized;
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
	return rc;
}

void pm_metal_viewport_local_snapshot(pm_metal_viewport_local_snapshot_t *out)
{
	int i;

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);
	for (i = 0; i < PM_METAL_VIEWPORT_LOCAL_MAX_SINKS; i++) {
		out->sink[i] = g_pm_metal_viewport_local.slots[i].sink;
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
}

void pm_metal_viewport_local_on_sink_bytes(int slot_index, pm_metal_console_sink_t *expect, const char *data,
					    size_t len)
{
	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);

	/* A register()/unregister() may have raced the caller's *unlocked*
	 * poll()/read() and reused this slot for a different sink, or
	 * dropped it entirely — re-check identity, not just the index,
	 * before touching anything. A mismatch is simply dropped; the next
	 * pump() call picks up the current state correctly. */
	pm_metal_viewport_local_slot_t *slot = &g_pm_metal_viewport_local.slots[slot_index];

	if (slot->sink == expect) {
		pm_metal_viewport_local_feed(slot, slot_index == g_pm_metal_viewport_local.focus_index, data,
					      len);
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
}

size_t pm_metal_viewport_local_consume_escape(const char *data, size_t len)
{
	if (len == 0 || data[0] != PM_METAL_VIEWPORT_ESCAPE_BYTE) {
		return 0;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);

	pm_metal_viewport_local_slot_t *slot0 = &g_pm_metal_viewport_local.slots[0];

	if (slot0->sink) {
		g_pm_metal_viewport_local.focus_index = 0;
		if (slot0->ring_len > 0) {
			pm_metal_port_term_write(slot0->ring, slot0->ring_len);
			slot0->ring_len = 0;
		}
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
	return 1;
}

pm_metal_console_sink_t *pm_metal_viewport_local_focused_sink(void)
{
	pm_metal_console_sink_t *sink = NULL;

	pm_metal_port_mutex_lock(&g_pm_metal_viewport_local_lock);
	if (g_pm_metal_viewport_local.focus_index >= 0) {
		sink = g_pm_metal_viewport_local.slots[g_pm_metal_viewport_local.focus_index].sink;
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_viewport_local_lock);
	return sink;
}

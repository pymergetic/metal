/*
 * Console — linux LOCAL viewport bind. Just the poll(2)+read(2)/write(2)
 * loop now — registration/focus/filter/ring/escape-byte logic all moved
 * to src/common/pymergetic/metal/console/viewport.c (impl: common, see
 * viewport_local.h and docs/CONSOLE.md "What's common vs bind"), since
 * none of that is actually OS-specific. What's left here is: gather
 * registered sinks' drain fds + real stdin into one poll(2) set, wait on
 * it (bounded, not -1 — see viewport.h), then for each ready fd either
 * read the real terminal's stdin (handing it to the common escape-byte
 * check, then forwarding whatever's left to the focused sink's feed fd)
 * or read a sink's drain fd (handing the bytes to the common
 * ring/filter/echo logic).
 *
 * Race note: the fd numbers used below come from a snapshot taken under
 * the common layer's lock; poll()/read() themselves then run unlocked
 * (poll() can block, and holding a lock across a blocking read() would
 * stall register()/focus() calls from other threads for no real benefit,
 * since poll() already told us data is ready). A concurrent unregister()+
 * console_close() racing in between could in principle mean a read() here
 * targets an fd number that's just been closed (and, in the worst case,
 * already reused for something unrelated) — the common layer's identity
 * re-check inside on_sink_bytes() (by sink pointer, not just index) still
 * guarantees any bytes read either land in the *correct* slot or are
 * silently dropped, never misrouted; this file's own commands (load/run/
 * unload) are user-driven and infrequent enough that this window is not
 * a practical concern. Same spirit as the "drop stale/mismatched events"
 * contract this file already had before the split.
 */
#include "pymergetic/metal/console/viewport.h"
#include "pymergetic/metal/console/viewport_local.h"

#include <poll.h>
#include <unistd.h>

#define PM_METAL_VIEWPORT_READ_CHUNK 512
#define PM_METAL_VIEWPORT_POLL_TIMEOUT_MS 200

int pm_metal_viewport_pump(pm_metal_viewport_kind_t kind)
{
	if (kind == PM_METAL_VIEWPORT_NETWORK) {
		return 0; /* stub — nothing to pump */
	}

	if (!pm_metal_viewport_local_is_initialized()) {
		return -1;
	}

	pm_metal_viewport_local_snapshot_t snap;

	pm_metal_viewport_local_snapshot(&snap);

	struct pollfd fds[1 + PM_METAL_VIEWPORT_LOCAL_MAX_SINKS];
	int slot_of_fd[1 + PM_METAL_VIEWPORT_LOCAL_MAX_SINKS];
	int n = 0;
	int i;

	fds[n].fd = STDIN_FILENO;
	fds[n].events = POLLIN;
	fds[n].revents = 0;
	slot_of_fd[n] = -1;
	n++;

	for (i = 0; i < PM_METAL_VIEWPORT_LOCAL_MAX_SINKS; i++) {
		if (!snap.sink[i]) {
			continue;
		}
		fds[n].fd = snap.sink[i]->viewport_drain_fd;
		fds[n].events = POLLIN;
		fds[n].revents = 0;
		slot_of_fd[n] = i;
		n++;
	}

	/* Unlocked — this can (and normally does) block. Bounded, not -1:
	 * lets a caller interleave its own "should I stop" check between
	 * calls without a self-pipe/wakeup trick — see src/linux/main.c. */
	int rc = poll(fds, (nfds_t)n, PM_METAL_VIEWPORT_POLL_TIMEOUT_MS);

	if (rc <= 0) {
		return 0; /* timeout, or EINTR/error — caller just calls pump() again */
	}

	char buf[PM_METAL_VIEWPORT_READ_CHUNK];
	int stop = 0;

	for (i = 0; i < n; i++) {
		if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
			continue;
		}

		if (slot_of_fd[i] == -1) { /* STDIN_FILENO */
			ssize_t r = read(STDIN_FILENO, buf, sizeof(buf));

			if (r <= 0) {
				stop = 1; /* real stdin EOF/error */
				continue;
			}

			size_t len = (size_t)r;
			size_t consumed = pm_metal_viewport_local_consume_escape(buf, len);
			char *data = buf + consumed;

			len -= consumed;

			if (len > 0) {
				pm_metal_console_sink_t *fsink = pm_metal_viewport_local_focused_sink();

				if (fsink) {
					ssize_t written = write(fsink->viewport_feed_fd, data, len);

					(void)written; /* best-effort — nothing sane to do if it fails */
				}
			}
			continue;
		}

		ssize_t r = read(snap.sink[slot_of_fd[i]]->viewport_drain_fd, buf, sizeof(buf));

		if (r <= 0) {
			continue; /* producer's write end closed, or a transient hiccup */
		}
		pm_metal_viewport_local_on_sink_bytes(slot_of_fd[i], snap.sink[slot_of_fd[i]], buf, (size_t)r);
	}

	return stop ? -1 : 0;
}

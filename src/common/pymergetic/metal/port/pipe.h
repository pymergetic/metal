/*
 * Port contract — one host-level pipe, for chaining two spawn()ed
 * processes together (see runtime/process.h's spawn() stdin_fd/
 * stdout_fd/stderr_fd) — a driver wanting "A's stdout -> B's stdin"
 * calls pipe() once, passes the write end as A's own stdout_fd and the
 * read end as B's own stdin_fd, then never touches either fd again:
 * process.c's own worker closes whichever of stdin_fd/stdout_fd/
 * stderr_fd it was actually given (any value >= 0 — never the -1
 * "inherit the host's" sentinel) the moment that specific process's
 * run_ex() call returns, not the driver. That's what makes this safe
 * without any dup()/refcounting gymnastics: once handed to spawn(),
 * each end has exactly one owner (the one process it was given to),
 * and closing it the instant that owner finishes is also exactly what
 * lets the *other* end see EOF promptly instead of blocking forever —
 * same reasoning as a real OS closing a process's fds automatically on
 * exit, just done explicitly here since there is no separate kernel
 * doing it for us. See docs/RUNTIME.md "Processes" for the worked
 * example.
 */
#ifndef PYMERGETIC_METAL_PORT_PIPE_H_
#define PYMERGETIC_METAL_PORT_PIPE_H_

#include <stdint.h>

/* impl: bind — src/linux/pymergetic/metal/port/pipe.c
 *              src/zephyr/pymergetic/metal/port/pipe.c (stub — deferred, see docs/RUNTIME.md "Bring-up plan")
 *
 * pipe(): opens one unidirectional host pipe, filling in *out_read_fd
 * (readable end) and *out_write_fd (writable end) — both real fds in
 * the same numbering space run_ex()'s own stdin_fd/stdout_fd/stderr_fd
 * already accept, so either can be handed straight to spawn() with no
 * translation. Returns 0/-1 (out of fds, or not implemented on this
 * target yet). */
int pm_metal_port_pipe(int64_t *out_read_fd, int64_t *out_write_fd);

/* impl: bind — src/linux/pymergetic/metal/port/pipe.c
 *              src/zephyr/pymergetic/metal/port/pipe.c (stub — deferred, see docs/RUNTIME.md "Bring-up plan")
 *
 * close(): closes one fd previously handed to run_ex()/spawn() as
 * stdin_fd/stdout_fd/stderr_fd, or returned by pipe() above — the one
 * primitive runtime/process.c's own worker needs to honor pipe.h's own
 * ownership-transfer contract (see this file's header) without
 * `#include`-ing an OS header itself. No-op-safe to call on a negative
 * fd (i.e. run_ex()'s own -1 "inherit the host's" sentinel) — callers
 * are not required to check first. */
void pm_metal_port_close(int64_t fd);

#endif /* PYMERGETIC_METAL_PORT_PIPE_H_ */

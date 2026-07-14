/*
 * Port contract — one tiny primitive: raw output to whatever "the real
 * local terminal" is on this target. Split out of console/viewport.c so
 * the LOCAL viewport's registration/ring/filter/escape-byte logic (all
 * already portable) can live in impl: common code and still perform the
 * one genuinely OS-specific action it needs — see docs/CONSOLE.md.
 */
#ifndef PYMERGETIC_METAL_PORT_TERM_H_
#define PYMERGETIC_METAL_PORT_TERM_H_

#include <stddef.h>

/* impl: bind — src/linux/pymergetic/metal/port/term.c (STDOUT_FILENO)
 *              src/zephyr/pymergetic/metal/port/term.c (stub — deferred with the rest of zephyr's console/shell, see docs/RUNTIME.md "Bring-up plan")
 *
 * Best-effort raw write of `len` bytes from `buf` to the real local
 * terminal output — linux: STDOUT_FILENO; zephyr later: shell backend /
 * UART. No buffering, no formatting, no return value to check — same
 * spirit as a bare write(2) whose result nothing sane can do about
 * anyway (every real-terminal write in console/viewport.c already
 * treats it this way). */
void pm_metal_port_term_write(const void *buf, size_t len);

#endif /* PYMERGETIC_METAL_PORT_TERM_H_ */

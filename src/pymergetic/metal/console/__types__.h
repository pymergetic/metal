/* pymergetic.metal.console — N text rings (F1–F6) + viewports (serial + fb). */
#ifndef PYMERGETIC_METAL_CONSOLE_TYPES_H
#define PYMERGETIC_METAL_CONSOLE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_metal_console_sink_fn)(const char *s, uint32_t n);

/* Ring shape. Out here rather than private to the card because a reader has
 * to size a buffer for one line.
 *
 * The depth is where a console starts, not where it stops: it is the default
 * of the scrollback knob, the ring is arena memory that grows with the output
 * and follows the knob when a seat moves it, and a reader asking for a line
 * that has scrolled out is told so (`line_at` returns -1) rather than being
 * promised a fixed reach. */
#ifndef PM_METAL_CONSOLE_LINES_DEFAULT
#define PM_METAL_CONSOLE_LINES_DEFAULT 64u
#endif
/* Wider than any screen on purpose: a line is stored as the seat printed it,
 * escapes included, and colour is expensive. One true-colour run costs up to
 * 19 bytes ("\033[38;2;255;255;255m"), so the banner's rainbow rows are ~340
 * bytes for ~50 glyphs. Store 160 and a reader gets the first eight letters
 * of the banner and nothing else, because an over-long line is truncated
 * here, not wrapped. */
#ifndef PM_METAL_CONSOLE_COLS
#define PM_METAL_CONSOLE_COLS 384u
#endif

/* Enough for any one line the ring can hold, terminator included. */
#define PM_METAL_CONSOLE_READ_MAX PM_METAL_CONSOLE_COLS

/* Taps: readers that pull the ring by cursor instead of being written into.
 * A browser panel is one, and it is as much a view of this console as the
 * serial line is — but it comes and goes without saying goodbye, so it counts
 * as present only while it keeps asking. The window is generous next to the
 * panel's 4 Hz poll: a reader on a slow link stays counted, one whose tab was
 * closed is gone within a couple of seconds. */
#ifndef PM_METAL_CONSOLE_TAP_DEFAULT
#define PM_METAL_CONSOLE_TAP_DEFAULT 4u
#endif
#ifndef PM_METAL_CONSOLE_TAP_TTL_MS
#define PM_METAL_CONSOLE_TAP_TTL_MS 3000u
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_CONSOLE_TYPES_H */

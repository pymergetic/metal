/*
 * Metal HAL port — real console/time wiring (W11.2).
 *
 * stdin/stdout go through `dev.stream` (same-language C, direct link —
 * this file and `dev/stream/__init__.c` are plain C in the same link unit,
 * so no registry proxy is needed). Log/time are Rust
 * (`impl = rs`) but not spine, so per the module system's "Consume
 * generated faces" rule this includes their generated `include/` face
 * headers rather than hand-declaring the prototypes.
 */
#include "mphalport.h"

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/dev/serial/__init__.h>
#include <pymergetic/metal/dev/stream/__init__.h>
#include <pymergetic/metal/log/__init__.h>

/*
 * `mp_hal_stdout_tx_strn` is never NUL-terminated and may be called many
 * times per printed value; `pm_metal_log` only takes whole NUL-terminated
 * ASCII lines. Buffer until a '\n' (or the buffer fills) and flush one
 * log line at a time.
 */
#define MPHAL_OUT_BUF 256u

static char g_out_buf[MPHAL_OUT_BUF];
static size_t g_out_len;

static void mphal_flush(void)
{
	if (g_out_len == 0u) {
		return;
	}
	g_out_buf[g_out_len] = '\0';
	pm_metal_log((const uint8_t *)g_out_buf);
	g_out_len = 0u;
}

void mp_hal_stdout_tx_strn(const char *str, size_t len)
{
	size_t i;

	if (str == NULL) {
		return;
	}
	for (i = 0u; i < len; i++) {
		if (str[i] == '\n') {
			mphal_flush();
			continue;
		}
		if (g_out_len + 1u >= MPHAL_OUT_BUF) {
			mphal_flush();
		}
		g_out_buf[g_out_len] = str[i];
		g_out_len++;
	}
}

int mp_hal_stdin_rx_chr(void)
{
	pm_metal_stream_h in;
	uint8_t b;

	in = pm_metal_stdio_in();
	if (in != PM_METAL_STREAM_INVALID) {
		b = 0u;
		if (pm_metal_stream_try_read(in, &b, 1u) == 1u) {
			return (int)b;
		}
	}
	/* No attached stdio stream (or it went dry) -- fall back to the raw
	 * UART RX path (`dev.serial`). Host smoke has no UART: always -1. */
	b = 0u;
	if (pm_metal_dev_serial_try_read(&b, 1u) == 1) {
		return (int)b;
	}
	return -1;
}

uint64_t mp_hal_ticks_us(void)
{
	return pm_metal_time_mono_us();
}

void mp_hal_delay_us(uint32_t us)
{
	(void)us;
	/* No busy-wait here -- callers await via Metal async instead of
	 * blocking the HAL layer (see metal-no-long-running-ops). */
}

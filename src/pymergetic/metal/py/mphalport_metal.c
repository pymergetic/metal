/** @file MicroPython HAL → Metal (port-neutral). */
#include <stddef.h>
#include <string.h>

#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/runtime/async/async.h>

#include "py/mphal.h"
#include "py/mpconfig.h"

#include "py_internal.h" /* IWYU pragma: keep — declares pm_metal_py_stdout_flush, defined below */

/*
 * pm_metal_shell_out()/pm_metal_log() are whole-*line* sinks (one call ==
 * one line on every viewport), but MicroPython's own str/bytes REPR
 * printer (py/objstr.c's quoting loop, used for bare REPL results like
 * `sys.version` or a bytes literal) calls mp_hal_stdout_tx_strn() once
 * PER CHARACTER to do its escaping -- neither call is guaranteed to carry
 * a trailing '\n'. The accumulator below must therefore survive across
 * calls (static, not a local reset to empty every time) and must only
 * hand a line to pm_metal_shell_out() when a real '\n' shows up (or the
 * buffer is full) -- flushing an incomplete line on every call (as if
 * "no newline yet" meant "this fragment IS the whole line") is what
 * turned every multi-call REPR print into one character per output line.
 * pm_metal_py_stdout_flush() (py.c, end of each REPL chunk) is the
 * catch-up for text that never gets a trailing '\n' at all (e.g.
 * `print('x', end='')`) so it still reaches the screen before the next
 * prompt instead of sitting buffered forever.
 */
static char   sStdoutLine[240];
static size_t sStdoutLen = 0;

static void py_stdout_flush_line(void)
{
  if (sStdoutLen > 0u) {
    sStdoutLine[sStdoutLen] = '\0';
    pm_metal_shell_out(sStdoutLine);
    sStdoutLen = 0;
  }
}

void pm_metal_py_stdout_flush(void)
{
  py_stdout_flush_line();
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len)
{
  size_t i = 0;

  if (str == NULL || len == 0) {
    return;
  }
  while (i < len) {
    char c = str[i++];
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      py_stdout_flush_line();
      continue;
    }
    if (sStdoutLen + 1u >= sizeof(sStdoutLine)) {
      py_stdout_flush_line();
    }
    sStdoutLine[sStdoutLen++] = c;
  }
}

void mp_hal_stdout_tx_str(const char *str)
{
  if (str != NULL) {
    mp_hal_stdout_tx_strn_cooked(str, strlen(str));
  }
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len)
{
  mp_hal_stdout_tx_strn_cooked(str, len);
  return (mp_uint_t)len;
}

int mp_hal_stdin_rx_chr(void)
{
  return -1;
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags)
{
  (void)poll_flags;
  return 0;
}

void mp_hal_delay_ms(mp_uint_t ms)
{
  (void)ms; /* product sleep is async via pymergetic.metal.aio */
}

void mp_hal_delay_us(mp_uint_t us)
{
  (void)us;
}

mp_uint_t mp_hal_ticks_ms(void)
{
  return (mp_uint_t)(pm_metal_async_mono_us() / 1000u);
}

mp_uint_t mp_hal_ticks_us(void)
{
  return (mp_uint_t)pm_metal_async_mono_us();
}

mp_uint_t mp_hal_ticks_cpu(void)
{
  return mp_hal_ticks_us();
}

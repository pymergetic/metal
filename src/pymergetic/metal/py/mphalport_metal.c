/** @file MicroPython HAL → Metal (port-neutral). */
#include <stddef.h>
#include <string.h>

#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/runtime/async/async.h>

#include "py/mphal.h"
#include "py/mpconfig.h"

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len)
{
  char   line[240];
  size_t i = 0;
  size_t o = 0;

  if (str == NULL || len == 0) {
    return;
  }
  while (i < len) {
    char c = str[i++];
    if (c == '\r') {
      continue;
    }
    if (c == '\n' || o + 1 >= sizeof(line)) {
      line[o] = '\0';
      pm_metal_shell_out(line);
      o = 0;
      if (c != '\n') {
        line[o++] = c;
      }
      continue;
    }
    line[o++] = c;
  }
  if (o > 0) {
    line[o] = '\0';
    pm_metal_shell_out(line);
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

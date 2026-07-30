#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/phase.h>
#include <pymergetic/metal/async/runner.h>
#include <pymergetic/metal/async/task.h>
#include <pymergetic/metal/async/time.h>
#include "stress.h"
#include <pymergetic/metal/boot/platform/uart.h>
#include <pymergetic/metal/boot/tree/print.h>
#include <pymergetic/metal/dev/blk/__init__.h>
#include <pymergetic/metal/dev/input/__init__.h>
#include <pymergetic/metal/mem/__init__.h>
#include <pymergetic/metal/net/http/__init__.h>
#include <pymergetic/metal/net/ip/__init__.h>
#include <pymergetic/metal/net/ntp/__init__.h>
#include <pymergetic/metal/net/ping/__init__.h>

void *memset(void *dst, int c, size_t n);

#define STRESS_TIMEOUT_US 8000000ull

static uint8_t mStressSector[PM_METAL_DEV_BLK_SECTOR_BYTES];

static size_t cstrlen(const char *s)
{
  size_t n = 0u;

  while (s[n] != '\0') {
    n++;
  }
  return n;
}

static int32_t stress_fail(const char *stage)
{
  const char *prefix = "stress FAIL ";

  pm_metal_boot_uart_write(prefix, cstrlen(prefix));
  pm_metal_boot_uart_write(stage, cstrlen(stage));
  pm_metal_boot_uart_write("\n", 1u);
  return -1;
}

static int32_t wait_handle(uint32_t h, uint64_t timeout_us)
{
  uint64_t deadline;

  if (h == 0u) {
    return -1;
  }
  deadline = pm_metal_time_mono_us() + timeout_us;
  while (pm_metal_time_mono_us() < deadline) {
    pm_metal_async_status_t status;

    pm_metal_net_ip_poll();
    (void)pm_metal_async_run_poll_all();
    status = pm_metal_async_status(h);
    if (status == PM_METAL_ASYNC_DONE) {
      return 0;
    }
    if (status == PM_METAL_ASYNC_ERROR || status == PM_METAL_ASYNC_CANCELLED) {
      return -1;
    }
  }
  return -1;
}

static int32_t mem_smoke(void)
{
  uint8_t *a;
  uint8_t *b;
  uint8_t *r;
  uint8_t *p0;
  uint8_t *p1;
  int32_t i;
  int32_t n_ok;

  a = pm_metal_mem_alloc(64);
  b = pm_metal_mem_alloc(128);
  if (a == NULL || b == NULL) {
    return -1;
  }
  pm_metal_mem_free(b);
  pm_metal_mem_free(a);

  r = pm_metal_mem_alloc(32);
  if (r == NULL) {
    return -1;
  }
  memset(r, 0xA5, 32);
  r = pm_metal_mem_realloc(r, 256);
  if (r == NULL || r[0] != (uint8_t)0xA5) {
    return -1;
  }
  r = pm_metal_mem_realloc(r, 0);
  if (r != NULL) {
    return -1;
  }

  r = pm_metal_mem_memalign(64, 100);
  if (r == NULL || ((uintptr_t)r % 64u) != 0u) {
    return -1;
  }
  pm_metal_mem_free(r);
  if (pm_metal_mem_memalign(3, 16) != NULL) {
    return -1;
  }

  p0 = pm_metal_mem_map(4096);
  p1 = pm_metal_mem_map(4096);
  if (p0 == NULL || p1 == NULL) {
    return -1;
  }
  if (pm_metal_mem_unmap(p0, 4096) != -1) {
    return -1;
  }
  if (pm_metal_mem_unmap(p1, 4096) != 0 || pm_metal_mem_unmap(p0, 4096) != 0) {
    return -1;
  }

  n_ok = 0;
  for (i = 0; i < 64; i++) {
    a = pm_metal_mem_alloc(8 * 1024);
    if (a == NULL) {
      break;
    }
    n_ok++;
  }
  if (n_ok < 4) {
    return -1;
  }
  return 0;
}

static int32_t phase_zero(uint32_t self_h)
{
  (void)self_h;
  return 1;
}

static int32_t phase_one(uint32_t self_h)
{
  (void)self_h;
  return 2;
}

/** Stress-only: exercise public async APIs (lives under exp2/stress, not src/). */
static int32_t async_smoke(void)
{
  static const pm_metal_async_phase_fn_t ops[2] = { phase_zero, phase_one };
  pm_metal_async_phase_table_t table;
  int32_t pc;
  uint32_t h;

  if (pm_metal_async_ready() == 0 || pm_metal_async_n_runners() == 0u) {
    return -1;
  }

  h = pm_metal_async_sleep_us(500u);
  if (h == 0u || pm_metal_async_create_task(h) == 0u || wait_handle(h, STRESS_TIMEOUT_US) != 0) {
    if (h != 0u) {
      pm_metal_async_coro_close(h);
    }
    return -1;
  }
  pm_metal_async_coro_close(h);

  table.fail = NULL;
  table.end  = NULL;
  table.n    = 2u;
  table.ops  = ops;
  pc         = 0;
  if (pm_metal_async_phase_step(0u, &table, &pc) != PM_METAL_ASYNC_PENDING || pc != 1) {
    return -1;
  }
  if (pm_metal_async_phase_step(0u, &table, &pc) != PM_METAL_ASYNC_DONE || pc != 2) {
    return -1;
  }
  return 0;
}

int32_t pm_metal_exp2_stress(void)
{
  uint32_t h;

  pm_metal_boot_uart_write("stress begin\n", 13u);
  if (mem_smoke() != 0) {
    return stress_fail("mem");
  }
  if (async_smoke() != 0) {
    return stress_fail("async");
  }
  if (pm_metal_boot_tree_print() != 0) {
    return stress_fail("tree");
  }

  h = pm_metal_net_ping("10.0.2.2", 3000u);
  if (h == 0u || pm_metal_async_create_task(h) == 0u || wait_handle(h, STRESS_TIMEOUT_US) != 0) {
    if (h != 0u) {
      pm_metal_async_coro_close(h);
    }
    return stress_fail("ping");
  }
  pm_metal_async_coro_close(h);

  /* Literal short-circuit. */
  h = pm_metal_net_ip_dns("10.0.2.2");
  if (h == 0u || pm_metal_async_create_task(h) == 0u || wait_handle(h, STRESS_TIMEOUT_US) != 0 ||
      pm_metal_async_result_u32(h) != 1u) {
    if (h != 0u) {
      pm_metal_async_coro_close(h);
    }
    return stress_fail("dns");
  }
  pm_metal_async_coro_close(h);

  /* Real UDP DNS via SLIRP (10.0.2.3) — resolve the HTTP stress hostname. */
  h = pm_metal_net_ip_dns("dns.google");
  if (h == 0u || pm_metal_async_create_task(h) == 0u || wait_handle(h, STRESS_TIMEOUT_US) != 0 ||
      pm_metal_async_result_u32(h) != 1u) {
    if (h != 0u) {
      pm_metal_async_coro_close(h);
    }
    return stress_fail("dns-lookup");
  }
  pm_metal_async_coro_close(h);

  {
    static uint8_t http_buf[512];

    /* Host python http.server on 10.0.2.2:18080 (started by scripts/stress). */
    h = pm_metal_net_http_get("http://10.0.2.2:18080/", http_buf, (uint32_t)sizeof(http_buf));
    if (h == 0u || pm_metal_async_create_task(h) == 0u || wait_handle(h, STRESS_TIMEOUT_US) != 0) {
      if (h != 0u) {
        pm_metal_async_coro_close(h);
      }
      return stress_fail("http");
    }
    if (pm_metal_net_http_status(h) < 200u || pm_metal_net_http_status(h) >= 400u) {
      pm_metal_async_coro_close(h);
      return stress_fail("http");
    }
    pm_metal_async_coro_close(h);
  }

  /* Host NTP mock on 10.0.2.2:18123 (PM_METAL_NET_NTP_PORT under EXP2_STRESS). */
  h = pm_metal_net_ntp_sync("10.0.2.2");
  if (h == 0u || pm_metal_async_create_task(h) == 0u || wait_handle(h, STRESS_TIMEOUT_US) != 0 ||
      pm_metal_net_ntp_status(h) != 0u || pm_metal_net_ntp_last_unix_ms() == 0ull) {
    if (h != 0u) {
      pm_metal_async_coro_close(h);
    }
    return stress_fail("ntp");
  }
  pm_metal_async_coro_close(h);

  h = pm_metal_dev_blk_read_async(0u, mStressSector, 1u);
  if (wait_handle(h, STRESS_TIMEOUT_US) != 0 || pm_metal_dev_blk_result(h) != 1u) {
    if (h != 0u) {
      pm_metal_async_coro_close(h);
    }
    return stress_fail("blk");
  }
  pm_metal_async_coro_close(h);

  /*
   * Automated QEMU has no human key source. Prove input parking and timeout
   * completion; an injected PS/2 key would complete the same handle early.
   */
  h = pm_metal_dev_input_wait_key_async(25u);
  if (wait_handle(h, 1000000ull) != 0) {
    if (h != 0u) {
      pm_metal_async_coro_close(h);
    }
    return stress_fail("input");
  }
  (void)pm_metal_dev_input_wait_key_result(h);
  pm_metal_async_coro_close(h);

  pm_metal_boot_uart_write("stress ok\n", 10u);
  return 0;
}

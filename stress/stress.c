#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/phase.h>
#include <pymergetic/metal/async/runner.h>
#include <pymergetic/metal/async/task.h>
#include <pymergetic/metal/async/time.h>
#include "stress.h"
#include <pymergetic/metal/boot/modload/__init__.h>
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

/* Local decls — avoid fragile generated face headers for FS/VFS entry points. */
uint32_t pm_metal_fs_write_async(const uint8_t *path, const uint8_t *src, uint32_t src_len);
uint32_t pm_metal_fs_result(uint32_t h);
int32_t pm_metal_fs_mtar_empty(uint8_t *out, size_t out_cap, size_t *out_len);
int32_t pm_metal_fs_mtar_mount_rw(const uint8_t *target, uint8_t *blob_mut, size_t len,
                                  size_t cap);
int32_t pm_metal_fs_littlefs_format_buf(uint8_t *buf, size_t len);
int32_t pm_metal_fs_littlefs_seed_simple(uint8_t *buf, size_t len, const uint8_t *const *names,
                                         const uint8_t *const *datas, const uint32_t *lens,
                                         uint32_t count);
int32_t pm_metal_fs_littlefs_mount(const uint8_t *target, uint8_t *buf, size_t len);
int32_t pm_metal_vfs_umount(const uint8_t *target);

#define STRESS_TIMEOUT_US 8000000ull
#define ASYNC_BENCH_WAVE 32u
#define ASYNC_BENCH_WAVES 32u
#define ASYNC_BENCH_POLL_ITERS 50000ull

static uint8_t mStressSector[PM_METAL_DEV_BLK_SECTOR_BYTES];

static size_t cstrlen(const char *s)
{
  size_t n = 0u;

  while (s[n] != '\0') {
    n++;
  }
  return n;
}

static void uart_puts(const char *s)
{
  pm_metal_boot_uart_write(s, cstrlen(s));
}

static void uart_u64(uint64_t v)
{
  char buf[20];
  size_t i;
  size_t n;

  if (v == 0ull) {
    pm_metal_boot_uart_write("0", 1u);
    return;
  }
  n = 0u;
  while (v > 0ull && n < sizeof(buf)) {
    buf[n++] = (char)('0' + (uint32_t)(v % 10ull));
    v /= 10ull;
  }
  i = n;
  while (i > 0u) {
    i--;
    pm_metal_boot_uart_write(&buf[i], 1u);
  }
}

static int32_t stress_fail(const char *stage)
{
  uart_puts("stress FAIL ");
  uart_puts(stage);
  uart_puts("\n");
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

static void sort_u64(uint64_t *a, uint32_t n)
{
  uint32_t i;
  uint32_t j;

  for (i = 1u; i < n; i++) {
    uint64_t key = a[i];

    j = i;
    while (j > 0u && a[j - 1u] > key) {
      a[j] = a[j - 1u];
      j--;
    }
    a[j] = key;
  }
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

/** Throughput bench: empty poll_all + sleep(0) ops/s and p99 wait (UART). */
static int32_t async_bench(void)
{
  static uint64_t waits[ASYNC_BENCH_WAVE * ASYNC_BENCH_WAVES];
  uint32_t handles[ASYNC_BENCH_WAVE];
  uint64_t starts[ASYNC_BENCH_WAVE];
  uint64_t t0;
  uint64_t t1;
  uint64_t dt;
  uint64_t poll_ops;
  uint64_t sleep_ops;
  uint64_t p99;
  uint32_t n_waits;
  uint32_t w;
  uint32_t i;
  uint64_t iter;

  t0 = pm_metal_time_mono_us();
  for (iter = 0ull; iter < ASYNC_BENCH_POLL_ITERS; iter++) {
    (void)pm_metal_async_run_poll_all();
  }
  t1 = pm_metal_time_mono_us();
  dt = (t1 > t0) ? (t1 - t0) : 1ull;
  poll_ops = (ASYNC_BENCH_POLL_ITERS * 1000000ull) / dt;

  n_waits = 0u;
  t0 = pm_metal_time_mono_us();
  for (w = 0u; w < ASYNC_BENCH_WAVES; w++) {
    uint32_t left;

    for (i = 0u; i < ASYNC_BENCH_WAVE; i++) {
      starts[i] = pm_metal_time_mono_us();
      handles[i] = pm_metal_async_sleep_us(0ull);
      if (handles[i] == 0u) {
        return -1;
      }
    }
    left = ASYNC_BENCH_WAVE;
    while (left > 0u) {
      (void)pm_metal_async_run_poll_all();
      for (i = 0u; i < ASYNC_BENCH_WAVE; i++) {
        pm_metal_async_status_t st;

        if (handles[i] == 0u) {
          continue;
        }
        st = pm_metal_async_status(handles[i]);
        if (st == PM_METAL_ASYNC_DONE) {
          uint64_t now = pm_metal_time_mono_us();

          waits[n_waits++] = (now >= starts[i]) ? (now - starts[i]) : 0ull;
          pm_metal_async_coro_close(handles[i]);
          handles[i] = 0u;
          left--;
        } else if (st == PM_METAL_ASYNC_ERROR || st == PM_METAL_ASYNC_CANCELLED) {
          return -1;
        }
      }
    }
  }
  t1 = pm_metal_time_mono_us();
  dt = (t1 > t0) ? (t1 - t0) : 1ull;
  sleep_ops = ((uint64_t)n_waits * 1000000ull) / dt;

  if (n_waits == 0u) {
    return -1;
  }
  sort_u64(waits, n_waits);
  p99 = waits[(n_waits * 99u) / 100u];

  uart_puts("async bench poll_all=");
  uart_u64(poll_ops);
  uart_puts("/s sleep0=");
  uart_u64(sleep_ops);
  uart_puts("/s p99_wait_us=");
  uart_u64(p99);
  uart_puts("\n");
  return 0;
}

/** tmpfs write, mtar_rw mount, littlefs seed+mount (exp2/stress only). */
static int32_t fs_smoke(void)
{
  static uint8_t mtar_buf[4096];
  static uint8_t lfs_buf[64 * 1024];
  static const uint8_t payload[] = "stress-fs\n";
  static const uint8_t path_tmp[] = "/tmp/stress.txt";
  static const uint8_t path_mtar[] = "/stress-mtar";
  static const uint8_t path_lfs[] = "/stress-lfs";
  static const uint8_t lfs_name[] = "hello.txt";
  static const uint8_t lfs_data[] = "lfs\n";
  const uint8_t *names[1];
  const uint8_t *datas[1];
  uint32_t lens[1];
  size_t mtar_len;
  uint32_t h;
  uint8_t *owned;

  h = pm_metal_fs_write_async(path_tmp, payload, (uint32_t)(sizeof(payload) - 1u));
  if (h == 0u || wait_handle(h, STRESS_TIMEOUT_US) != 0 ||
      pm_metal_fs_result(h) != (uint32_t)(sizeof(payload) - 1u)) {
    if (h != 0u) {
      pm_metal_async_coro_close(h);
    }
    return -1;
  }
  pm_metal_async_coro_close(h);

  mtar_len = 0u;
  if (pm_metal_fs_mtar_empty(mtar_buf, sizeof(mtar_buf), &mtar_len) != 0 || mtar_len == 0u) {
    return -1;
  }
  owned = pm_metal_mem_alloc(mtar_len + 4096u);
  if (owned == NULL) {
    return -1;
  }
  memset(owned, 0, mtar_len + 4096u);
  {
    size_t i;

    for (i = 0u; i < mtar_len; i++) {
      owned[i] = mtar_buf[i];
    }
  }
  if (pm_metal_fs_mtar_mount_rw(path_mtar, owned, mtar_len, mtar_len + 4096u) != 0) {
    pm_metal_mem_free(owned);
    return -1;
  }
  /* open_owned copies into a Vec; seed buffer is ours again. */
  pm_metal_mem_free(owned);
  (void)pm_metal_vfs_umount(path_mtar);

  names[0] = lfs_name;
  datas[0] = lfs_data;
  lens[0] = (uint32_t)(sizeof(lfs_data) - 1u);
  memset(lfs_buf, 0xff, sizeof(lfs_buf));
  if (pm_metal_fs_littlefs_format_buf(lfs_buf, sizeof(lfs_buf)) != 0) {
    return -1;
  }
  if (pm_metal_fs_littlefs_seed_simple(lfs_buf, sizeof(lfs_buf), names, datas, lens, 1u) != 0) {
    return -1;
  }
  if (pm_metal_fs_littlefs_mount(path_lfs, lfs_buf, sizeof(lfs_buf)) != 0) {
    return -1;
  }
  (void)pm_metal_vfs_umount(path_lfs);
  return 0;
}

/** Exercise pm_metal_boot_mod_load / _unload with an empty mtar. */
static int32_t mod_load_smoke(void)
{
  static uint8_t blob[4096];
  static const uint8_t id[] = "stress";
  size_t len;

  len = 0u;
  if (pm_metal_fs_mtar_empty(blob, sizeof(blob), &len) != 0 || len == 0u) {
    return -1;
  }
  if (pm_metal_boot_mod_load(id, blob, len, NULL, 0u) != 0) {
    return -1;
  }
  if (pm_metal_boot_mod_unload(id) != 0) {
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
  if (async_bench() != 0) {
    return stress_fail("async-bench");
  }
  if (fs_smoke() != 0) {
    return stress_fail("fs");
  }
  if (mod_load_smoke() != 0) {
    return stress_fail("mod-load");
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

  /* Real UDP DNS via SLIRP (10.0.2.3) - resolve the HTTP stress hostname. */
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

    /* Host python http.server on 10.0.2.2:18080 (started by forge stress). */
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

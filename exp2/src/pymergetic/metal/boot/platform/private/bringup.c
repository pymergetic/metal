#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/runner.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/boot/banner.h>
#include <pymergetic/metal/boot/__init__.h>
#include <pymergetic/metal/boot/platform/handoff.h>
#include <pymergetic/metal/boot/platform/mem_map.h>
#include <pymergetic/metal/boot/platform/private/bringup.h>
#include <pymergetic/metal/boot/platform/uart.h>
#if defined(EXP2_STRESS) && EXP2_STRESS
int32_t pm_metal_exp2_stress(void);
#endif
#include <pymergetic/metal/boot/tree/print.h>
#include <pymergetic/metal/console/__init__.h>
#include <pymergetic/metal/dev/acpi/__init__.h>
#include <pymergetic/metal/dev/blk/__init__.h>
#include <pymergetic/metal/dev/serial/__init__.h>
#include <pymergetic/metal/dev/net/__init__.h>
#include <pymergetic/metal/log/__init__.h>
#include <pymergetic/metal/net/ip/__init__.h>
#include <pymergetic/metal/dt/__init__.h>
#include <pymergetic/metal/mem/__init__.h>

void *memset(void *dst, int c, size_t n);

#define MIN_CLAIM_BYTES (2u * 1024u * 1024u)
#define PAGE_SIZE 4096ull

/* Lifetime: static .rodata — DT stores the pointer. */
/* AVAILABLE below 1MiB vs above — classic PC lowmem / highmem split. */
static const uint8_t k_lowmem_compat[] = "lowmem";
static const uint8_t k_highmem_compat[] = "highmem";
static const uint8_t k_heap_compat[] = "heap";
#define HIGHMEM_FLOOR 0x100000ull

static size_t cstrlen(const char *s)
{
  size_t n = 0;

  if (s == NULL) {
    return 0;
  }
  while (s[n] != '\0') {
    n++;
  }
  return n;
}

static void serial_viewport_write(uint8_t *ctx, const uint8_t *s, size_t n)
{
  (void)ctx;
  pm_metal_dev_serial_write(s, n);
}

static const pm_metal_console_viewport_ops_t g_serial_vp = {
  .write = serial_viewport_write,
};

static int32_t fail(const char *msg)
{
  if (msg != NULL) {
    if (pm_metal_log_ready() != 0) {
      pm_metal_log((const uint8_t *)msg);
    } else {
      /* Pre-attach: platform uart ops only (no COM1 constants here). */
      pm_metal_boot_uart_write(msg, cstrlen(msg));
    }
  }
  return -1;
}

static int32_t claim_arena(uint8_t **base_out, size_t *bytes_out)
{
  pm_metal_boot_mem_region_t regs[64];
  uint32_t n = 0;
  uint32_t i;
  uint64_t img_end;
  uint64_t best_addr = 0;
  uint64_t best_len = 0;

  if (pm_metal_boot_mem_map_get(regs, 64u, &n) != 0 || n == 0u) {
    return -1;
  }
  img_end = ((uint64_t)pm_metal_boot_mem_map_image_end() + (PAGE_SIZE - 1ull)) & ~(PAGE_SIZE - 1ull);

  for (i = 0; i < n; i++) {
    uint64_t start;
    uint64_t end;
    uint64_t len;

    if (regs[i].type != (uint32_t)PM_METAL_BOOT_MEM_AVAILABLE) {
      continue;
    }
    if (regs[i].len == 0u) {
      continue;
    }
    end = regs[i].addr + regs[i].len;
    start = regs[i].addr;
    if (start < img_end) {
      start = img_end;
    }
    start = (start + (PAGE_SIZE - 1ull)) & ~(PAGE_SIZE - 1ull);
    if (start >= end) {
      continue;
    }
    len = end - start;
    len &= ~(PAGE_SIZE - 1ull);
    if (len < (uint64_t)MIN_CLAIM_BYTES) {
      continue;
    }
    if (len > best_len) {
      best_addr = start;
      best_len = len;
    }
  }
  if (best_len < (uint64_t)MIN_CLAIM_BYTES || best_addr > (uint64_t)UINTPTR_MAX) {
    return -1;
  }
  if (best_len > (uint64_t)SIZE_MAX) {
    best_len = (uint64_t)SIZE_MAX;
    best_len &= ~(PAGE_SIZE - 1ull);
  }
  *base_out = (uint8_t *)(uintptr_t)best_addr;
  *bytes_out = (size_t)best_len;
  return 0;
}

static int compat_eq(const uint8_t *a, const char *b)
{
  size_t i;

  if (a == NULL || b == NULL) {
    return 0;
  }
  for (i = 0; b[i] != '\0'; i++) {
    if (a[i] != (uint8_t)b[i]) {
      return 0;
    }
  }
  return a[i] == 0;
}

static int32_t seed_mem_partition(uint8_t *arena, size_t bytes)
{
  pm_metal_boot_mem_region_t regs[64];
  uint32_t n = 0;
  uint32_t i;

  if (pm_metal_boot_mem_map_get(regs, 64u, &n) != 0) {
    return -1;
  }
  for (i = 0; i < n; i++) {
    const uint8_t *compat;

    if (regs[i].type != (uint32_t)PM_METAL_BOOT_MEM_AVAILABLE || regs[i].len == 0u) {
      continue;
    }
    compat = (regs[i].addr < HIGHMEM_FLOOR) ? k_lowmem_compat : k_highmem_compat;
    if (pm_metal_dt_seed_mem(compat, 0u, regs[i].addr, regs[i].len) < 0) {
      return -1;
    }
  }
  if (pm_metal_dt_seed_mem(k_heap_compat, (uint32_t)PM_METAL_DT_CAP_BOUND,
                           (uint64_t)(uintptr_t)arena, (uint64_t)bytes)
      < 0) {
    return -1;
  }
  return 0;
}


static void log_virtio_net_mac(const uint8_t mac[6])
{
  static const char hex[] = "0123456789abcdef";
  char              buf[48];
  size_t            p = 0;
  size_t            i;
  const char       *prefix = "virtio-net: mac=";
  const char       *suffix = " open ok\n";

  for (i = 0; prefix[i] != '\0'; i++) {
    buf[p++] = prefix[i];
  }
  for (i = 0; i < 6; i++) {
    if (i > 0) {
      buf[p++] = ':';
    }
    buf[p++] = hex[(mac[i] >> 4) & 0x0fu];
    buf[p++] = hex[mac[i] & 0x0fu];
  }
  for (i = 0; suffix[i] != '\0'; i++) {
    buf[p++] = suffix[i];
  }
  buf[p] = '\0';
  if (pm_metal_log_ready() != 0) {
    pm_metal_log((const uint8_t *)buf);
  } else {
    pm_metal_boot_uart_write(buf, p);
  }
}

static void log_net_dhcp(int32_t ok, const char *ip)
{
  char buf[64];
  size_t p = 0;
  size_t i;
  const char *prefix;
  const char *suffix;

  if (ok != 0 && ip != NULL) {
    prefix = "net: eth0 dhcp ok ";
    suffix = "\n";
    for (i = 0; prefix[i] != '\0'; i++) {
      buf[p++] = prefix[i];
    }
    for (i = 0; ip[i] != '\0' && p + 2 < sizeof(buf); i++) {
      buf[p++] = ip[i];
    }
  } else {
    prefix = "net: eth0 dhcp FAIL\n";
    for (i = 0; prefix[i] != '\0'; i++) {
      buf[p++] = prefix[i];
    }
    suffix = "";
  }
  for (i = 0; suffix[i] != '\0'; i++) {
    buf[p++] = suffix[i];
  }
  buf[p] = '\0';
  if (pm_metal_log_ready() != 0) {
    pm_metal_log((const uint8_t *)buf);
  } else {
    pm_metal_boot_uart_write(buf, p);
  }
}

static int32_t net_dhcp_bringup(void)
{
  static const pm_metal_net_ip_l2_ops_t virtio_l2 = {
    .open = pm_metal_dev_net_virtio_open,
    .mac  = pm_metal_dev_net_virtio_mac,
    .tx   = pm_metal_dev_net_virtio_tx,
    .poll = (pm_metal_net_ip_l2_poll_fn)pm_metal_dev_net_virtio_poll,
  };
  char     ip[16];
  uint64_t start;
  uint64_t deadline;
  int32_t  r;

  /* Composition: L2 driver lives in dev/net; IP only sees ops. */
  if (pm_metal_net_ip_l2_start("lwip+virtio-net", &virtio_l2) != 0) {
    log_net_dhcp(0, NULL);
    return -1;
  }
  (void)pm_metal_net_ip_loopback_start();

  ip[0]    = '\0';
  start    = pm_metal_time_mono_us();
  deadline = start + 10000000ull; /* 10s */
  for (;;) {
    pm_metal_net_ip_poll();
    (void)pm_metal_async_run_poll_all();
    r = pm_metal_net_ip_if_dhcp_ready("eth0", ip, sizeof(ip));
    if (r == 1) {
      log_net_dhcp(1, ip);
      return 0;
    }
    if (r < 0) {
      log_net_dhcp(0, NULL);
      return -1;
    }
    if (pm_metal_time_mono_us() >= deadline) {
      log_net_dhcp(0, NULL);
      return -1;
    }
  }
}


int32_t pm_metal_boot_bringup(void)
{
  uint8_t *arena;
  size_t bytes;
  int32_t uart_id;

  if (claim_arena(&arena, &bytes) != 0) {
    return fail("exp2: claim_arena failed\n");
  }
  if (pm_metal_mem_init(arena, bytes) != 0) {
    return fail("exp2: mem_init failed\n");
  }

  if (pm_metal_console_init0(0) != 0) {
    return fail("exp2: console_init0 failed\n");
  }

  /* log default = console 0 (log facade writes there when ready). */
  pm_metal_boot_banner();

  pm_metal_dt_reset();
  if (seed_mem_partition(arena, bytes) != 0) {
    return fail("exp2: dt mem seed failed\n");
  }
  {
    const uint8_t *compat = pm_metal_boot_uart_floor_compat();
    uint32_t iobase = pm_metal_boot_uart_floor_iobase();
    if (compat == NULL || iobase == 0u) {
      return fail("exp2: uart floor missing\n");
    }
    uart_id = pm_metal_dt_seed_bound_uart(compat, PM_METAL_DT_BUS_ISA, iobase);
    if (uart_id < 0) {
      return fail("exp2: dt uart seed failed\n");
    }
  }

  if (pm_metal_dev_serial_init() != 0) {
    return fail("exp2: serial init failed\n");
  }
  if (pm_metal_console_attach(0u, &g_serial_vp, NULL) != 0) {
    return fail("exp2: console attach failed\n");
  }

  /* TSC calibrate needs EFI Stall — before ExitBootServices. */
  pm_metal_time_init();

  if (pm_metal_boot_leave_firmware() != 0) {
    return fail("exp2: handoff failed\n");
  }

  /* Sticky port cache on EFI; PIT remeasure on BIOS. */
  pm_metal_time_recalibrate();

  if (pm_metal_boot_harvest() != 0) {
    return fail("exp2: harvest failed\n");
  }


  {
    uint32_t bi;
    uint32_t nblk;

    /* Open + bind only; LBA0 hammer lives under ./exp2/scripts/stress. */
    if (pm_metal_dev_blk_open() != 0) {
      return fail("exp2: virtio-blk open failed\n");
    }
    nblk = pm_metal_dt_count_class(PM_METAL_DT_CLASS_BLK);
    for (bi = 0u; bi < nblk; bi++) {
      const DtNode *n = pm_metal_dt_by_class(PM_METAL_DT_CLASS_BLK, bi);
      if (n != NULL && n->compat != NULL && compat_eq(n->compat, "virtio-blk")) {
        (void)pm_metal_dt_or_caps(PM_METAL_DT_CLASS_BLK, bi, (uint32_t)PM_METAL_DT_CAP_BOUND);
        break;
      }
    }
  }

  /* Runners only — opt-in harness lives under ./exp2/scripts/stress. */
  {
    uint32_t n_cpus = pm_metal_dev_acpi_cpu_count();
    if (n_cpus == 0u) {
      n_cpus = 1u;
    }
    if (pm_metal_async_start(n_cpus) != 0) {
      return fail("exp2: async start failed\n");
    }
  }

  if (net_dhcp_bringup() != 0) {
    return fail("exp2: net dhcp failed\n");
  }
  {
    const uint8_t *mac;
    uint32_t ni;
    uint32_t nnet;

    mac = pm_metal_dev_net_virtio_mac();
    if (mac != NULL) {
      log_virtio_net_mac(mac);
    }
    nnet = pm_metal_dt_count_class(PM_METAL_DT_CLASS_NET);
    for (ni = 0; ni < nnet; ni++) {
      const DtNode *n = pm_metal_dt_by_class(PM_METAL_DT_CLASS_NET, ni);
      if (n != NULL && n->compat != NULL && compat_eq(n->compat, "virtio-net")) {
        (void)pm_metal_dt_or_caps(PM_METAL_DT_CLASS_NET, ni, (uint32_t)PM_METAL_DT_CAP_BOUND);
        break;
      }
    }
  }

  if (pm_metal_boot_tree_print() != 0) {
    return fail("exp2: boot tree failed\n");
  }

#if defined(EXP2_STRESS) && EXP2_STRESS
  if (pm_metal_exp2_stress() != 0) {
    return -1;
  }
#endif

  return 0;
}

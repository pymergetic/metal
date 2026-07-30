#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/boot/platform/handoff.h>
#include <pymergetic/metal/boot/platform/mem_map.h>
#include <pymergetic/metal/boot/platform/private/bringup.h>
#include <pymergetic/metal/console/__init__.h>
#include <pymergetic/metal/dev/serial/__init__.h>
#include <pymergetic/metal/dt/__init__.h>
#include <pymergetic/metal/mem/__init__.h>

void *memset(void *dst, int c, size_t n);

#define COM1_IOBASE 0x3F8u
#define MIN_CLAIM_BYTES (2u * 1024u * 1024u)
#define PAGE_SIZE 4096ull

/* Lifetime: static .rodata — DT stores the pointer. */
static const uint8_t k_com1_compat[] = "com1";
static const uint8_t k_sysmem_compat[] = "sysmem";
static const uint8_t k_heap_compat[] = "heap";

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

static void puts_console(const char *s)
{
  pm_metal_console_write(0u, (const uint8_t *)s, cstrlen(s));
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
  if (msg != NULL && pm_metal_console_ready() != 0) {
    puts_console(msg);
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
    if (regs[i].type != (uint32_t)PM_METAL_BOOT_MEM_AVAILABLE || regs[i].len == 0u) {
      continue;
    }
    if (pm_metal_dt_seed_mem(k_sysmem_compat, 0u, regs[i].addr, regs[i].len) < 0) {
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

static int32_t dt_smoke(uint8_t *arena, size_t bytes)
{
  const DtNode *n;
  uint32_t i;
  uint32_t mem_n;
  int saw_heap = 0;

  if (pm_metal_dt_count_class(PM_METAL_DT_CLASS_STREAM) != 1u) {
    return -1;
  }
  if (pm_metal_dt_uart_bound(COM1_IOBASE) != 1) {
    return -1;
  }
  n = pm_metal_dt_lookup(PM_METAL_DT_CLASS_STREAM);
  if (n == NULL) {
    return -1;
  }
  if ((n->caps & (uint32_t)PM_METAL_DT_CAP_BOUND) == 0u) {
    return -1;
  }
  if (n->bus != PM_METAL_DT_BUS_ISA || n->loc[0] != COM1_IOBASE) {
    return -1;
  }

  mem_n = pm_metal_dt_count_class(PM_METAL_DT_CLASS_MEM);
  if (mem_n < 1u) {
    return -1;
  }
  for (i = 0; i < mem_n; i++) {
    uint64_t base;
    uint64_t size;

    n = pm_metal_dt_by_class(PM_METAL_DT_CLASS_MEM, i);
    if (n == NULL || n->bus != PM_METAL_DT_BUS_PLATFORM) {
      return -1;
    }
    base = ((uint64_t)n->loc[1] << 32) | (uint64_t)n->loc[0];
    size = ((uint64_t)n->loc[3] << 32) | (uint64_t)n->loc[2];
    if (size == 0u) {
      return -1;
    }
    if ((n->caps & (uint32_t)PM_METAL_DT_CAP_BOUND) != 0u && compat_eq(n->compat, "heap")) {
      if (base != (uint64_t)(uintptr_t)arena || size != (uint64_t)bytes) {
        return -1;
      }
      saw_heap = 1;
    }
  }
  if (!saw_heap) {
    return -1;
  }
  return 0;
}

static int32_t mem_smoke(void)
{
  uint8_t *a;
  uint8_t *b;
  uint8_t *r;
  uint8_t *p0;
  uint8_t *p1;
  int i;
  int n_ok;

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

int32_t pm_metal_boot_bringup(void)
{
  uint8_t *arena;
  size_t bytes;
  int32_t uart_id;

  if (claim_arena(&arena, &bytes) != 0) {
    return fail(NULL);
  }
  if (pm_metal_mem_init(arena, bytes) != 0) {
    return fail(NULL);
  }

  if (pm_metal_console_init0(0) != 0) {
    return fail(NULL);
  }

  puts_console("exp2: hello\n");
  puts_console("mem: arena ready\n");
  puts_console("console: #0 ready\n");

  pm_metal_dt_reset();
  if (seed_mem_partition(arena, bytes) != 0) {
    return fail("exp2: dt mem seed failed\n");
  }
  uart_id = pm_metal_dt_seed_bound_uart(k_com1_compat, PM_METAL_DT_BUS_ISA, COM1_IOBASE);
  if (uart_id < 0 || dt_smoke(arena, bytes) != 0) {
    return fail("exp2: dt seed failed\n");
  }

  if (pm_metal_dev_serial_init() != 0) {
    return fail("exp2: serial init failed\n");
  }
  if (pm_metal_console_attach(0u, &g_serial_vp, NULL) != 0) {
    return fail("exp2: console attach failed\n");
  }

  if (pm_metal_boot_leave_firmware() != 0) {
    return fail("exp2: handoff failed\n");
  }

  if (mem_smoke() != 0) {
    return fail("exp2: mem smoke failed\n");
  }

  puts_console("mem: PASS\n");
  return 0;
}

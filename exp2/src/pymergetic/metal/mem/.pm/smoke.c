/* Host C ABI smoke — metal mod test (links host libmetal_mem). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "__init__.h"

#define ARENA_BYTES (512u * 1024u)
#define ARENA_ALIGN 4096u

int main(void)
{
  uint8_t *raw;
  uint8_t *base;
  uint8_t *a;
  uint8_t *b;
  uint8_t *p0;
  uint8_t *p1;
  uint8_t *r;
  int i;
  int n_ok;

  /*
   * Host-only seed buffer (fake). On iron this range comes from
   * BIOS/UEFI/DT RAM — not from pm_metal_mem_alloc (needs init first).
   * Plain malloc + align — no C11/POSIX feature macros (clangd-safe).
   */
  raw = (uint8_t *)malloc(ARENA_BYTES + ARENA_ALIGN);
  if (raw == NULL) {
    fprintf(stderr, "mem .pm/smoke.c: malloc failed\n");
    return 1;
  }
  base = (uint8_t *)(((uintptr_t)raw + (ARENA_ALIGN - 1u)) & ~(uintptr_t)(ARENA_ALIGN - 1u));
  if (pm_metal_mem_init(base, ARENA_BYTES) != 0) {
    fprintf(stderr, "mem .pm/smoke.c: init failed\n");
    free(raw);
    return 1;
  }

  a = pm_metal_mem_alloc(64);
  b = pm_metal_mem_alloc(128);
  if (a == NULL || b == NULL) {
    fprintf(stderr, "mem .pm/smoke.c: alloc failed\n");
    return 1;
  }
  pm_metal_mem_free(b);
  pm_metal_mem_free(a);

  r = pm_metal_mem_alloc(32);
  if (r == NULL) {
    return 1;
  }
  memset(r, 0xA5, 32);
  r = pm_metal_mem_realloc(r, 256);
  if (r == NULL || r[0] != (uint8_t)0xA5) {
    fprintf(stderr, "mem .pm/smoke.c: realloc failed\n");
    return 1;
  }
  r = pm_metal_mem_realloc(r, 0);
  if (r != NULL) {
    return 1;
  }

  r = pm_metal_mem_memalign(64, 100);
  if (r == NULL || ((uintptr_t)r % 64u) != 0) {
    fprintf(stderr, "mem .pm/smoke.c: memalign failed\n");
    return 1;
  }
  pm_metal_mem_free(r);
  if (pm_metal_mem_memalign(3, 16) != NULL) {
    fprintf(stderr, "mem .pm/smoke.c: memalign bad align accepted\n");
    return 1;
  }

  p0 = pm_metal_mem_map(4096);
  p1 = pm_metal_mem_map(4096);
  if (p0 == NULL || p1 == NULL) {
    return 1;
  }
  if (pm_metal_mem_unmap(p0, 4096) != -1) {
    fprintf(stderr, "mem .pm/smoke.c: LIFO expected fail\n");
    return 1;
  }
  if (pm_metal_mem_unmap(p1, 4096) != 0 || pm_metal_mem_unmap(p0, 4096) != 0) {
    fprintf(stderr, "mem .pm/smoke.c: LIFO unmap failed\n");
    return 1;
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
    fprintf(stderr, "mem .pm/smoke.c: pressure too weak (%d)\n", n_ok);
    return 1;
  }

  free(raw);
  printf("mem .pm/smoke.c: PASS\n");
  return 0;
}

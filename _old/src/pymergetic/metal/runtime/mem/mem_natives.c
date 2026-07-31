/** @file
  WASI over pm_metal_mem_alloc / free / copy_* — guest cookies, host TLSF.
**/
#include "mem_internal.h"

#include <pymergetic/metal/log/log.h>

#include <stdint.h>
#include <string.h>

#include "wasm_export.h"

#ifndef PM_METAL_MEM_GUEST_MAX
#define PM_METAL_MEM_GUEST_MAX 256u
#endif

typedef struct {
  int32_t  used;
  void    *ptr;
  uint32_t bytes;
} mem_guest_slot_t;

static mem_guest_slot_t mGuestSlots[PM_METAL_MEM_GUEST_MAX + 1];

static uint32_t MemGuestAllocSlot(void)
{
  uint32_t i;

  for (i = 1; i <= PM_METAL_MEM_GUEST_MAX; i++) {
    if (!mGuestSlots[i].used) {
      memset(&mGuestSlots[i], 0, sizeof(mGuestSlots[i]));
      mGuestSlots[i].used = 1;
      return i;
    }
  }

  return 0;
}

static mem_guest_slot_t *MemGuestGet(uint32_t h)
{
  if (h == 0 || h > PM_METAL_MEM_GUEST_MAX) {
    return NULL;
  }

  if (!mGuestSlots[h].used || mGuestSlots[h].ptr == NULL) {
    return NULL;
  }

  return &mGuestSlots[h];
}

pm_metal_ptr_t pm_metal_mem_guest_ptr(pm_metal_ptr_t cookie)
{
  mem_guest_slot_t *s;

  s = MemGuestGet((uint32_t)(uintptr_t)cookie);
  return (s != NULL) ? s->ptr : NULL;
}

uint32_t pm_metal_mem_guest_size(pm_metal_ptr_t cookie)
{
  mem_guest_slot_t *s;

  s = MemGuestGet((uint32_t)(uintptr_t)cookie);
  return (s != NULL) ? s->bytes : 0u;
}

static int32_t MemCopyGuestLinear(wasm_exec_env_t exec_env,
                                  uint32_t        off,
                                  uint32_t        n,
                                  void          **native_out)
{
  wasm_module_inst_t inst;
  void              *native;

  *native_out = NULL;
  if (exec_env == NULL || off == 0 || n == 0) {
    return -1;
  }

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL) {
    return -1;
  }

  if (!wasm_runtime_validate_app_addr(inst, (uint64_t)off, n)) {
    return -1;
  }

  native = wasm_runtime_addr_app_to_native(inst, (uint64_t)off);
  if (native == NULL) {
    return -1;
  }

  *native_out = native;
  return 0;
}

int32_t pm_metal_mem_copy_out(pm_metal_ptr_t src, uint32_t dest, uint32_t n)
{
  (void)src;
  (void)dest;
  (void)n;
  /* Host call without exec_env — use native WASI path from guest. */
  return -1;
}

int32_t pm_metal_mem_copy_out_at(pm_metal_ptr_t src, uint32_t src_off, uint32_t dest, uint32_t n)
{
  (void)src;
  (void)src_off;
  (void)dest;
  (void)n;
  return -1;
}

int32_t pm_metal_mem_copy_in(pm_metal_ptr_t dest, uint32_t src, uint32_t n)
{
  (void)dest;
  (void)src;
  (void)n;
  return -1;
}

static uint32_t pm_metal_mem_alloc_native(wasm_exec_env_t exec_env,
                                          uint32_t        size,
                                          uint32_t        where,
                                          uint32_t        id)
{
  uint32_t h;
  void    *p;

  (void)exec_env;
  if (size == 0) {
    return 0;
  }

  h = MemGuestAllocSlot();
  if (h == 0) {
    pm_metal_log("metal-mem: guest cookie table full");
    return 0;
  }

  p = pm_metal_mem_alloc((size_t)size, (pm_metal_mem_flags_t)where, (pm_metal_mem_id_t)id);
  if (p == NULL) {
    mGuestSlots[h].used = 0;
    return 0;
  }

  memset(p, 0, (size_t)size);
  mGuestSlots[h].ptr   = p;
  mGuestSlots[h].bytes = size;
  return h;
}

static void pm_metal_mem_free_native(wasm_exec_env_t exec_env, uint32_t cookie)
{
  mem_guest_slot_t *s;

  (void)exec_env;
  s = MemGuestGet(cookie);
  if (s == NULL) {
    return;
  }

  pm_metal_mem_free(s->ptr);
  memset(s, 0, sizeof(*s));
}

static int32_t pm_metal_mem_copy_out_at_native(
  wasm_exec_env_t exec_env, uint32_t src_cookie, uint32_t src_off, uint32_t dest, uint32_t n)
{
  mem_guest_slot_t *s;
  void             *linear;
  uint64_t          end;

  s = MemGuestGet(src_cookie);
  if (s == NULL || n == 0) {
    return -1;
  }

  end = (uint64_t)src_off + (uint64_t)n;
  if (end > (uint64_t)s->bytes) {
    return -1;
  }

  if (MemCopyGuestLinear(exec_env, dest, n, &linear) != 0) {
    return -1;
  }

  memcpy(linear, (const uint8_t *)s->ptr + src_off, n);
  return 0;
}

static int32_t pm_metal_mem_copy_out_native(wasm_exec_env_t exec_env,
                                            uint32_t        src_cookie,
                                            uint32_t        dest,
                                            uint32_t        n)
{
  return pm_metal_mem_copy_out_at_native(exec_env, src_cookie, 0u, dest, n);
}

static int32_t pm_metal_mem_copy_in_native(wasm_exec_env_t exec_env,
                                           uint32_t        dest_cookie,
                                           uint32_t        src,
                                           uint32_t        n)
{
  mem_guest_slot_t *s;
  void             *linear;

  s = MemGuestGet(dest_cookie);
  if (s == NULL || n == 0 || n > s->bytes) {
    return -1;
  }

  if (MemCopyGuestLinear(exec_env, src, n, &linear) != 0) {
    return -1;
  }

  memcpy(s->ptr, linear, n);
  return 0;
}

static NativeSymbol g_pm_metal_mem_native_symbols[] = {
  { "pm_metal_mem_alloc", (void *)pm_metal_mem_alloc_native, "(iii)i", NULL },
  { "pm_metal_mem_free", (void *)pm_metal_mem_free_native, "(i)", NULL },
  { "pm_metal_mem_copy_out", (void *)pm_metal_mem_copy_out_native, "(iii)i", NULL },
  { "pm_metal_mem_copy_out_at", (void *)pm_metal_mem_copy_out_at_native, "(iiii)i", NULL },
  { "pm_metal_mem_copy_in", (void *)pm_metal_mem_copy_in_native, "(iii)i", NULL },
};

int pm_metal_mem_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_MEM_WASI_MODULE,
                                     g_pm_metal_mem_native_symbols,
                                     sizeof(g_pm_metal_mem_native_symbols) /
                                       sizeof(g_pm_metal_mem_native_symbols[0]))) {
    return -1;
  }

  return 0;
}

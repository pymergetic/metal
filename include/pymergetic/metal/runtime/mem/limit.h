/*
 * Compile-time memory / buffer limit catalog — linker section of
 * module + name + value rows. Parallel to boot/externals; lookup id is
 * "module.name" (e.g. net.asgi.ASGI_IO_MAX). Register with
 * PM_METAL_MEM_LIMIT beside the owning TU (or limit_seed.c).
 *
 * impl: common — src/pymergetic/metal/runtime/mem/limit.c
 */
#ifndef PYMERGETIC_METAL_RUNTIME_MEM_LIMIT_H_
#define PYMERGETIC_METAL_RUNTIME_MEM_LIMIT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_MEM_LIMIT_WASI_MODULE "pymergetic.metal.mem.limit"

/**
 * One registered limit. String pointers are host static literals (or NULL
 * for optional note). id is "module.name" for find / HTTP.
 */
typedef struct pm_metal_mem_limit {
  const char *id;
  const char *module;
  const char *name;
  uint64_t    value;
  const char *unit;
  const char *note;
} pm_metal_mem_limit_t;

#if !defined(__wasm__)

/** One contribution — always 16 bytes so section walk is LTO-safe. */
typedef struct pm_metal_mem_limit_table {
  const pm_metal_mem_limit_t *limits;
  uint32_t                    count;
  uint8_t                     _pad[16u - sizeof(void *) - sizeof(uint32_t)];
} pm_metal_mem_limit_table_t;

/**
 * Place one limit in `.pm_metal_mem_limits.<var>` (SORT_BY_NAME).
 * `module` and `name` must be string literals; id becomes module "." name.
 * `var` should be `g_pm_metal_lim_<...>`.
 */
#define PM_METAL_MEM_LIMIT(var, module, name, value, unit, note)                               \
  static const pm_metal_mem_limit_t       var##_row = { module "." name,   (module), (name),   \
                                                        (uint64_t)(value), (unit),   (note) }; \
  static const pm_metal_mem_limit_table_t var                                                  \
    __attribute__((used, section(".pm_metal_mem_limits." #var), aligned(16))) = { &var##_row, 1u }

extern const pm_metal_mem_limit_table_t __pm_metal_mem_limits_start[];
extern const pm_metal_mem_limit_table_t __pm_metal_mem_limits_end[];

uint32_t pm_metal_mem_limit_count(void);
int32_t  pm_metal_mem_limit_get(uint32_t idx, pm_metal_mem_limit_t *out);
int32_t  pm_metal_mem_limit_find(const char *id, pm_metal_mem_limit_t *out);
int32_t pm_metal_mem_limit_find_mn(const char *module, const char *name, pm_metal_mem_limit_t *out);

int pm_metal_mem_limit_native_register(void);

#else /* __wasm__ */

#include "pymergetic/metal/wasi.h"

#define PM_METAL_MEM_LIMIT_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_MEM_LIMIT_WASI_MODULE, name)

typedef struct pm_metal_mem_limit_info {
  char     id[64];
  char     module[32];
  char     name[48];
  uint64_t value;
  char     unit[16];
  char     note[96];
} pm_metal_mem_limit_info_t;

#define PM_METAL_MEM_LIMIT_IO_PTR(p) ((uint32_t)(uintptr_t)(p))

extern uint32_t pm_metal_mem_limit_count(void) PM_METAL_MEM_LIMIT_IMPORT(pm_metal_mem_limit_count);

extern int32_t pm_metal_mem_limit_get(uint32_t idx, uint32_t out)
  PM_METAL_MEM_LIMIT_IMPORT(pm_metal_mem_limit_get);

extern int32_t pm_metal_mem_limit_find(const char *id, uint32_t out)
  PM_METAL_MEM_LIMIT_IMPORT(pm_metal_mem_limit_find);

extern int32_t pm_metal_mem_limit_find_mn(const char *module, const char *name, uint32_t out)
  PM_METAL_MEM_LIMIT_IMPORT(pm_metal_mem_limit_find_mn);

#endif /* __wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_RUNTIME_MEM_LIMIT_H_ */

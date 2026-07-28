/*
 * Third-party "externals" registry — compile-time linker section of
 * vendored stack identity rows (id / version / url / note). Parallel to
 * authors/about (Metal's own identity stays there); not the mod registry,
 * not shell cmds, not Python modules. Self-register with PM_METAL_EXTERNAL
 * beside the owning glue TU; omitted ports simply omit the row.
 *
 * impl: common — src/pymergetic/metal/boot/externals.c
 */
#ifndef PYMERGETIC_METAL_BOOT_EXTERNALS_H_
#define PYMERGETIC_METAL_BOOT_EXTERNALS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_EXTERNALS_WASI_MODULE "pymergetic.metal.externals"

/**
 * One registered external. String pointers are host static literals (or
 * NULL for optional url/note). version may be "" if unknown.
 */
typedef struct pm_metal_external {
  const char *id;
  const char *version;
  const char *url;
  const char *note;
} pm_metal_external_t;

#if !defined(__wasm__)

/** One module's contribution — always 16 bytes so section walk is LTO-safe. */
typedef struct pm_metal_external_table {
  const pm_metal_external_t *exts;
  uint32_t                   count;
  uint8_t                    _pad[16u - sizeof(void *) - sizeof(uint32_t)];
} pm_metal_external_table_t;

/**
 * Place one external in `.pm_metal_externals.<id>` (SORT_BY_NAME ->
 * alphabetical by id). `var` should be `g_pm_metal_ext_<id>`; `id` is an
 * unquoted token (becomes both the string key and the section suffix).
 */
#define PM_METAL_EXTERNAL(var, id, version, url, note)                         \
  static const pm_metal_external_t var##_row = { #id, (version), (url),        \
                                                 (note) };                     \
  static const pm_metal_external_table_t var                                   \
    __attribute__((used, section(".pm_metal_externals." #id), aligned(16))) = { \
      &var##_row, 1u                                                           \
    }

/** Section bounds — walked only by externals.c. */
extern const pm_metal_external_table_t __pm_metal_externals_start[];
extern const pm_metal_external_table_t __pm_metal_externals_end[];

uint32_t pm_metal_external_count(void);
int32_t  pm_metal_external_get(uint32_t idx, pm_metal_external_t *out);
int32_t  pm_metal_external_find(const char *id, pm_metal_external_t *out);

/**
 * Runtime (guest Python / host) registration — copies strings into a small
 * dyn table appended after the linker-section rows. Same id updates in place.
 * url/note may be NULL or "". Returns 0 ok, -1 full/invalid.
 */
int32_t pm_metal_external_register(const char *id,
                                   const char *version,
                                   const char *url,
                                   const char *note);

int pm_metal_externals_native_register(void);

#else /* __wasm__ */

#include "pymergetic/metal/wasi.h"

#define PM_METAL_EXTERNALS_IMPORT(name) \
  PM_METAL_WASI_IMPORT(PM_METAL_EXTERNALS_WASI_MODULE, name)

/**
 * Guest out-buffer for get/find — string fields copied from host statics
 * (pointer-form pm_metal_external_t is not wasm-safe).
 */
typedef struct pm_metal_external_info {
  char id[32];
  char version[48];
  char url[96];
  char note[96];
} pm_metal_external_info_t;

#define PM_METAL_EXTERNAL_IO_PTR(p) ((uint32_t)(uintptr_t)(p))

extern uint32_t pm_metal_external_count(void) PM_METAL_EXTERNALS_IMPORT(pm_metal_external_count);

extern int32_t pm_metal_external_get(uint32_t idx, uint32_t out)
  PM_METAL_EXTERNALS_IMPORT(pm_metal_external_get);

extern int32_t pm_metal_external_find(const char *id, uint32_t out)
  PM_METAL_EXTERNALS_IMPORT(pm_metal_external_find);

#endif /* __wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_EXTERNALS_H_ */

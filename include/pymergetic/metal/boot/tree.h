/*
 * Live boot tree — emit as stages complete (not a post-mortem dump).
 */
#ifndef PYMERGETIC_METAL_BOOT_TREE_H_
#define PYMERGETIC_METAL_BOOT_TREE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pm_metal_boot_tree_status {
    PM_METAL_BOOT_TREE_OK = 0,
    PM_METAL_BOOT_TREE_WARN = 1,
    PM_METAL_BOOT_TREE_FAIL = 2,
    PM_METAL_BOOT_TREE_DIM = 3,
    PM_METAL_BOOT_TREE_SIM = 4,
} pm_metal_boot_tree_status_t;

typedef void (*pm_metal_boot_print_fn)(const char *line, void *user);

void pm_metal_boot_set_print(pm_metal_boot_print_fn fn, void *user);
void pm_metal_boot_emit(const char *line);

/* Start a live session (banner). */
void pm_metal_boot_banner(const char *version, const char *cpu);

/* Section: enter → items (buffered) → leave flushes that section live.
 * Nested enter flushes pending siblings first (dual-span area under mem). */
void pm_metal_boot_tree_reset(void);
void pm_metal_boot_tree_enter(const char *name);
void pm_metal_boot_tree_enter_ex(
    const char *name, pm_metal_boot_tree_status_t st, const char *detail);
void pm_metal_boot_tree_item(
    const char *name, pm_metal_boot_tree_status_t st, const char *detail);
void pm_metal_boot_tree_leave(void);

void pm_metal_boot_tree_ready_ok(void);
/* Rainbow FIGlet + stamp line (ver @ cpu) — mirrors boot_banner. */
void pm_metal_boot_rainbow_metalpython(const char *version, const char *cpu);

/* Deprecated: was a full dump. Prefer live enter/item/leave. */
int pm_metal_boot_tree_print(void);

#ifdef __cplusplus
}
#endif

#endif

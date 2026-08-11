/*
 * Static RegMod lifecycle (Rust kernel) — C face.
 *
 * **FROZEN / transitional façade.** Module identity SoT is µPy
 * `sys.modules` via `pm_mod_publish` / `pm_mod_connect_import`
 * (`extmod/wasmmod/include/pm_mod.h`). Do not grow new RegMod ring
 * features — publish exports onto the µPy module and connect soft
 * imports there. This header remains so existing floor muscles keep
 * compiling while the ring is starved and deleted.
 *
 * Indexes stay inside the owning TU (named enums). Outside resolves by
 * string name only — never peer enums / naked export indexes.
 *
 * Exports-only (usual):
 *
 *   enum {
 *       PM_METAL_NET_SSH_EXPORT_INIT = 0,
 *       PM_METAL_NET_SSH_EXPORT_LISTEN,
 *       ...
 *   };
 *   static pm_metal_reg_export_t net_ssh_exports[] = {
 *       PM_METAL_REG_EXPORT(init),
 *       PM_METAL_REG_EXPORT(listen),
 *       ...
 *   };
 *   PM_METAL_REG_MOD(net_ssh, "pymergetic.metal.net.ssh")
 *   static int32_t net_ssh_register_symbols(void *ctx) { ... }
 *
 * With soft imports: write pm_metal_reg_mod_desc_t by hand (imports != NULL);
 * PM_METAL_REG_MOD is exports-only.
 *
 * Naming: PM_METAL_<PATH>_{EXPORT|IMPORT}_<NAME>
 * (path after pymergetic.metal. — net.ssh → NET_SSH).
 *
 * Floor load calls pm_metal_*_reg_load().
 */
#ifndef PYMERGETIC_METAL_REG_MOD_H_
#define PYMERGETIC_METAL_REG_MOD_H_

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/reg/ledger.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*pm_metal_reg_hook_fn)(void *ctx);

/** One export slot filled by register_symbols (short name + fn ptr). */
typedef struct pm_metal_reg_export {
    const char *name;
    void *ptr;
} pm_metal_reg_export_t;

/** One soft import: peer module + func (resolved by connect_all). */
typedef struct pm_metal_reg_import {
    const char *module;
    const char *func;
} pm_metal_reg_import_t;

/**
 * C-side module descriptor. Rust builds a real RegMod from this
 * (names must be static / immortal C strings).
 */
typedef struct pm_metal_reg_mod_desc {
    const char *name;
    pm_metal_reg_export_t *exports;
    uint32_t n_exports;
    pm_metal_reg_import_t *imports;
    uint32_t n_imports;
    pm_metal_reg_hook_fn register_symbols;
    void *ctx;
    uint8_t lang; /* PM_METAL_REG_LANG_* */
} pm_metal_reg_mod_desc_t;

static inline void pm_metal_reg_export_publish(pm_metal_reg_export_t *e, void *fn)
{
    if (e != NULL) {
        e->ptr = fn;
    }
}

static inline void *pm_metal_reg_export_get(const pm_metal_reg_export_t *e)
{
    return e != NULL ? e->ptr : NULL;
}

/** Load one Rust RegMod (opaque). 0 success, -1 fail. */
int32_t pm_metal_reg_mod_load(const void *m);
/** Load one C descriptor into the RegMod ring. Idempotent on name. */
int32_t pm_metal_reg_mod_load_c(pm_metal_reg_mod_desc_t *d);
int32_t pm_metal_reg_mod_unload(const uint8_t *name);
void pm_metal_reg_mod_connect_all(void);
uint32_t pm_metal_reg_mod_count(void);

/**
 * Load all permanently-linked floor RegMods. Idempotent per module.
 * Returns 0 if every load succeeded (or was already loaded).
 */
int32_t pm_metal_reg_floor_load(void);

/** One table row: short name stringified from the token. */
#define PM_METAL_REG_EXPORT(name_) \
    {                              \
        .name = #name_, .ptr = NULL  \
    }

/**
 * Optional alias → exports[idx]. Prefer module enums
 * `PM_METAL_<PATH>_EXPORT_*` at call sites instead of this macro.
 */
#define PM_METAL_REG_REF(sym, name_, idx) \
    static pm_metal_reg_export_t *const sym##_##name_ = &sym##_exports[(idx)]

/**
 * Wire sym_exports[] + sym_register_symbols → pm_metal_sym_reg_load().
 * Exports-only (imports = NULL). With imports, write desc by hand (see
 * file header). Define sym_exports and sym_register_symbols first.
 */
#define PM_METAL_REG_MOD(sym, full_name)                        \
    static int32_t sym##_register_symbols(void *ctx);           \
    static pm_metal_reg_mod_desc_t sym##_desc = {                \
        .name = (full_name),                                    \
        .exports = sym##_exports,                               \
        .n_exports = (uint32_t)(sizeof(sym##_exports) /         \
                                sizeof(sym##_exports[0])),      \
        .imports = NULL,                                        \
        .n_imports = 0u,                                        \
        .register_symbols = sym##_register_symbols,             \
        .ctx = NULL,                                            \
        .lang = PM_METAL_REG_LANG_C,                            \
    };                                                          \
    int32_t pm_metal_##sym##_reg_load(void)                     \
    {                                                           \
        return pm_metal_reg_mod_load_c(&sym##_desc);            \
    }

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_REG_MOD_H_ */

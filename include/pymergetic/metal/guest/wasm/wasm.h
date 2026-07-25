/*
 * WAMR engine — host-only load/instantiate primitives.
 *
 * Not the product mod API. Product surface:
 *   include/pymergetic/metal/guest/mod/mod.h     — load / cmd → process
 *   include/pymergetic/metal/guest/process/process.h
 *
 * impl: src/pymergetic/metal/guest/wasm/wasm.c (+ embed_mods.inc.c)
 *       src/pymergetic/metal/guest/wamr/ (efi_*.c)
 */
#ifndef PYMERGETIC_METAL_GUEST_WASM_WASM_H_
#define PYMERGETIC_METAL_GUEST_WASM_WASM_H_

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/shell/ui/ui.h> /* IWYU pragma: keep */

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_wasm_mod_image {
  void    *module;   /* wasm_module_t */
  void    *inst;     /* wasm_module_inst_t */
  void    *exec_env; /* wasm_exec_env_t */
  uint8_t *copy;
  char     name[64];
} pm_metal_wasm_mod_image_t;

/** Init WAMR pool + register Metal natives. Returns 0 ok. */
int pm_metal_wasm_init(void);

void pm_metal_wasm_shutdown(void);

int pm_metal_wasm_ready(void);

/**
 * Bind WASI stdout/stderr for the next/current run to this UI tab handle.
 * Pass PM_METAL_UI_HANDLE_INVALID to clear.
 */
void pm_metal_wasm_set_stdout_tab(pm_metal_ui_handle_t tab);

pm_metal_ui_handle_t pm_metal_wasm_stdout_tab(void);

/**
 * Resolve embed or ESP bytes for name (AOT then wasm).
 * On success: *bytes valid; if *esp_owned != NULL caller frees with
 * pm_metal_mem_free (ESP path). Embed path leaves *esp_owned NULL.
 */
int pm_metal_wasm_mod_fetch(const char     *name,
                            const uint8_t **bytes,
                            uint32_t       *len,
                            uint8_t       **esp_owned);

/**
 * Load+instantiate+bind+exec_env. No magic export lookup.
 * Does not call on_load or reserve a process.
 */
int pm_metal_wasm_mod_image_open(const char                *name,
                                 const uint8_t             *bytes,
                                 uint32_t                   len,
                                 pm_metal_wasm_mod_image_t *out);

void pm_metal_wasm_mod_image_close(pm_metal_wasm_mod_image_t *img);

/**
 * Fresh instance + exec_env sharing an already-loaded module (module
 * "0th instance" stays owned by its loader — e.g. mod.c's registry slot).
 * Use for a private, isolated per-process WASM heap/globals ("give this
 * run its own instance") without reloading/recompiling the module.
 * out->module/copy are non-owning aliases; out->inst/exec_env are owned
 * by the caller — release with pm_metal_wasm_mod_image_deinstantiate.
 */
int pm_metal_wasm_mod_image_instantiate(void                      *module,
                                        const char                *name,
                                        pm_metal_wasm_mod_image_t *out);

/**
 * Tear down a pm_metal_wasm_mod_image_instantiate() result: inst +
 * exec_env only. Does not unload the module or free copy bytes (owned
 * by the loader, not this instance).
 */
void pm_metal_wasm_mod_image_deinstantiate(pm_metal_wasm_mod_image_t *img);

/**
 * Call guest export `name` with signature ()i. Sets *result_out from return.
 * 0 ok, -1 missing/fail.
 */
int pm_metal_wasm_mod_image_call0(pm_metal_wasm_mod_image_t *img,
                                  const char                *name,
                                  int32_t                   *result_out);

/**
 * Look up export on image instance. NULL if missing.
 * Returned pointer is a wasm_function_inst_t.
 */
void *pm_metal_wasm_mod_image_lookup(pm_metal_wasm_mod_image_t *img, const char *export_name);

/**
 * Begin async session, adopt root_coro as process root, startup-pump.
 * root_coro from pm_metal_mod_fn_coro (same path as plain funcs).
 * pid from process_reserve. Mod slot keeps image ownership.
 * Returns 0 ok (live or short-finished); -1 fail.
 */
int pm_metal_wasm_fn_start_async(void                   *module,
                                 void                   *inst,
                                 void                   *exec_env,
                                 void                   *step_fn,
                                 pm_metal_async_handle_t root_coro,
                                 const char             *mod_name,
                                 uint8_t                *copy,
                                 uint32_t                pid);

/** Tear down live wasm instance + async session (reaps process). */
void pm_metal_wasm_live_finish(void);

/** Point WASI bind_inst targets at this module instance (or NULL). */
void pm_metal_wasm_bind_inst(void *module_inst);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_WASM_WASM_H_ */

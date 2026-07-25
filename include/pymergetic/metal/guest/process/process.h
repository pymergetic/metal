/*
 * Metal process — command Extrawurst on the task tree (guest/host dual ABI).
 *
 * A process is a task that was started as a command (UI/stdio redirect).
 * Nesting = subprocess via the same task hierarchy (parent_id + root task).
 * Flat process table indexes process-root tasks — not a second parent tree.
 *
 * impl: common — src/pymergetic/metal/guest/process/process.c
 */
#ifndef PYMERGETIC_METAL_GUEST_PROCESS_PROCESS_H_
#define PYMERGETIC_METAL_GUEST_PROCESS_PROCESS_H_

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/runtime/async/async.h>
#include "pymergetic/metal/shell/ui/types.h" /* IWYU pragma: keep */

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_PROCESS_WASI_MODULE "pymergetic.metal.process"

typedef uint32_t pm_metal_process_id_t;

#define PM_METAL_PROCESS_ID_INVALID 0u
#define PM_METAL_PROCESS_MAX        8u

#define PM_METAL_PROC_STATE_RUNNING 1u
#define PM_METAL_PROC_STATE_EXITED  2u

typedef enum {
  PM_METAL_PROC_UI_NONE = 0,
  PM_METAL_PROC_UI_TAB,
  PM_METAL_PROC_UI_FULLSCREEN /* DEFAULT surface */
} pm_metal_process_ui_kind_t;

/** Shared guest/host layout (fixed-width fields for WASI copy). */
typedef struct {
  pm_metal_process_id_t id;
  char                  name[64];
  uint32_t              state;
  uint32_t              ui_kind; /* pm_metal_process_ui_kind_t */
  pm_metal_ui_handle_t  tab;
  uint32_t              surface;
} pm_metal_process_info_t;

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_PROCESS_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_PROCESS_WASI_MODULE, name)

/** This guest's process id (0 if none). */
extern pm_metal_process_id_t pm_metal_process_self(void)
  PM_METAL_PROCESS_IMPORT(pm_metal_process_self);

/** Copy info for id into guest struct at dest; 1=ok, 0=missing. */
extern int32_t pm_metal_process_info(pm_metal_process_id_t id, uint32_t dest)
  PM_METAL_PROCESS_IMPORT(pm_metal_process_info);

/** Copy up to max infos into guest array at dest; returns count. */
extern uint32_t pm_metal_process_list(uint32_t dest, uint32_t max)
  PM_METAL_PROCESS_IMPORT(pm_metal_process_list);

extern uint32_t pm_metal_process_ui_kind(pm_metal_process_id_t id)
  PM_METAL_PROCESS_IMPORT(pm_metal_process_ui_kind);

extern uint32_t pm_metal_process_surface(pm_metal_process_id_t id)
  PM_METAL_PROCESS_IMPORT(pm_metal_process_surface);

extern pm_metal_ui_handle_t pm_metal_process_tab(pm_metal_process_id_t id)
  PM_METAL_PROCESS_IMPORT(pm_metal_process_tab);

#else /* host */

/** This (current live) process id. */
pm_metal_process_id_t pm_metal_process_self(void);

/**
 * Reserve a process id before instantiate (for PID= env). UI derived from
 * tab / kind. Returns id or INVALID.
 */
pm_metal_process_id_t pm_metal_process_reserve(const char                *name,
                                               pm_metal_process_ui_kind_t ui_kind,
                                               pm_metal_ui_handle_t       tab);

/** Keep reserved process as the live current guest (async stayed up). */
void pm_metal_process_commit_live(pm_metal_process_id_t id);

/**
 * Commit a nested process (subprocess): root_task_h is its process-root task.
 * Pushes stdout/UI redirect; parent remains in the table.
 */
void pm_metal_process_commit_child(pm_metal_process_id_t id, pm_metal_async_handle_t root_task_h);

/** Bind / replace the process-root task handle (top-level or after spawn). */
void pm_metal_process_bind_root_task(pm_metal_process_id_t id, pm_metal_async_handle_t root_task_h);

pm_metal_async_handle_t pm_metal_process_root_task(pm_metal_process_id_t id);

pm_metal_process_id_t pm_metal_process_parent(pm_metal_process_id_t id);

/** Stamp proc_id onto tasks created until stamp_end (subprocess spawn). */
void pm_metal_process_stamp_begin(pm_metal_process_id_t id);
void pm_metal_process_stamp_end(void);
/** Inherit proc for create_task: stamp, else current process, else 0. */
uint32_t pm_metal_process_inherit_id(void);

/**
 * Stash a fresh, private WASM instance (mod.c's fn_process FRESH mode)
 * on this process slot. reap()/release() deinstantiate it automatically
 * (via pm_metal_mod_on_fresh_instance_end) — callers must not close it
 * themselves once handed off here. auto_unload: forwarded verbatim to
 * that teardown call — see PM_METAL_MOD_FLAG_AUTO_UNLOAD.
 */
void pm_metal_process_set_owned_image(pm_metal_process_id_t id,
                                      const void            *img,
                                      int32_t                auto_unload);

/**
 * Read-only peek at a process's owned fresh instance image (a
 * pm_metal_wasm_mod_image_t*), or NULL if this process has none (shared
 * "instance 0" invocation, or already reaped). Do not free/deinstantiate
 * — still owned by the process slot.
 */
const void *pm_metal_process_owned_image(pm_metal_process_id_t id);

/** Drop a reserved process that did not stay live (sync exit / startup end). */
void pm_metal_process_release(pm_metal_process_id_t id);

/** Mark process exited and free its slot (does not end parent). */
void pm_metal_process_reap(pm_metal_process_id_t id);

/**
 * Invoke named command via mod registry (docs/MODS.md).
 * Returns guest exit code, or -1 on host error. Live cmd task → current process.
 * Uses AUTO instance selection — a real process defers to the mod's own
 * declared capability (pm_metal_mod_cap_t); MULTI mods get their own
 * fresh heap/globals like a real exec, SINGLE mods share instance 0.
 */
int pm_metal_process_spawn_mod(const char                *name,
                               pm_metal_process_ui_kind_t ui_kind,
                               pm_metal_ui_handle_t       tab);

/** Pump current live process; 1 done ok, -1 error, 0 still running / none. */
int pm_metal_process_poll(int32_t *status_out);

/**
 * Drain cooperative runners for the shell tick.
 * When a process owns a live session runner, that CPU is left to
 * process_poll/session_pump (avoids double-stepping the stem).
 */
void pm_metal_process_pump_runners(void);

int pm_metal_process_active(void);

pm_metal_process_id_t pm_metal_process_current(void);

const char *pm_metal_process_name(pm_metal_process_id_t id);

int pm_metal_process_info(pm_metal_process_id_t id, pm_metal_process_info_t *out);

/** Copy up to max running slots; returns count written. */
uint32_t pm_metal_process_list(pm_metal_process_info_t *out, uint32_t max);

/** Update UI attachment on a live process. 0 ok. */
int pm_metal_process_attach_ui(pm_metal_process_id_t      id,
                               pm_metal_process_ui_kind_t ui_kind,
                               pm_metal_ui_handle_t       tab);

pm_metal_ui_handle_t pm_metal_process_tab(pm_metal_process_id_t id);

uint32_t pm_metal_process_surface(pm_metal_process_id_t id);

pm_metal_process_ui_kind_t pm_metal_process_ui_kind(pm_metal_process_id_t id);

/** Tear down live wasm + reap process. 0 ok, -1 if not current/live. */
int pm_metal_process_kill(pm_metal_process_id_t id);

/** Host: derive UI kind/surface from a tab handle. */
void pm_metal_process_ui_from_tab(pm_metal_ui_handle_t        tab,
                                  pm_metal_process_ui_kind_t *kind_out,
                                  uint32_t                   *surface_out);

/**
 * Spawn hint for process_reserve (set by cmd invoke / spawn_mod).
 * Host-internal.
 */
void pm_metal_process_set_spawn_hint(pm_metal_process_ui_kind_t ui_kind, pm_metal_ui_handle_t tab);
void pm_metal_process_clear_spawn_hint(void);
int  pm_metal_process_spawn_hint(pm_metal_process_ui_kind_t *ui_kind_out,
                                 pm_metal_ui_handle_t       *tab_out);
pm_metal_process_id_t pm_metal_process_pending(void);

int  pm_metal_process_native_register(void);
void pm_metal_process_bind_inst(void *module_inst);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_PROCESS_PROCESS_H_ */

/*
 * Metal fake process — logical anchor for a live wasm guest (guest/host dual ABI).
 *
 * A process owns the root async session (and its async tasks). Optional UI
 * attachment (tab / fullscreen) is metadata for focus and views (ps, later
 * overlay). Not the same as pm_metal_async_*_task.
 *
 * v1: at most one live guest (async session is global).
 *
 * impl: common — src/pymergetic/metal/guest/process/process.c
 */
#ifndef PYMERGETIC_METAL_GUEST_PROCESS_PROCESS_H_
#define PYMERGETIC_METAL_GUEST_PROCESS_PROCESS_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/shell/ui/types.h" /* IWYU pragma: keep */

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_PROCESS_WASI_MODULE "pymergetic.metal.process"

typedef uint32_t pm_metal_process_id_t;

#define PM_METAL_PROCESS_ID_INVALID 0u
#define PM_METAL_PROCESS_MAX        4u

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
	char name[64];
	uint32_t state;
	uint32_t ui_kind; /* pm_metal_process_ui_kind_t */
	pm_metal_ui_handle_t tab;
	uint32_t surface;
} pm_metal_process_info_t;

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_PROCESS_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_PROCESS_WASI_MODULE, name)

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
pm_metal_process_id_t pm_metal_process_reserve(
	const char *name, pm_metal_process_ui_kind_t ui_kind,
	pm_metal_ui_handle_t tab);

/** Keep reserved process as the live current guest (async stayed up). */
void pm_metal_process_commit_live(pm_metal_process_id_t id);

/** Drop a reserved process that did not stay live (sync exit / startup end). */
void pm_metal_process_release(pm_metal_process_id_t id);

/** Mark current live process exited and free its slot. */
void pm_metal_process_reap(pm_metal_process_id_t id);

/**
 * Bind stdout/UI, run mod. Returns guest exit code, or -1 on host error.
 * If an async guest stays live, it is the current process.
 */
int pm_metal_process_spawn_mod(const char *name,
			       pm_metal_process_ui_kind_t ui_kind,
			       pm_metal_ui_handle_t tab);

/** Pump current live process; 1 done ok, -1 error, 0 still running / none. */
int pm_metal_process_poll(int32_t *status_out);

int pm_metal_process_active(void);

pm_metal_process_id_t pm_metal_process_current(void);

const char *pm_metal_process_name(pm_metal_process_id_t id);

int pm_metal_process_info(pm_metal_process_id_t id,
			  pm_metal_process_info_t *out);

/** Copy up to max running slots; returns count written. */
uint32_t pm_metal_process_list(pm_metal_process_info_t *out, uint32_t max);

/** Update UI attachment on a live process. 0 ok. */
int pm_metal_process_attach_ui(pm_metal_process_id_t id,
			       pm_metal_process_ui_kind_t ui_kind,
			       pm_metal_ui_handle_t tab);

pm_metal_ui_handle_t pm_metal_process_tab(pm_metal_process_id_t id);

uint32_t pm_metal_process_surface(pm_metal_process_id_t id);

pm_metal_process_ui_kind_t pm_metal_process_ui_kind(pm_metal_process_id_t id);

/** Tear down live wasm + reap process. 0 ok, -1 if not current/live. */
int pm_metal_process_kill(pm_metal_process_id_t id);

/** Host: derive UI kind/surface from a tab handle. */
void pm_metal_process_ui_from_tab(pm_metal_ui_handle_t tab,
				 pm_metal_process_ui_kind_t *kind_out,
				 uint32_t *surface_out);

/**
 * Spawn hint for wasm_run_bytes → reserve (set by spawn_mod).
 * Host-internal.
 */
void pm_metal_process_set_spawn_hint(pm_metal_process_ui_kind_t ui_kind,
				     pm_metal_ui_handle_t tab);
void pm_metal_process_clear_spawn_hint(void);
int pm_metal_process_spawn_hint(pm_metal_process_ui_kind_t *ui_kind_out,
				pm_metal_ui_handle_t *tab_out);
pm_metal_process_id_t pm_metal_process_pending(void);

int pm_metal_process_native_register(void);
void pm_metal_process_bind_inst(void *module_inst);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_PROCESS_PROCESS_H_ */

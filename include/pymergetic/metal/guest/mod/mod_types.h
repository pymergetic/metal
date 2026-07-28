/*
 * Mod registry — shared types: ids/handles, enums, POD structs. No
 * functions here (see mod_lifecycle.h / mod_call.h / mod_fresh.h) — this
 * is the piece other headers (e.g. boot/authors.h, for pm_metal_mod_about_t)
 * can include on its own without pulling in the whole registry API.
 *
 * Convenience umbrella: mod.h (includes this + the three above).
 * Contract: docs/MODS.md
 */
#ifndef PYMERGETIC_METAL_GUEST_MOD_MOD_TYPES_H_
#define PYMERGETIC_METAL_GUEST_MOD_MOD_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_MOD_WASI_MODULE "pymergetic.metal.mod"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_MOD_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_MOD_WASI_MODULE, name)
#endif

#define PM_METAL_MOD_ID_INVALID 0u
#define PM_METAL_MOD_MAX        128u
#define PM_METAL_MOD_FUNC_MAX   1024u
#define PM_METAL_MOD_CMD_MAX    128u
#define PM_METAL_MOD_FN_H_MAX   1024u
#define PM_METAL_MOD_FRESH_MAX  128u

typedef uint32_t pm_metal_mod_id_t;
typedef uint32_t pm_metal_mod_fn_h_t;

#define PM_METAL_MOD_FN_H_INVALID 0u

/**
 * Opaque handle onto an open FRESH-mode instance scope
 * (pm_metal_mod_fresh_open) — same "resolve/call by handle, no raw
 * pointers across the wasm boundary" idiom as pm_metal_mod_fn_h_t.
 * A guest never sees the underlying pm_metal_wasm_mod_image_t.
 */
typedef uint32_t pm_metal_mod_fresh_h_t;

#define PM_METAL_MOD_FRESH_H_INVALID 0u

/**
 * Instancing capability, declared once from on_load via
 * pm_metal_mod_set_capability(). Governs what AUTO resolves to and which
 * forced instance_mode values are honored — see pm_metal_mod_instance_t.
 * Undeclared mods default to SINGLE (today's original, back-compat
 * behavior: one persistent instance for everything).
 */
typedef enum {
  /* One persistent instance ("instance 0") for everything, forever.
   * Forced FRESH is refused. Pick this for stateless mods or mods that
   * are fine with shared/reentrant statics (e.g. hello, the test mods). */
  PM_METAL_MOD_CAP_SINGLE = 0,
  /* Instance 0 still exists and runs on_load (func/cmd registration),
   * but real invocations are expected to use a fresh instance instead —
   * AUTO resolves to FRESH. Forced SHARED is still allowed (e.g. a
   * lightweight library call), it's just not the default. Pick this for
   * mods with real static state that must not leak/persist across runs
   * or that want to run multiple times concurrently (e.g. Doom). */
  PM_METAL_MOD_CAP_MULTI = 1
} pm_metal_mod_cap_t;

/**
 * Per-call instance selection for cmd_invoke / fn_process.
 * See pm_metal_mod_cap_t for how AUTO resolves and which forced values
 * a mod's declared capability will refuse.
 */
typedef enum {
  PM_METAL_MOD_INSTANCE_AUTO   = 0, /* ask the mod's declared capability */
  PM_METAL_MOD_INSTANCE_SHARED = 1, /* force the mod's persistent "instance 0" */
  PM_METAL_MOD_INSTANCE_FRESH  = 2  /* force a fresh, private instance (refused if cap == SINGLE) */
} pm_metal_mod_instance_t;

/** Bit flags for cmd_invoke / fn_process, alongside pm_metal_mod_instance_t. */
typedef enum {
  PM_METAL_MOD_FLAG_NONE = 0u,
  /*
   * Once this call's process ends and its instance is torn down,
   * best-effort unload the whole mod too (drop the compiled module +
   * registry rows), instead of leaving it READY/resident. Refused
   * silently (mod just stays loaded) if anything else is still using
   * it — see pm_metal_mod_unload(). Only meaningful together with
   * FRESH (or AUTO resolving to FRESH); ignored for a SHARED call.
   */
  PM_METAL_MOD_FLAG_AUTO_UNLOAD = 1u << 0
} pm_metal_mod_flag_t;

/**
 * Resolved mod function — fill once at callsite, then call without
 * string lookup. Pointers are live while the mod stays loaded (this is
 * the mod's shared "instance 0" — see fn_process's instance_mode param
 * for a private, fresh instance instead).
 */
typedef struct pm_metal_mod_fn {
  void *inst;     /* wasm_module_inst_t */
  void *exec_env; /* wasm_exec_env_t */
  void *fn;       /* wasm_function_inst_t — async (i)i */
} pm_metal_mod_fn_t;

/**
 * Resolved command — fn first so &cmd.fn works with fn_coro.
 * name is the registry command (default process table name).
 * mod_name/export_name identify the owning mod + wasm export so
 * fn_process(FRESH) can re-resolve fn against a fresh instance instead
 * of the shared one above.
 */
typedef struct pm_metal_mod_cmd {
  pm_metal_mod_fn_t fn;
  char              name[64];
  char              mod_name[64];
  char              export_name[64];
} pm_metal_mod_cmd_t;

/** Max authors/contributors recorded per mod (or the kernel — see boot/authors.h). */
#define PM_METAL_MOD_AUTHOR_MAX 4u

/**
 * Author/contributor role — enum, not a free-text string, so it crosses
 * the wasm ABI as a plain uint32_t (same convention as pm_metal_mod_cap_t)
 * and has exactly one spelling per role.
 */
typedef enum {
  PM_METAL_MOD_AUTHOR_ROLE_AUTHOR      = 0,
  PM_METAL_MOD_AUTHOR_ROLE_MAINTAINER  = 1,
  PM_METAL_MOD_AUTHOR_ROLE_CONTRIBUTOR = 2
} pm_metal_mod_author_role_t;

typedef struct pm_metal_mod_author {
  char                       name[64];
  char                       email[64];
  pm_metal_mod_author_role_t role;
} pm_metal_mod_author_t;

/** Max bytes (incl. NUL) in pm_metal_mod_about_t.desc — room for a real,
 * multi-paragraph description, not just a one-liner (embedded '\n' is
 * fine; printers split on it, see pm_metal_shell_out_lines()). */
#define PM_METAL_MOD_DESC_MAX 2048u

/** Max bytes (incl. NUL) in pm_metal_mod_about_t.url — a homepage/repo
 * link for the mod (or Metal itself), not per-author (see
 * pm_metal_mod_author_t for individual contact info). Empty ("") if the
 * mod never declared one. */
#define PM_METAL_MOD_URL_MAX 128u

/**
 * One mod's (or the kernel's — boot/authors.h) "about" record: version,
 * description (may be multi-line — '\n'-separated, not just one line),
 * a project homepage/repo url, and up to PM_METAL_MOD_AUTHOR_MAX authors.
 * Fixed-size, no internal pointers, so it crosses the wasm ABI by value —
 * same idiom as pm_metal_process_info_t (guest passes a linear-memory
 * offset, host reads/writes the struct directly via
 * wasm_runtime_addr_app_to_native()). ~2.7 KB total (mostly desc) —
 * callers copy it via a heap temp (pm_metal_mem_alloc), not a stack
 * local; see mod.c/authors.c natives.
 */
typedef struct pm_metal_mod_about {
  char                  version[32];
  char                  desc[PM_METAL_MOD_DESC_MAX];
  char                  url[PM_METAL_MOD_URL_MAX];
  uint32_t              author_count;
  pm_metal_mod_author_t authors[PM_METAL_MOD_AUTHOR_MAX];
} pm_metal_mod_about_t;

/**
 * Host-side registry row for shell `mods` / Python listing — not a wasm
 * ABI type (guests use load/ready/unload by name).
 */
typedef struct pm_metal_mod_info {
  char     name[64];
  int32_t  ready;      /* 1 if READY or RUNNING */
  int32_t  running;    /* 1 if MOD_RUNNING (process stem active on slot) */
  uint32_t cap;        /* pm_metal_mod_cap_t */
  uint32_t open_tasks;
  uint32_t fresh_open;
  int32_t  has_about;  /* 1 if set_about was called */
} pm_metal_mod_info_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_MOD_MOD_TYPES_H_ */

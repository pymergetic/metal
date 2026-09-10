/* pymergetic.metal.jit.c — C → native via embedded TCC / libtcc.
 *
 * Step function: lex/parse/compile via TCC, relocate, return
 * a callable function pointer (native_entry).
 *
 * The card is vm_only — the compile step uses TCC's allocator.
 */
#ifndef PYMERGETIC_METAL_JIT_C_TYPES_H
#define PYMERGETIC_METAL_JIT_C_TYPES_H

#include "pymergetic/metal/coop/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_jit_c_result {
    int32_t ok;
    uint8_t wasmbuf[262144];   /* 256KB WASM buffer */
    const uint8_t *wasm_bytes;
    size_t wasm_len;
    const char *error;
    void *native_entry;   /* function pointer from tcc_get_symbol("main") */
} pm_metal_jit_c_result_t;

/* Allocate a compile frame. source is copied into the frame; caller
 * keeps ownership of the original buffer. Returns a coroutine that
 * will produce pm_metal_jit_c_result_t on completion. */
pm_metal_coop_coro_t *pm_metal_jit_c_compile_alloc(
    pm_util_mem_arena_t *arena,
    const char *source,
    size_t source_len,
    const char *module_name);

/* Step the compile coroutine. One step = full lex/parse/compile/serialize.
 * Returns DONE on success (see result), ERROR on failure. */
pm_metal_coop_status_t pm_metal_jit_c_compile_step(pm_metal_coop_coro_t *self);

/* Result view of a completed compile coroutine (valid until the coro's
 * arena is destroyed). Returns NULL when self is not a jit.c frame. */
const pm_metal_jit_c_result_t *pm_metal_jit_c_result_of(
    const pm_metal_coop_coro_t *self);

/* Free the result and its owned strings via the arena. */
void pm_metal_jit_c_result_free(pm_util_mem_arena_t *arena, pm_metal_jit_c_result_t *r);

/* Compile one C source to a loadable object (arena-owned bytes in obj_out).
 * Native seats: ELF ET_REL via TCC_OUTPUT_OBJ (the multi-object link path).
 * The wasm32 seat: the serialized WASM module itself — every defined
 * function is exported by name, and the loader publishes those exports
 * into the registry (the software-defined link). */
int32_t pm_metal_jit_c_object_compile(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len);

/* Cross-compile target: which TCC backend produces the object. SEAT is the
 * backend this binary embeds natively; WASM32 / ARM_EABI / X86_64 ask for
 * that backend when the seat links a second, symbol-prefixed instance
 * (ELF seats enable PM_METAL_TCC_CROSS_WASM32 / _ARM_EABI; the wasm32
 * browser seat enables _X86_64 and _ARM_EABI — its native IS wasm32).
 * A seat without the requested backend refuses with a clear errbuf —
 * never silently falling back. */
typedef enum pm_metal_jit_c_target {
    PM_METAL_JIT_C_TARGET_SEAT = 0,
    PM_METAL_JIT_C_TARGET_WASM32 = 1,
    PM_METAL_JIT_C_TARGET_ARM_EABI = 2,
    PM_METAL_JIT_C_TARGET_X86_64 = 3,
} pm_metal_jit_c_target_t;

/* compile_opts: the include/define seam the build card drives. include_dirs
 * are added with tcc_add_include_path in order; defines with
 * tcc_define_symbol, where "NAME" defines to 1 and "NAME=VALUE" (split on
 * the first '=') defines with that value. The 7-arg object_compile above is
 * a thin wrapper over this with empty lists. */
int32_t pm_metal_jit_c_object_compile_opts(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len);

/* compile_target: compile_opts plus the cross-compile knob. TARGET_SEAT
 * picks the seat's native backend exactly like compile_opts; TARGET_WASM32
 * routes to the wasm32 instance (or refuses where none is linked). The
 * object format follows the backend: ELF ET_REL from the native backend,
 * a serialized WASM module from wasm32. */
int32_t pm_metal_jit_c_object_compile_target(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    int32_t target,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len);

/*------------------ TCC allocator window (Phase 5) ------------------
 * TCC's reallocator is ONE global (tcc_set_realloc in libtcc.c): every
 * tcc_malloc/tcc_realloc/tcc_free call in the process dispatches through
 * it, and its arena context lives in a TU-static here. That makes any
 * TCC invocation an exclusive window: while one caller's arena is
 * installed, another caller's compile would allocate from the wrong
 * arena (and its diagnostics, Sym tables and emission buffers would be
 * freed by the wrong teardown).
 *
 * The window API makes that contract first-class. acquire() installs
 * arena as the allocator context and returns the previous reallocator;
 * release() restores it. Both are guarded by one lock so two threads can
 * never hold overlapping windows on seats with real threads — the lock
 * IS the serialization, not an optimization. object_compile_opts takes
 * the window internally (its callers need nothing); the build card's
 * actor takes it around its whole unit compile so every TCC call in the
 * job — per-source compiles and the link — shares one arena context.
 * Nothing else may touch s_tcc_arena or tcc_set_realloc. */

/* Install arena as TCC's allocator context. Returns 0 on success, -1
 * when arena is NULL. The caller MUST release the window with the
 * arena it acquired it with. Blocks while another window is held. */
int32_t pm_metal_jit_c_arena_acquire(pm_util_mem_arena_t *arena);

/* Release a window acquired with arena_acquire(arena). Restores the
 * prior reallocator and clears the allocator context. Returns 0, -1
 * when the window is not held for arena. */
int32_t pm_metal_jit_c_arena_release(pm_util_mem_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_JIT_C_TYPES_H */
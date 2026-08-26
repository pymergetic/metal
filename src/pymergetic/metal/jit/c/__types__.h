/* pymergetic.metal.jit.c — TCC C → WASM bytecode async coroutine.
 *
 * Step function: lex/parse/compile via TCC (embedded in-bin),
 * serialize WASM bytes, return DONE with the WASM module bytes.
 *
 * The card is vm_only — the compile step may invoke TCC which uses
 * malloc/GC, so it runs under the async card's VM lock.
 */
#ifndef PYMERGETIC_METAL_JIT_C_TYPES_H
#define PYMERGETIC_METAL_JIT_C_TYPES_H

#include "pymergetic/metal/async/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result of a C → WASM compilation.
 *
 * On success, ok = 1, wasm_bytes/wasm_len point to the emitted WASM
 * module (owned by the coro frame — caller must not free).
 * On failure, ok = 0 and error points to a diagnostic string
 * (allocated in the coro frame's tail; valid until the coro is freed). */
typedef struct pm_metal_jit_c_result {
    int32_t ok;
    const uint8_t *wasm_bytes;
    size_t wasm_len;
    const char *error;
} pm_metal_jit_c_result_t;

/* Allocate a compile frame. source is copied into the frame; caller
 * keeps ownership of the original buffer. Returns a coroutine that
 * will produce pm_metal_jit_c_result_t on completion. */
pm_metal_async_coro_t *pm_metal_jit_c_compile_alloc(
    pm_util_mem_arena_t *arena,
    const char *source,
    size_t source_len,
    const char *module_name);

/* Step the compile coroutine. One step = full lex/parse/compile/serialize.
 * Returns DONE on success (see result), ERROR on failure. */
pm_metal_async_status_t pm_metal_jit_c_compile_step(pm_metal_async_coro_t *self);

/* Free the result and its owned strings via the arena. */
void pm_metal_jit_c_result_free(pm_util_mem_arena_t *arena, pm_metal_jit_c_result_t *r);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_JIT_C_TYPES_H */
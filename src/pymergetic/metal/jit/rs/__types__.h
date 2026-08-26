/* pymergetic.metal.jit.rs — Rust → WASM bytecode async coroutine.
 *
 * Step function: mrustc (build-time tool) converts Rust to C,
 * then TCC/WASM backend compiles C to WASM. The coroutine chains
 * both stages in a single async step.
 *
 * The card is vm_only — both stages may use GC/malloc.
 */
#ifndef PYMERGETIC_METAL_JIT_RS_TYPES_H
#define PYMERGETIC_METAL_JIT_RS_TYPES_H

#include "pymergetic/metal/async/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result of a Rust → WASM compilation.
 *
 * On success, ok = 1, wasm_bytes/wasm_len point to the emitted WASM
 * module (owned by the coro frame — caller must not free).
 * On failure, ok = 0 and error points to a diagnostic string. */
typedef struct pm_metal_jit_rs_result {
    int32_t ok;
    const uint8_t *wasm_bytes;
    size_t wasm_len;
    const char *error;
} pm_metal_jit_rs_result_t;

/* Allocate a compile frame. source is copied into the frame; caller
 * keeps ownership of the original buffer. Returns a coroutine that
 * will produce pm_metal_jit_rs_result_t on completion.
 *
 * When mrustc+TCC are linked, the compile chain is:
 *   Rust src → mrustc → C src → TCC/WASM → WASM bytes */
pm_metal_async_coro_t *pm_metal_jit_rs_compile_alloc(
    pm_util_mem_arena_t *arena,
    const char *source,
    size_t source_len,
    const char *module_name);

/* Step the compile coroutine. One step = full mrustc→TCC→WASM pipeline.
 * Returns DONE on success, ERROR on failure. */
pm_metal_async_status_t pm_metal_jit_rs_compile_step(pm_metal_async_coro_t *self);

/* Free the result and its owned strings via the arena. */
void pm_metal_jit_rs_result_free(pm_util_mem_arena_t *arena, pm_metal_jit_rs_result_t *r);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_JIT_RS_TYPES_H */
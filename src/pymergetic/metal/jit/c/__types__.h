/* pymergetic.metal.jit.c — C → native via embedded TCC / libtcc.
 *
 * Step function: lex/parse/compile via TCC, relocate, return
 * a callable function pointer (native_entry).
 *
 * The card is vm_only — the compile step uses TCC's allocator.
 */
#ifndef PYMERGETIC_METAL_JIT_C_TYPES_H
#define PYMERGETIC_METAL_JIT_C_TYPES_H

#include "pymergetic/metal/async/__types__.h"

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
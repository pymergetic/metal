/* pymergetic.metal.jit.py — µPy lex/parse/compile as an async coroutine.
 *
 * Step function: lex from source bytes, parse, compile to module object,
 * install into sys.modules, return DONE with the module qstr name.
 *
 * vm_only — the step re-enters the bytecode VM and is stepped under the
 * async card's VM lock on any runner core that has MicroPython thread state.
 */
#ifndef PYMERGETIC_METAL_JIT_PY_TYPES_H
#define PYMERGETIC_METAL_JIT_PY_TYPES_H

#include "pymergetic/metal/async/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward type that the public API uses for the compilation result. */
typedef struct pm_metal_jit_py_result pm_metal_jit_py_result_t;

/* Compile Python source into a µPy module. Blocks (nlr_push lex/parse/compile)
 * for the duration of one async step — the caller splits large sources across
 * multiple steps by chunking, or the entire compilation runs inside a single
 * vm_only coroutine step.
 *
 * On success, result->ok = 1 and the module is installed in sys.modules under
 * result->module_name. On failure, result->ok = 0 and the exception is stored
 * as a string in result->error (allocated from the arena; caller must
 * pm_metal_jit_py_result_free it via the same arena). */
struct pm_metal_jit_py_result {
    int32_t ok;
    char *module_name;
    char *error;
};

/* Allocate and run one compilation step. Returns a pm_metal_jit_py_result_t
 * allocated from the caller's arena. The coroutine frame must embed the
 * source bytes and module_name. DONE means the module is in sys.modules;
 * ERROR means compilation failed (see result->error). */
pm_metal_async_status_t pm_metal_jit_py_compile_step(pm_metal_async_coro_t *self);

/* Free a result struct and its owned strings via the arena. */
void pm_metal_jit_py_result_free(pm_util_mem_arena_t *arena, pm_metal_jit_py_result_t *r);

/* Allocate a compile frame. source + module_name are copied into the frame;
 * caller keeps ownership of the original buffers. */
pm_metal_async_coro_t *pm_metal_jit_py_compile_alloc(
    pm_util_mem_arena_t *arena,
    const char *source,
    size_t source_len,
    const char *module_name);

/* Python object loop (mpy artifacts) — the Python twin of jit.c's
 * object_compile: source in, serialized bytecode out. Load runs the
 * reverse: bytes back into a live module (exactly what a .mpy import
 * does). Together they close Python's in-kernel compile loop with no
 * host tool anywhere: Python source -> µPy compiler -> mpy bytes -> µPy
 * VM. Needs MICROPY_PERSISTENT_CODE_SAVE; seats without it refuse
 * politely (rc -1) so callers can skip. */
int32_t pm_metal_jit_py_object_compile(
    pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char *module_name,
    uint8_t **mpy_out, size_t *mpy_len,
    char *errbuf, size_t errbuf_len);

int32_t pm_metal_jit_py_object_load(
    pm_util_mem_arena_t *arena,
    const uint8_t *mpy, size_t mpy_len,
    const char *module_name,
    char *errbuf, size_t errbuf_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_JIT_PY_TYPES_H */
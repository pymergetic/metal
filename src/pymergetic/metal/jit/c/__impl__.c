/* pymergetic.metal.jit.c — C → WASM bytecode via embedded TCC.
 *
 * When TCC is linked (TCC backend complete), the compile step:
 *   1. Invokes TCC to parse C source
 *   2. TCC's WASM backend emits IR ops
 *   3. Post-pass serializes IR to WAMR-compatible WASM binary
 *   4. Returns WASM bytes in pm_metal_jit_c_result_t
 *
 * Without TCC linked (current state), compile_alloc returns a stub
 * error coro — the API surface is registered so callers can probe and
 * the card proves on all seats. The real TCC wiring is a follow-on.
 */
#include "pymergetic/metal/jit/c/__exports__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#define PM_METAL_JIT_C_ERR_MAX 256u
#define PM_METAL_JIT_C_WASM_CAP (64u * 1024u)  /* 64 KiB wasm output cap */

typedef struct {
    pm_metal_async_coro_t coro;
    pm_metal_jit_c_result_t result;
    char errbuf[PM_METAL_JIT_C_ERR_MAX];
    uint8_t wasmbuf[PM_METAL_JIT_C_WASM_CAP];
    char *source;
    size_t source_len;
} pm_metal_jit_c_frame_t;

/* ---- TCC integration stub (no TCC linked yet) ---- */

#if PM_HAS_TCC
/* Real path: TCC is linked. Invoke the compiler, emit WASM.
 * TCC's WASM backend lives in lib/tcc and is compiled via
 * fw_tcc.mk / tools/tcc.mk. When that branch is ready, this
 * block replaces the stub below. */
#include "libtcc.h"
#else
/* Stub: TCC not linked. Return a compile error. */
static int pm_metal_jit_c_tcc_compile(
    const char *source, size_t source_len,
    uint8_t *out, size_t out_cap, size_t *out_len)
{
    (void)source;
    (void)source_len;
    (void)out;
    (void)out_cap;
    (void)out_len;
    return -1;
}
#endif

/* ---- Public API ---- */

pm_metal_async_coro_t *pm_metal_jit_c_compile_alloc(
    pm_util_mem_arena_t *arena,
    const char *source,
    size_t source_len,
    const char *module_name)
{
    size_t name_len;
    size_t frame_bytes;
    pm_metal_jit_c_frame_t *f;
    char *src_copy;
    if (arena == NULL || source == NULL || source_len == 0 || module_name == NULL) {
        return NULL;
    }
    name_len = strlen(module_name);
    if (name_len == 0) {
        return NULL;
    }
    frame_bytes = sizeof(*f) + source_len + name_len + 1u;
    f = (pm_metal_jit_c_frame_t *)pm_metal_async_coro_create(
        pm_metal_jit_c_compile_step, frame_bytes);
    if (f == NULL) {
        return NULL;
    }
    src_copy = (char *)(f + 1);
    memcpy(src_copy, source, source_len);
    f->source = src_copy;
    f->source_len = source_len;
    memset(&f->result, 0, sizeof(f->result));
    /* stow the module name in the tail for diagnostics */
    {
        char *p = src_copy + source_len;
        memcpy(p, module_name, name_len + 1u);
    }
    f->result.error = NULL;
    f->result.wasm_bytes = NULL;
    f->result.wasm_len = 0;
    return &f->coro;
}

void pm_metal_jit_c_result_free(pm_util_mem_arena_t *arena, pm_metal_jit_c_result_t *r) {
    (void)arena;
    (void)r;
}

pm_metal_async_status_t pm_metal_jit_c_compile_step(pm_metal_async_coro_t *self) {
    pm_metal_jit_c_frame_t *f;
    pm_metal_jit_c_result_t *r;

    if (self == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    f = (pm_metal_jit_c_frame_t *)self;
    r = &f->result;

    if (f->source == NULL || f->source_len == 0) {
        r->ok = 0;
        return PM_METAL_ASYNC_ERROR;
    }

    int rc = pm_metal_jit_c_tcc_compile(
        f->source, f->source_len,
        f->wasmbuf, sizeof(f->wasmbuf), &r->wasm_len);

    if (rc != 0) {
        const char *msg = "C->WASM compile not available (TCC backend not linked)";
        size_t n = strlen(msg);
        if (n + 1u <= sizeof(f->errbuf)) {
            memcpy(f->errbuf, msg, n + 1u);
            r->error = f->errbuf;
        }
        r->ok = 0;
        return PM_METAL_ASYNC_ERROR;
    }

    r->ok = 1;
    r->wasm_bytes = f->wasmbuf;
    return PM_METAL_ASYNC_DONE;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_compile_alloc, pm_metal_jit_c_compile_alloc,
    pm_metal_async_coro_t *(pm_util_mem_arena_t *, const char *, size_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_compile_step, pm_metal_jit_c_compile_step,
    pm_metal_async_status_t(pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_result_free, pm_metal_jit_c_result_free,
    void(pm_util_mem_arena_t *, pm_metal_jit_c_result_t *));
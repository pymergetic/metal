/* pymergetic.metal.jit.rs — Rust → WASM bytecode via mrustc + TCC.
 *
 * Pipeline (when both backends are linked):
 *   1. mrustc (WASM guest running in WAMR) converts Rust → C
 *   2. TCC/WASM backend compiles C → WAMR-compatible WASM
 *   3. Resulting WASM module can be loaded by the same WAMR instance
 *
 * Without mrustc + TCC linked (current state), compile_alloc is a stub
 * — the API surface registers so callers can probe, and the card
 * proves on all seats. The real backend wiring is follow-on work.
 */
#include "pymergetic/metal/jit/rs/__exports__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/metal/jit/c.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#define PM_METAL_JIT_RS_ERR_MAX 256u
#define PM_METAL_JIT_RS_C_CAP     (512u * 1024u)
#define PM_METAL_JIT_RS_WASM_CAP  (64u * 1024u)

typedef struct {
    pm_metal_async_coro_t coro;
    pm_metal_jit_rs_result_t result;
    char errbuf[PM_METAL_JIT_RS_ERR_MAX];
    char cbuf[PM_METAL_JIT_RS_C_CAP];
    uint8_t wasmbuf[PM_METAL_JIT_RS_WASM_CAP];
    char *source;
    size_t source_len;
} pm_metal_jit_rs_frame_t;

/* ---- mrustc integration stub (no mrustc linked yet) ---- */

#if PM_HAS_MRUSTC && PM_HAS_TCC
static int pm_metal_jit_rs_mrustc_to_c(
    const char *rs_source, size_t rs_len,
    char *c_out, size_t c_out_cap, size_t *c_out_len)
{
    (void)rs_source;
    (void)rs_len;
    (void)c_out;
    (void)c_out_cap;
    (void)c_out_len;
    return -1;
}
#else
static int pm_metal_jit_rs_mrustc_to_c(
    const char *rs_source, size_t rs_len,
    char *c_out, size_t c_out_cap, size_t *c_out_len)
{
    (void)rs_source;
    (void)rs_len;
    (void)c_out;
    (void)c_out_cap;
    (void)c_out_len;
    return -1;
}
#endif

/* ---- Public API ---- */

pm_metal_async_coro_t *pm_metal_jit_rs_compile_alloc(
    pm_util_mem_arena_t *arena,
    const char *source,
    size_t source_len,
    const char *module_name)
{
    size_t name_len;
    size_t frame_bytes;
    pm_metal_jit_rs_frame_t *f;
    char *src_copy;
    if (arena == NULL || source == NULL || source_len == 0 || module_name == NULL) {
        return NULL;
    }
    name_len = strlen(module_name);
    if (name_len == 0) {
        return NULL;
    }
    frame_bytes = sizeof(*f) + source_len + name_len + 1u;
    f = (pm_metal_jit_rs_frame_t *)pm_metal_async_coro_create(
        pm_metal_jit_rs_compile_step, frame_bytes);
    if (f == NULL) {
        return NULL;
    }
    src_copy = (char *)(f + 1);
    memcpy(src_copy, source, source_len);
    f->source = src_copy;
    f->source_len = source_len;
    memset(&f->result, 0, sizeof(f->result));
    {
        char *p = src_copy + source_len;
        memcpy(p, module_name, name_len + 1u);
    }
    f->result.error = NULL;
    f->result.wasm_bytes = NULL;
    f->result.wasm_len = 0;
    return &f->coro;
}

void pm_metal_jit_rs_result_free(pm_util_mem_arena_t *arena, pm_metal_jit_rs_result_t *r) {
    (void)arena;
    (void)r;
}

pm_metal_async_status_t pm_metal_jit_rs_compile_step(pm_metal_async_coro_t *self) {
    pm_metal_jit_rs_frame_t *f = (pm_metal_jit_rs_frame_t *)self;
    pm_metal_jit_rs_result_t *r;
    size_t c_len = 0;
    int rc;
    const char *msg;
    size_t n;

    if (self == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    r = &f->result;

    if (f->source == NULL || f->source_len == 0) {
        r->ok = 0;
        return PM_METAL_ASYNC_ERROR;
    }

    /* Stage 1: Rust → C via mrustc */
    rc = pm_metal_jit_rs_mrustc_to_c(
        f->source, f->source_len,
        f->cbuf, sizeof(f->cbuf), &c_len);
    if (rc != 0) {
        msg = "Rust->C compile not available (mrustc not linked). "
              "Vendoring mrustc as a WASM guest is a follow-on step.";
        n = strlen(msg);
        if (n + 1u <= sizeof(f->errbuf)) {
            memcpy(f->errbuf, msg, n + 1u);
            r->error = f->errbuf;
        }
        r->ok = 0;
        return PM_METAL_ASYNC_ERROR;
    }

    /* Stage 2: C → WASM via TCC. */
    (void)c_len;
    msg = "C->WASM compile not available (TCC not linked). "
          "Vendoring TCC with WASM backend is the next step.";
    n = strlen(msg);
    if (n + 1u <= sizeof(f->errbuf)) {
        memcpy(f->errbuf, msg, n + 1u);
        r->error = f->errbuf;
    }
    r->ok = 0;
    return PM_METAL_ASYNC_ERROR;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.jit.rs, pm_metal_jit_rs_compile_alloc, pm_metal_jit_rs_compile_alloc,
    pm_metal_async_coro_t *(pm_util_mem_arena_t *, const char *, size_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.rs, pm_metal_jit_rs_compile_step, pm_metal_jit_rs_compile_step,
    pm_metal_async_status_t(pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.rs, pm_metal_jit_rs_result_free, pm_metal_jit_rs_result_free,
    void(pm_util_mem_arena_t *, pm_metal_jit_rs_result_t *));
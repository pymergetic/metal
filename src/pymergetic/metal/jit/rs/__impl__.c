/* pymergetic.metal.jit.rs -- Rust to WASM bytecode via mrustc + TCC.
 *
 * Pipeline (when both backends are linked):
 *   1. mrustc (subprocess on host/unix, WASM guest on firmware) converts Rust to C
 *   2. TCC/WASM backend compiles C to WAMR-compatible WASM
 *   3. Resulting WASM module can be loaded by the same WAMR instance
 */

#include "pymergetic/metal/jit/rs/__exports__.h"

#include "pymergetic/metal/coop.h"
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/metal/jit/c.h"
#include "pymergetic/util/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PM_METAL_JIT_RS_ERR_MAX 256u
#define PM_METAL_JIT_RS_C_CAP     (512u * 1024u)
#define PM_METAL_JIT_RS_WASM_CAP  (256u * 1024u)

typedef struct {
    pm_metal_coop_coro_t coro;
    pm_metal_jit_rs_result_t result;
    char errbuf[PM_METAL_JIT_RS_ERR_MAX];
    char cbuf[PM_METAL_JIT_RS_C_CAP];
    uint8_t wasmbuf[PM_METAL_JIT_RS_WASM_CAP];
    char *source;
    size_t source_len;
} pm_metal_jit_rs_frame_t;

/* ---- mrustc: Rust -> C (in-process, no subprocess) ---- */

#if defined(PM_HAS_MRUSTC) && PM_HAS_MRUSTC
#include <errno.h>

/* The mrustc compiler is embedded in this binary (bin/mrustc.a, linked with
 * -Wl,--whole-archive) and called in-process via the tools/mrustc_embed shim.
 * No subprocess is spawned: the Rust source is written to a temp file (mrustc's
 * lexer is filename-based), compiled down to C by the in-process pipeline, and
 * the generated C is returned for the TCC/WASM stage. */
int pm_metal_jit_rs_mrustc_compile(
    const char *rs_source, size_t rs_len,
    char *c_out, size_t c_out_cap, size_t *c_out_len);
int pm_metal_jit_rs_mrustc_compile_edition(
    const char *rs_source, size_t rs_len, int edition,
    char *c_out, size_t c_out_cap, size_t *c_out_len);

static int pm_metal_jit_rs_mrustc_to_c(
    const char *rs_source, size_t rs_len,
    char *c_out, size_t c_out_cap, size_t *c_out_len)
{
    (void)errno;
    return pm_metal_jit_rs_mrustc_compile(rs_source, rs_len, c_out, c_out_cap, c_out_len);
}

/* Edition-selected compile: 2015 keeps the historical wasm-guest route
 * byte-identical; 2024 is the kernel's card language (unsafe extern blocks,
 * crate:: paths) — the build card's capability fallback for impl="rs" units
 * whose Rust is beyond rsx's subset rides this. */
int pm_metal_jit_rs_mrustc_to_c_edition(
    const char *rs_source, size_t rs_len, int edition,
    char *c_out, size_t c_out_cap, size_t *c_out_len)
{
    (void)errno;
    return pm_metal_jit_rs_mrustc_compile_edition(rs_source, rs_len, edition,
        c_out, c_out_cap, c_out_len);
}
#else
static int pm_metal_jit_rs_mrustc_to_c(
    const char *rs_source, size_t rs_len,
    char *c_out, size_t c_out_cap, size_t *c_out_len)
{
    (void)rs_source; (void)rs_len; (void)c_out; (void)c_out_cap; (void)c_out_len;
    return -1;
}

/* Stub twin of the edition entry: seats without the embedded mrustc (the
 * firmware trees) refuse loudly — the fallback caller must preserve rsx's
 * refusal when this returns -1, never paper over it. */
int pm_metal_jit_rs_mrustc_to_c_edition(
    const char *rs_source, size_t rs_len, int edition,
    char *c_out, size_t c_out_cap, size_t *c_out_len)
{
    (void)rs_source; (void)rs_len; (void)edition;
    (void)c_out; (void)c_out_cap; (void)c_out_len;
    return -1;
}
#endif /* PM_HAS_MRUSTC */

/* ---- TCC/WASM: C -> WASM ---- */

#if defined(PM_HAS_TCC) && PM_HAS_TCC && defined(TCC_TARGET_WASM32)
#include "libtcc.h"
extern int wasm_build_module(uint8_t **out_buf, int *out_len);
extern void wasm_release_buffers(void);
/* tcc.h's pub allocators, declared by hand (same posture as jit.c: the
 * public header does not carry them with MEM_DEBUG off). */
extern void tcc_free(void *ptr);

/* The TCC allocator window lives on the jit.c card (one window per process,
 * lock-guarded, try-acquire): this card's C->WASM stage goes through it like
 * every other TCC caller, so the wasm32 backend's scratch rides the boot
 * arena and the global reallocator is never touched outside the window. The
 * coro face carries no arena of its own — the window installs async's boot
 * arena, same as jit.c's coro wasm path. */
extern int32_t pm_metal_jit_c_arena_acquire(pm_util_mem_arena_t *arena);
extern int32_t pm_metal_jit_c_arena_release(pm_util_mem_arena_t *arena);

static int pm_metal_jit_rs_tcc_wasm_compile(
    const char *c_source, size_t c_len,
    uint8_t *wasm_out, size_t wasm_cap, size_t *wasm_len)
{
    TCCState *s;
    uint8_t *buf = NULL;
    int len = 0;
    pm_util_mem_arena_t *a;
    (void)c_len;

    a = pm_metal_coop_arena();
    if (!a) return -1;
    if (pm_metal_jit_c_arena_acquire(a) != 0) return -1;

    s = tcc_new();
    if (!s) { pm_metal_jit_c_arena_release(a); return -1; }
    tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (tcc_compile_string(s, c_source) != 0) {
        tcc_delete(s);
        wasm_release_buffers(); /* partial emission still rode this arena */
        pm_metal_jit_c_arena_release(a);
        return -1;
    }
    if (wasm_build_module(&buf, &len) != 0 || !buf) {
        tcc_delete(s);
        wasm_release_buffers();
        pm_metal_jit_c_arena_release(a);
        return -1;
    }
    if (len > (int)wasm_cap) {
        /* too big: free while the arena reallocator still owns buf —
         * tcc_free after the restore would be libc free on an arena block */
        tcc_free(buf);
        wasm_release_buffers();
        tcc_delete(s);
        pm_metal_jit_c_arena_release(a);
        return -1;
    }
    memcpy(wasm_out, buf, (size_t)len);
    *wasm_len = (size_t)len;
    /* copy out, then free under the same window (same lifetime rule) */
    tcc_free(buf);
    wasm_release_buffers();
    tcc_delete(s);
    pm_metal_jit_c_arena_release(a);
    return 0;
}
#else
static int pm_metal_jit_rs_tcc_wasm_compile(
    const char *c_source, size_t c_len,
    uint8_t *wasm_out, size_t wasm_cap, size_t *wasm_len)
{
    (void)c_source; (void)c_len; (void)wasm_out; (void)wasm_cap; (void)wasm_len;
    return -1;
}
#endif

/* ---- Public API ---- */

pm_metal_coop_coro_t *pm_metal_jit_rs_compile_alloc(
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
    f = (pm_metal_jit_rs_frame_t *)pm_metal_coop_coro_create(
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

pm_metal_coop_status_t pm_metal_jit_rs_compile_step(pm_metal_coop_coro_t *self) {
    pm_metal_jit_rs_frame_t *f;
    pm_metal_jit_rs_result_t *r;
    size_t c_len = 0;
    int rc;
    const char *msg;
    size_t n;

    if (self == NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    f = (pm_metal_jit_rs_frame_t *)self;
    r = &f->result;

    if (f->source == NULL || f->source_len == 0) {
        r->ok = 0;
        return PM_METAL_ASYNC_ERROR;
    }

    /* Stage 1: Rust -> C via mrustc */
    rc = pm_metal_jit_rs_mrustc_to_c(
        f->source, f->source_len,
        f->cbuf, sizeof(f->cbuf), &c_len);
    if (rc != 0) {
        msg = "Rust->C compile not available (mrustc not found). "
              "Set MRUSTC_BIN env var or install mrustc.";
        n = strlen(msg);
        if (n + 1u <= sizeof(f->errbuf)) {
            memcpy(f->errbuf, msg, n + 1u);
            r->error = f->errbuf;
        }
        r->ok = 0;
        return PM_METAL_ASYNC_ERROR;
    }

    /* Stage 2: C -> WASM via TCC/WASM32 backend */
    rc = pm_metal_jit_rs_tcc_wasm_compile(
        f->cbuf, c_len,
        f->wasmbuf, sizeof(f->wasmbuf), &r->wasm_len);
    if (rc != 0) {
        msg = "C->WASM compile failed (TCC/WASM32 backend unavailable).";
        n = strlen(msg);
        if (n + 1u <= sizeof(f->errbuf)) {
            memcpy(f->errbuf, msg, n + 1u);
            r->error = f->errbuf;
        }
        r->ok = 0;
        return PM_METAL_ASYNC_ERROR;
    }

    r->wasm_bytes = f->wasmbuf;
    r->ok = 1;
    return PM_METAL_ASYNC_DONE;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.jit.rs, pm_metal_jit_rs_compile_alloc, pm_metal_jit_rs_compile_alloc,
    pm_metal_coop_coro_t *(pm_util_mem_arena_t *, const char *, size_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.rs, pm_metal_jit_rs_compile_step, pm_metal_jit_rs_compile_step,
    pm_metal_coop_status_t(pm_metal_coop_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.rs, pm_metal_jit_rs_result_free, pm_metal_jit_rs_result_free,
    void(pm_util_mem_arena_t *, pm_metal_jit_rs_result_t *));

PM_METAL_EXTERNAL_C(mrustc, "1.90.0");

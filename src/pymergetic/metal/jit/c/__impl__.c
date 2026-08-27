#include "pymergetic/metal/jit/c/__exports__.h"
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/util/mem.h"
#include <stdlib.h>
#include <string.h>

#define PM_METAL_JIT_C_ERR_MAX 256u
#define PM_METAL_JIT_C_WASM_CAP (256u * 1024u)

typedef struct {
    pm_metal_async_coro_t coro;
    pm_metal_jit_c_result_t result;
    char errbuf[PM_METAL_JIT_C_ERR_MAX];
    uint8_t wasmbuf[PM_METAL_JIT_C_WASM_CAP];
    char *source;
    size_t source_len;
} pm_metal_jit_c_frame_t;

#if PM_HAS_TCC
#include "libtcc.h"

#ifdef TCC_TARGET_WASM32
/* WASM backend: compile and serialize the WASM module via wasm_build_module() */
int wasm_build_module(uint8_t **out_buf, int *out_len);

static int pm_metal_jit_c_tcc_wasm_compile(const char *source,
    uint8_t *wasm_out, size_t wasm_cap, size_t *wasm_len) {
    TCCState *s = tcc_new();
    if (!s) return -1;
    tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (tcc_compile_string(s, source) != 0) { tcc_delete(s); return -1; }
    uint8_t *buf = NULL;
    int len = 0;
    if (wasm_build_module(&buf, &len) != 0 || !buf) { tcc_delete(s); return -1; }
    if (len > (int)wasm_cap) { free(buf); tcc_delete(s); return -1; }
    memcpy(wasm_out, buf, (size_t)len);
    *wasm_len = (size_t)len;
    free(buf);
    tcc_delete(s);
    return 0;
}
#else
/* Native (x86_64) backend: compile and relocate */
static int pm_metal_jit_c_tcc_native_compile(const char *source, pm_metal_jit_c_result_t *r) {
    TCCState *s = tcc_new();
    if (!s) return -1;
    tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (tcc_compile_string(s, source) != 0) { tcc_delete(s); return -1; }
    if (tcc_relocate(s) != 0) { tcc_delete(s); return -1; }
    r->native_entry = tcc_get_symbol(s, "main");
    r->ok = 1;
    return 0;
}
#endif /* TCC_TARGET_WASM32 */

#else
static int pm_metal_jit_c_tcc_native_compile(const char *source, pm_metal_jit_c_result_t *r) {
    (void)source; (void)r; return -1;
}
#endif /* PM_HAS_TCC */

pm_metal_async_coro_t *pm_metal_jit_c_compile_alloc(
    pm_util_mem_arena_t *arena, const char *source, size_t source_len, const char *module_name) {
    size_t name_len; pm_metal_jit_c_frame_t *f; char *src_copy;
    if (arena == NULL || source == NULL || module_name == NULL || source_len == 0) return NULL;
    name_len = strlen(module_name);
    if (!name_len) return NULL;
    f = (pm_metal_jit_c_frame_t *)pm_metal_async_coro_create(
        pm_metal_jit_c_compile_step, sizeof(*f) + source_len + 1u + name_len + 1u);
    if (!f) return NULL;
    src_copy = (char *)(f + 1);
    memcpy(src_copy, source, source_len);
    src_copy[source_len] = '\0';
    f->source = src_copy;
    f->source_len = source_len;
    memset(&f->result, 0, sizeof(f->result));
    memcpy(src_copy + source_len + 1u, module_name, name_len + 1u);
    return &f->coro;
}

void pm_metal_jit_c_result_free(pm_util_mem_arena_t *arena, pm_metal_jit_c_result_t *r) {
    (void)arena; (void)r;
}

pm_metal_async_status_t pm_metal_jit_c_compile_step(pm_metal_async_coro_t *self) {
    if (!self) return PM_METAL_ASYNC_ERROR;
    pm_metal_jit_c_frame_t *f = (pm_metal_jit_c_frame_t *)self;
    if (!f->source || !f->source_len) return PM_METAL_ASYNC_ERROR;
#ifdef TCC_TARGET_WASM32
    if (pm_metal_jit_c_tcc_wasm_compile(f->source, f->wasmbuf,
        PM_METAL_JIT_C_WASM_CAP, &f->result.wasm_len) != 0) return PM_METAL_ASYNC_ERROR;
    f->result.wasm_bytes = f->wasmbuf;
    f->result.ok = 1;
    return PM_METAL_ASYNC_DONE;
#else
    if (pm_metal_jit_c_tcc_native_compile(f->source, &f->result) != 0) return PM_METAL_ASYNC_ERROR;
    f->result.ok = 1;
    return PM_METAL_ASYNC_DONE;
#endif
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_compile_alloc, pm_metal_jit_c_compile_alloc,
    pm_metal_async_coro_t *(pm_util_mem_arena_t *, const char *, size_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_compile_step, pm_metal_jit_c_compile_step,
    pm_metal_async_status_t(pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_result_free, pm_metal_jit_c_result_free,
    void(pm_util_mem_arena_t *, pm_metal_jit_c_result_t *));

PM_METAL_EXTERNAL_C(tcc, "0.9.28rc");

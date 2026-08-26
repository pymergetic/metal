/* pymergetic.metal.jit.py — compile µPy source → module object, async.
 *
 * One step: lex, parse, compile+execute into a new module dict,
 * install into sys.modules, return DONE.
 * vm_only — stepped under the async card's VM lock.
 *
 * When built without µPy (host test), all functions are stubs that
 * return NULL/error — the real compile can only run inside the µPy VM.
 */
#include "pymergetic/metal/jit/py/__exports__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#if MICROPY_PY_WASM && !PM_WASMMOD_GUEST
#include "py/compile.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/runtime.h"
#endif

#define PM_METAL_JIT_PY_ERR_MAX 512u

typedef struct {
    pm_metal_async_coro_t coro;
    pm_metal_jit_py_result_t result;
    char errbuf[PM_METAL_JIT_PY_ERR_MAX];
    char *source;
    size_t source_len;
} pm_metal_jit_py_frame_t;

pm_metal_async_coro_t *pm_metal_jit_py_compile_alloc(
    pm_util_mem_arena_t *arena,
    const char *source,
    size_t source_len,
    const char *module_name)
{
    size_t name_len;
    size_t frame_bytes;
    pm_metal_jit_py_frame_t *f;
    char *src_copy;
    if (arena == NULL || source == NULL || source_len == 0 || module_name == NULL) {
        return NULL;
    }
    name_len = strlen(module_name);
    if (name_len == 0) {
        return NULL;
    }
#if !(MICROPY_PY_WASM && !PM_WASMMOD_GUEST)
    (void)frame_bytes;
    (void)f;
    (void)src_copy;
    return NULL;
#else
    frame_bytes = sizeof(*f) + source_len + name_len + 1u;
    f = (pm_metal_jit_py_frame_t *)pm_metal_async_coro_create(
        pm_metal_jit_py_compile_step, frame_bytes);
    if (f == NULL) {
        return NULL;
    }
    src_copy = (char *)(f + 1);
    memcpy(src_copy, source, source_len);
    f->source = src_copy;
    f->source_len = source_len;
    memset(&f->result, 0, sizeof(f->result));
    f->result.module_name = src_copy + source_len;
    memcpy(f->result.module_name, module_name, name_len + 1u);
    f->result.error = NULL;
    return &f->coro;
#endif
}

void pm_metal_jit_py_result_free(pm_util_mem_arena_t *arena, pm_metal_jit_py_result_t *r) {
    (void)arena;
    (void)r;
}

pm_metal_async_status_t pm_metal_jit_py_compile_step(pm_metal_async_coro_t *self) {
#if !(MICROPY_PY_WASM && !PM_WASMMOD_GUEST)
    (void)self;
    return PM_METAL_ASYNC_ERROR;
#else
    pm_metal_jit_py_frame_t *f = (pm_metal_jit_py_frame_t *)self;
    pm_metal_jit_py_result_t *r = &f->result;
    nlr_buf_t nlr;
    qstr mod_qstr;
    mp_obj_t module;

    if (f->source == NULL || f->source_len == 0 || r->module_name == NULL) {
        r->ok = 0;
        return PM_METAL_ASYNC_ERROR;
    }
    /* Non-blocking GIL: same reasoning as step_upy — vm_only step functions
     * run under s_vm_mutex, but the REPL thread may hold the GIL concurrently.
     * Trylock; park on contention instead of blocking the runner pthread. */
    if (!MP_THREAD_GIL_TRYLOCK()) {
        return PM_METAL_ASYNC_WAITING;
    }
    mod_qstr = qstr_from_str(r->module_name);

    if (nlr_push(&nlr) == 0) {
        module = mp_obj_new_module(mod_qstr);
        mp_obj_module_t *mod = MP_OBJ_TO_PTR(module);
        mp_obj_dict_t *globals = mod->globals;

        mp_obj_dict_store(
            MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)),
            MP_OBJ_NEW_QSTR(mod_qstr), module);

        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            mod_qstr, f->source, f->source_len, 0);

        (void)mp_parse_compile_execute(
            lex, MP_PARSE_FILE_INPUT, globals, globals);
        nlr_pop();
        r->ok = 1;
    } else {
        r->ok = 0;
        r->error = NULL;
        MP_THREAD_GIL_EXIT();
        return PM_METAL_ASYNC_ERROR;
    }
    MP_THREAD_GIL_EXIT();

    return PM_METAL_ASYNC_DONE;
#endif
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.jit.py, pm_metal_jit_py_compile_alloc, pm_metal_jit_py_compile_alloc,
    pm_metal_async_coro_t *(pm_util_mem_arena_t *, const char *, size_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.py, pm_metal_jit_py_compile_step, pm_metal_jit_py_compile_step,
    pm_metal_async_status_t(pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.py, pm_metal_jit_py_result_free, pm_metal_jit_py_result_free,
    void(pm_util_mem_arena_t *, pm_metal_jit_py_result_t *));
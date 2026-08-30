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

#include <stdio.h>
#include <string.h>

#if MICROPY_PY_WASM && !PM_WASMMOD_GUEST
#include "py/compile.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/runtime.h"
/* mpthread.h defines MP_THREAD_GIL_* unconditionally (empty stubs when
 * threads are off), so TRYLOCK/EXIT below resolve on every seat including
 * the browser where MICROPY_PY_THREAD_GIL is unset. */
#include "py/mpthread.h"
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

/* --- Python object loop: source -> mpy bytes -> live module -------------
 *
 * The Python twin of jit.c's object_compile. What makes it self-host
 * shaped: the compiler in the middle is µPy itself (py/compile.c),
 * running in-process — no host tool, no subprocess. Seats without
 * MICROPY_PERSISTENT_CODE_SAVE refuse politely so callers can skip. */

#if MICROPY_PY_WASM && !PM_WASMMOD_GUEST && MICROPY_PERSISTENT_CODE_SAVE
#include "py/persistentcode.h"
#include "py/mpprint.h"
#include "py/objmodule.h"

typedef struct {
    pm_util_mem_arena_t *arena;
    uint8_t *buf;
    size_t cap;
    size_t len;
} pm_jit_py_mpy_buf_t;

static void pm_jit_py_mpy_print(void *env, const char *str, size_t len) {
    pm_jit_py_mpy_buf_t *b = (pm_jit_py_mpy_buf_t *)env;
    if (b->len + len > b->cap) {
        /* geometric grow via the arena's realloc; on exhaustion cap drops to
         * 0 and the length check after the save refuses the artifact */
        size_t want = b->cap * 2u + len;
        uint8_t *nb = (uint8_t *)pm_util_mem_realloc(b->arena, b->buf, want);
        if (nb == NULL) {
            b->cap = 0;
            return;
        }
        b->buf = nb;
        b->cap = want;
    }
    memcpy(b->buf + b->len, str, len);
    b->len += len;
}

int32_t pm_metal_jit_py_object_compile(
    pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char *module_name,
    uint8_t **mpy_out, size_t *mpy_len,
    char *errbuf, size_t errbuf_len)
{
    pm_jit_py_mpy_buf_t b;
    nlr_buf_t nlr;
    mp_obj_t module;
    mp_lexer_t *lex;
    qstr mod_qstr;
    mp_parse_tree_t pt;
    mp_compiled_module_t cm;
    mp_print_t print;

    if (arena == NULL || source == NULL || source_len == 0
        || module_name == NULL || mpy_out == NULL || mpy_len == NULL) {
        if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "object_compile: bad args");
        return -1;
    }
    *mpy_out = NULL;
    *mpy_len = 0;

    b.arena = arena;
    b.cap = source_len + 256u;
    b.len = 0;
    b.buf = (uint8_t *)pm_util_mem_alloc(arena, b.cap);
    if (b.buf == NULL) {
        if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "object_compile: arena alloc failed");
        return -1;
    }

    if (!MP_THREAD_GIL_TRYLOCK()) {
        if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "object_compile: GIL busy");
        return -1;
    }

    mod_qstr = qstr_from_str(module_name);
    if (nlr_push(&nlr) == 0) {
        module = mp_obj_new_module(mod_qstr);
        /* context is the module the raw code will run against — the dict
         * mp_obj_new_module just created (builtinevex passes a fresh context
         * the same way before mp_compile_to_raw_code) */
        mp_module_context_t ctx;
        ctx.module.globals = mp_obj_module_get_globals(module);
        memset(&cm, 0, sizeof(cm)); /* arch_flags: compile_to_raw_code leaves
         * it to the caller — garbage there poisons the mpy feature byte */
        cm.context = &ctx;
        lex = mp_lexer_new_from_str_len(mod_qstr, source, source_len, 0);
        pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_compile_to_raw_code(&pt, mod_qstr, false, &cm);
        print.data = &b;
        print.print_strn = pm_jit_py_mpy_print;
        b.len = 0;
        mp_raw_code_save(&cm, &print);
        nlr_pop();
    } else {
        MP_THREAD_GIL_EXIT();
        if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "object_compile: µPy raised");
        return -1;
    }
    MP_THREAD_GIL_EXIT();

    if (b.len == 0 || b.cap == 0) {
        if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "object_compile: save produced nothing");
        return -1;
    }
    *mpy_out = b.buf;
    *mpy_len = b.len;
    return 0;
}

int32_t pm_metal_jit_py_object_load(
    pm_util_mem_arena_t *arena,
    const uint8_t *mpy, size_t mpy_len,
    const char *module_name,
    char *errbuf, size_t errbuf_len)
{
    nlr_buf_t nlr;
    mp_obj_t module;
    mp_module_context_t *context;
    mp_compiled_module_t cm;
    qstr mod_qstr;
    mp_obj_dict_t *saved_globals;
    mp_obj_dict_t *saved_locals;

    (void)arena;
    if (mpy == NULL || mpy_len < 4 || module_name == NULL) {
        if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "object_load: bad args");
        return -1;
    }

    if (!MP_THREAD_GIL_TRYLOCK()) {
        if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "object_load: GIL busy");
        return -1;
    }

    mod_qstr = qstr_from_str(module_name);
    if (nlr_push(&nlr) == 0) {
        module = mp_obj_new_module(mod_qstr);
        context = (mp_module_context_t *)MP_OBJ_TO_PTR(module);
        cm.context = context;
        mp_raw_code_load_mem(mpy, mpy_len, &cm);

        /* run the module body with its own globals installed; restore both
         * on the way out even when the body raises (packbind's pattern) */
        mp_obj_dict_t *mod_globals = context->module.globals;
        saved_globals = mp_globals_get();
        saved_locals = mp_locals_get();
        mp_globals_set(mod_globals);
        mp_locals_set(mod_globals);
        mp_obj_t module_fun = mp_make_function_from_proto_fun(cm.rc, context, NULL);
        mp_call_function_0(module_fun);
        mp_globals_set(saved_globals);
        mp_locals_set(saved_locals);
        nlr_pop();
    } else {
        MP_THREAD_GIL_EXIT();
        if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "object_load: µPy raised");
        return -1;
    }
    MP_THREAD_GIL_EXIT();
    return 0;
}

#else /* no µPy or no PERSISTENT_CODE_SAVE: polite refusal */

int32_t pm_metal_jit_py_object_compile(
    pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char *module_name,
    uint8_t **mpy_out, size_t *mpy_len,
    char *errbuf, size_t errbuf_len)
{
    (void)arena; (void)source; (void)source_len; (void)module_name;
    (void)mpy_out; (void)mpy_len;
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len,
            "object_compile: no mpy artifact output on this seat");
    }
    return -1;
}

int32_t pm_metal_jit_py_object_load(
    pm_util_mem_arena_t *arena,
    const uint8_t *mpy, size_t mpy_len,
    const char *module_name,
    char *errbuf, size_t errbuf_len)
{
    (void)arena; (void)mpy; (void)mpy_len; (void)module_name;
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len,
            "object_load: no mpy artifact input on this seat");
    }
    return -1;
}

#endif /* MICROPY_PY_WASM && !PM_WASMMOD_GUEST && MICROPY_PERSISTENT_CODE_SAVE */

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.jit.py, pm_metal_jit_py_compile_alloc, pm_metal_jit_py_compile_alloc,
    pm_metal_async_coro_t *(pm_util_mem_arena_t *, const char *, size_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.py, pm_metal_jit_py_compile_step, pm_metal_jit_py_compile_step,
    pm_metal_async_status_t(pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.py, pm_metal_jit_py_result_free, pm_metal_jit_py_result_free,
    void(pm_util_mem_arena_t *, pm_metal_jit_py_result_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.py, pm_metal_jit_py_object_compile, pm_metal_jit_py_object_compile,
    int32_t(pm_util_mem_arena_t *, const char *, size_t, const char *, uint8_t **, size_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.jit.py, pm_metal_jit_py_object_load, pm_metal_jit_py_object_load,
    int32_t(pm_util_mem_arena_t *, const uint8_t *, size_t, const char *, char *, size_t));
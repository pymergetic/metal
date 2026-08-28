#include "pymergetic/metal/jit/c/__exports__.h"
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/util/mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PM_METAL_JIT_C_ERR_MAX 256u
#define PM_METAL_JIT_C_WASM_CAP (256u * 1024u)

typedef struct {
    pm_metal_async_coro_t coro;
    /* Tag checked by pm_metal_jit_c_result_of — a pointer compare against
     * this TU's compile_step would reject a rebuilt (re-linked) copy of the
     * same card, whose step fn lives at a different address. */
    uint32_t magic;
    pm_metal_jit_c_result_t result;
    char errbuf[PM_METAL_JIT_C_ERR_MAX];
    uint8_t wasmbuf[PM_METAL_JIT_C_WASM_CAP];
    char *source;
    size_t source_len;
} pm_metal_jit_c_frame_t;

#define PM_METAL_JIT_C_FRAME_MAGIC 0x4a495443u /* "JITC" */

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
    f->magic = PM_METAL_JIT_C_FRAME_MAGIC;
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

#if PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
#define PM_METAL_JIT_C_OBJECT_PATH 1
/* Object path (multi-object build): compile to ET_REL .o via tcc_output_file.
 * Same temp-file convention as the jit.rs mrustc embed (/tmp/.jit_*). */
#include <unistd.h>
#include <fcntl.h>

static void jit_c_obj_err(char *errbuf, size_t errbuf_len, const char *msg) {
    if (errbuf == NULL || errbuf_len == 0) return;
    snprintf(errbuf, errbuf_len, "%s", msg);
}

int32_t pm_metal_jit_c_object_compile_opts(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    char tmpl[] = "/tmp/.jit_c_obj_XXXXXX";
    int fd;
    FILE *f;
    long n;
    uint8_t *buf;
    TCCState *s;
    uint32_t i;

    if (arena == NULL || source == NULL || source_len == 0
        || obj_out == NULL || obj_len == NULL) {
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: bad args");
        return -1;
    }
    if ((include_dirs == NULL && n_include_dirs != 0)
        || (defines == NULL && n_defines != 0)) {
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: bad args");
        return -1;
    }
    *obj_out = NULL;
    *obj_len = 0;

    fd = mkstemp(tmpl);
    if (fd < 0) {
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: mkstemp failed");
        return -1;
    }
    close(fd);

    s = tcc_new();
    if (s == NULL) {
        unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: tcc_new failed");
        return -1;
    }
    tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_set_output_type(s, TCC_OUTPUT_OBJ);
    for (i = 0; i < n_include_dirs; i++) {
        if (include_dirs[i] != NULL && include_dirs[i][0] != '\0') {
            tcc_add_include_path(s, include_dirs[i]);
        }
    }
    for (i = 0; i < n_defines; i++) {
        if (defines[i] != NULL && defines[i][0] != '\0') {
            /* "NAME" defines to 1; "NAME=VALUE" splits on the first '=' —
             * tcc_define_symbol implements exactly that split. */
            tcc_define_symbol(s, defines[i], NULL);
        }
    }
    if (tcc_compile_string(s, source) != 0) {
        tcc_delete(s); unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: tcc compile failed");
        return -1;
    }
    if (tcc_output_file(s, tmpl) != 0) {
        tcc_delete(s); unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: tcc_output_file failed");
        return -1;
    }
    tcc_delete(s);

    f = fopen(tmpl, "rb");
    if (f == NULL) {
        unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: reopen failed");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    if (n <= 0) {
        fclose(f); unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: empty object");
        return -1;
    }
    buf = (uint8_t *)pm_util_mem_alloc(arena, (size_t)n);
    if (buf == NULL) {
        fclose(f); unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: arena alloc failed");
        return -1;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: short read");
        return -1;
    }
    fclose(f);
    unlink(tmpl);
    *obj_out = buf;
    *obj_len = (size_t)n;
    return 0;
}

int32_t pm_metal_jit_c_object_compile(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    return pm_metal_jit_c_object_compile_opts(arena, source, source_len,
        NULL, 0, NULL, 0, obj_out, obj_len, errbuf, errbuf_len);
}
#else
#if PM_HAS_TCC && defined(TCC_TARGET_WASM32)
/* WASM object path (browser seat): the wasm32 backend serializes the module
 * directly from its code buffer (wasm_build_module), so the "object" is the
 * module itself — the loader instantiates it and the registry publishes its
 * named exports. No temp file: bytes go straight into the arena. */
int32_t pm_metal_jit_c_object_compile_opts(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    TCCState *s;
    uint8_t *mod = NULL;
    int mod_len = 0;
    uint8_t *buf;
    uint32_t i;

    if (arena == NULL || source == NULL || source_len == 0
        || obj_out == NULL || obj_len == NULL) {
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: bad args");
        }
        return -1;
    }
    if ((include_dirs == NULL && n_include_dirs != 0)
        || (defines == NULL && n_defines != 0)) {
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: bad args");
        }
        return -1;
    }
    *obj_out = NULL;
    *obj_len = 0;

    s = tcc_new();
    if (s == NULL) {
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: tcc_new failed");
        }
        return -1;
    }
    tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    for (i = 0; i < n_include_dirs; i++) {
        if (include_dirs[i] != NULL && include_dirs[i][0] != '\0') {
            tcc_add_include_path(s, include_dirs[i]);
        }
    }
    for (i = 0; i < n_defines; i++) {
        if (defines[i] != NULL && defines[i][0] != '\0') {
            tcc_define_symbol(s, defines[i], NULL);
        }
    }
    if (tcc_compile_string(s, source) != 0) {
        tcc_delete(s);
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: tcc compile failed");
        }
        return -1;
    }
    if (wasm_build_module(&mod, &mod_len) != 0 || mod == NULL || mod_len <= 0) {
        tcc_delete(s);
        if (mod != NULL) {
            free(mod);
        }
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: wasm serialize failed");
        }
        return -1;
    }
    tcc_delete(s);
    buf = (uint8_t *)pm_util_mem_alloc(arena, (size_t)mod_len);
    if (buf == NULL) {
        free(mod);
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: arena alloc failed");
        }
        return -1;
    }
    memcpy(buf, mod, (size_t)mod_len);
    free(mod);
    *obj_out = buf;
    *obj_len = (size_t)mod_len;
    return 0;
}

int32_t pm_metal_jit_c_object_compile(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    return pm_metal_jit_c_object_compile_opts(arena, source, source_len,
        NULL, 0, NULL, 0, obj_out, obj_len, errbuf, errbuf_len);
}
#else
int32_t pm_metal_jit_c_object_compile_opts(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    (void)arena; (void)source; (void)source_len;
    (void)include_dirs; (void)n_include_dirs;
    (void)defines; (void)n_defines;
    (void)obj_out; (void)obj_len;
    if (errbuf != NULL && errbuf_len > 0) {
        snprintf(errbuf, errbuf_len,
            "object_compile: no native object output on this seat");
    }
    return -1;
}

int32_t pm_metal_jit_c_object_compile(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    return pm_metal_jit_c_object_compile_opts(arena, source, source_len,
        NULL, 0, NULL, 0, obj_out, obj_len, errbuf, errbuf_len);
}
#endif /* PM_HAS_TCC && TCC_TARGET_WASM32 */
#endif /* PM_HAS_TCC && !TCC_TARGET_WASM32 */

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

const pm_metal_jit_c_result_t *pm_metal_jit_c_result_of(
    const pm_metal_async_coro_t *self) {
    const pm_metal_jit_c_frame_t *f;
    if (self == NULL) {
        return NULL;
    }
    f = (const pm_metal_jit_c_frame_t *)self;
    if (f->magic != PM_METAL_JIT_C_FRAME_MAGIC) {
        return NULL;
    }
    return &f->result;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_compile_alloc, pm_metal_jit_c_compile_alloc,
    pm_metal_async_coro_t *(pm_util_mem_arena_t *, const char *, size_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_compile_step, pm_metal_jit_c_compile_step,
    pm_metal_async_status_t(pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_result_free, pm_metal_jit_c_result_free,
    void(pm_util_mem_arena_t *, pm_metal_jit_c_result_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_result_of, pm_metal_jit_c_result_of,
    const pm_metal_jit_c_result_t *(const pm_metal_async_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_object_compile, pm_metal_jit_c_object_compile,
    int32_t(pm_util_mem_arena_t *, const char *, size_t,
        uint8_t **, size_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_object_compile_opts, pm_metal_jit_c_object_compile_opts,
    int32_t(pm_util_mem_arena_t *, const char *, size_t,
        const char **, uint32_t, const char **, uint32_t,
        uint8_t **, size_t *, char *, size_t));

PM_METAL_EXTERNAL_C(tcc, "0.9.28rc");

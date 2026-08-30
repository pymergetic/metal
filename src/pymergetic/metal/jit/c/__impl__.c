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
/* tcc.h's pub allocators, declared by hand: including tcc.h itself is not an
 * option from a card — it #defines free() to poison stray libc calls and its
 * inline DWARF helpers trip the host -Werror build. Signatures mirror the
 * MEM_DEBUG-off PUB_FUNC prototypes in tcc.h (our libtcc builds never define
 * MEM_DEBUG). */
extern void tcc_free(void *ptr);
extern void *tcc_malloc(unsigned long size);

/* Arena-backed TCC allocations: tcc_set_realloc routes every tcc_malloc /
 * tcc_realloc / tcc_free the compiler does through the card arena, so a
 * compile's scratch lives and dies with the arena instead of libc heap.
 * TCC has one contract plain tlsf doesn't give: max_align_t (16B) alignment
 * — glibc malloc guarantees it, TCC's SValue/Sym tables store int64/double
 * inline, and misaligned access corrupts deep expression parsing (observed:
 * block() SIGSEGV during the libtcc.c self-compile). Fresh allocs go through
 * pm_util_mem_memalign; growth stays on tlsf_realloc, which copies exactly
 * min(block, requested) bytes — a hand-rolled alloc/copy/free here cannot
 * know the requested size and over-copies into the next block (that was the
 * small-source SIGSEGV in tccelf_begin_file). tcc's reallocator is one
 * global, so the arena goes in a TU-static and the compile calls
 * save/restore around their arena window — the card contract is sequential
 * compiles (same posture as the nativecall artifact slot). */
static pm_util_mem_arena_t *s_tcc_arena;

static void *pm_metal_jit_c_tcc_arena_realloc(void *ptr, unsigned long size) {
    if (s_tcc_arena == NULL) {
        return NULL;
    }
    if (size == 0) {
        pm_util_mem_free(s_tcc_arena, ptr);
        return NULL;
    }
    if (ptr == NULL) {
        return pm_util_mem_memalign(s_tcc_arena, 16u, (size_t)size);
    }
    return pm_util_mem_realloc(s_tcc_arena, ptr, (size_t)size);
}

#if PM_HAS_TCC && defined(TCC_TARGET_WASM32)
#define PM_METAL_JIT_C_WASM_PATH 1
#endif
#if PM_HAS_TCC && defined(PM_METAL_TCC_CROSS_WASM32)
#define PM_METAL_JIT_C_WASM_PATH 1
#endif

#if PM_METAL_JIT_C_WASM_PATH
#ifdef TCC_TARGET_WASM32
/* WASM backend: compile and serialize the WASM module via wasm_build_module() */
int wasm_build_module(uint8_t **out_buf, int *out_len);
void wasm_release_buffers(void);
/* thin shims so both wasm-path bodies below read identically: on the
 * wasm-native seat the instance API is the unprefixed libtcc one */
static void *wasm_tcc_new(void) { return tcc_new(); }
static void wasm_tcc_delete(void *s) { tcc_delete((TCCState *)s); }
static void wasm_tcc_set_lib_path(void *s, const char *p) { tcc_set_lib_path((TCCState *)s, p); }
static void wasm_tcc_add_library_path(void *s, const char *p) { tcc_add_library_path((TCCState *)s, p); }
static void wasm_tcc_add_include_path(void *s, const char *p) { tcc_add_include_path((TCCState *)s, p); }
static void wasm_tcc_set_output_type(void *s, int t) { tcc_set_output_type((TCCState *)s, t); }
static int wasm_tcc_compile_string(void *s, const char *b) { return tcc_compile_string((TCCState *)s, b); }
static void wasm_tcc_define_symbol(void *s, const char *sym, const char *val) { tcc_define_symbol((TCCState *)s, sym, val); }
static void wasm_tcc_set_realloc(void *f) { tcc_set_realloc((TCCReallocFunc *)f); }
static void wasm_tcc_free(void *p) { tcc_free(p); }
static int wasm_build_mod(uint8_t **out_buf, int *out_len) { return wasm_build_module(out_buf, out_len); }
static void wasm_release_bufs(void) { wasm_release_buffers(); }
#else
/* Cross-compiled wasm32 instance (ELF seat): same libtcc API, symbol-
 * prefixed so the two backends coexist in one binary. The wasm instance's
 * TCCState layout differs from the native one — only opaque-pointer calls
 * cross this seam. */
extern void *pm_tccw_tcc_new(void);
extern void pm_tccw_tcc_delete(void *s);
extern void pm_tccw_tcc_set_lib_path(void *s, const char *p);
extern void pm_tccw_tcc_add_library_path(void *s, const char *p);
extern void pm_tccw_tcc_add_include_path(void *s, const char *p);
extern void pm_tccw_tcc_set_output_type(void *s, int t);
extern int pm_tccw_tcc_compile_string(void *s, const char *b);
extern void pm_tccw_tcc_define_symbol(void *s, const char *sym, const char *val);
extern void pm_tccw_tcc_set_realloc(void *f);
extern void pm_tccw_tcc_free(void *ptr);
extern int pm_tccw_wasm_build_module(uint8_t **out_buf, int *out_len);
extern void pm_tccw_wasm_release_buffers(void);
static void *wasm_tcc_new(void) { return pm_tccw_tcc_new(); }
static void wasm_tcc_delete(void *s) { pm_tccw_tcc_delete(s); }
static void wasm_tcc_set_lib_path(void *s, const char *p) { pm_tccw_tcc_set_lib_path(s, p); }
static void wasm_tcc_add_library_path(void *s, const char *p) { pm_tccw_tcc_add_library_path(s, p); }
static void wasm_tcc_add_include_path(void *s, const char *p) { pm_tccw_tcc_add_include_path(s, p); }
static void wasm_tcc_set_output_type(void *s, int t) { pm_tccw_tcc_set_output_type(s, t); }
static int wasm_tcc_compile_string(void *s, const char *b) { return pm_tccw_tcc_compile_string(s, b); }
static void wasm_tcc_define_symbol(void *s, const char *sym, const char *val) { pm_tccw_tcc_define_symbol(s, sym, val); }
static void wasm_tcc_set_realloc(void *f) { pm_tccw_tcc_set_realloc(f); }
static void wasm_tcc_free(void *p) { pm_tccw_tcc_free(p); }
static int wasm_build_mod(uint8_t **out_buf, int *out_len) { return pm_tccw_wasm_build_module(out_buf, out_len); }
static void wasm_release_bufs(void) { pm_tccw_wasm_release_buffers(); }
#endif

/* unused on cross seats' coro face (native_entry is the seat's own backend)
 * — object_compile_target is the only caller there */
#if defined(PM_METAL_TCC_CROSS_WASM32) && !defined(TCC_TARGET_WASM32)
__attribute__((unused))
#endif
static int pm_metal_jit_c_tcc_wasm_compile(const char *source,
    uint8_t *wasm_out, size_t wasm_cap, size_t *wasm_len) {
    /* opaque on cross seats: the wasm instance's TCCState layout differs
     * from the native instance's — only the prefixed API touches it */
    void *s;
    uint8_t *buf = NULL;
    int len = 0;
    /* the coro face carries no arena of its own — the compile's scratch
     * (and wasm32-gen's growable emission buffers) go through the boot
     * arena (async_init's) */
    pm_util_mem_arena_t *a = pm_metal_async_arena();
    if (!a) return -1;
    s_tcc_arena = a;
    wasm_tcc_set_realloc(pm_metal_jit_c_tcc_arena_realloc);
    s = wasm_tcc_new();
    if (!s) {
        wasm_tcc_set_realloc(NULL);
        s_tcc_arena = NULL;
        return -1;
    }
    wasm_tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    wasm_tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    wasm_tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (wasm_tcc_compile_string(s, source) != 0) {
        wasm_tcc_delete(s);
        wasm_release_bufs();
        wasm_tcc_set_realloc(NULL);
        s_tcc_arena = NULL;
        return -1;
    }
    if (wasm_build_mod(&buf, &len) != 0 || !buf) {
        wasm_tcc_delete(s);
        wasm_release_bufs();
        wasm_tcc_set_realloc(NULL);
        s_tcc_arena = NULL;
        return -1;
    }
    wasm_tcc_delete(s);
    /* copy out while the arena reallocator still owns buf's allocator,
     * then free it under the same window (tcc_free after the restore would
     * be libc free on an arena block) */
    if (len > (int)wasm_cap) {
        wasm_tcc_free(buf);
        wasm_release_bufs();
        wasm_tcc_set_realloc(NULL);
        s_tcc_arena = NULL;
        return -1;
    }
    memcpy(wasm_out, buf, (size_t)len);
    *wasm_len = (size_t)len;
    wasm_tcc_free(buf);
    wasm_release_bufs();
    wasm_tcc_set_realloc(NULL);
    s_tcc_arena = NULL;
    return 0;
}
#endif /* PM_METAL_JIT_C_WASM_PATH */

#if PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
/* Native (x86_64) backend: compile and relocate. The whole compile stays on
 * the default (libc) reallocator on purpose: tcc_relocate's run image is
 * mprotect'ed RX from the block rt_mem allocates (libc heap semantics), and
 * tcc_delete frees that image and the state tables in one window — no split
 * allocator can serve both correctly. The in-kernel object path
 * (object_compile_opts, the Rust->C->object loop) carries the arena routing. */
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
#else
/* wasm32-native seat: the coro face never calls this (its compile_step
 * drives the wasm path) — present only for link shape */
__attribute__((unused))
static int pm_metal_jit_c_tcc_native_compile(const char *source, pm_metal_jit_c_result_t *r) {
    (void)source; (void)r; return -1;
}
#endif

#else /* !PM_HAS_TCC */
static int pm_metal_jit_c_tcc_native_compile(const char *source, pm_metal_jit_c_result_t *r) {
    (void)source; (void)r; return -1;
}
static int pm_metal_jit_c_tcc_wasm_compile(const char *source,
    uint8_t *wasm_out, size_t wasm_cap, size_t *wasm_len) {
    (void)source; (void)wasm_out; (void)wasm_cap; (void)wasm_len;
    return -1;
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

/* Capture TCC's own diagnostics so a refused compile names the real cause
 * (file:line + message), not just "compile failed". The callback receives
 * the raw message; the retained prefix survives into errbuf. */
static char *s_jit_c_diag;
static size_t s_jit_c_diag_len;
static size_t s_jit_c_diag_max;

static void jit_c_diag_cb(void *opaque, const char *msg) {
    size_t n;
    (void)opaque;
    if (msg == NULL || s_jit_c_diag == NULL || s_jit_c_diag_max == 0) return;
    /* Keep only the most recent lines: TCC emits include-stack prefixes
     * first and the error line last, so a full buffer would evict the
     * cause. Roll the buffer when this line does not fit. */
    n = strlen(msg);
    if (n >= s_jit_c_diag_max - 1) {
        /* a single line longer than the buffer: keep its tail */
        s_jit_c_diag_len = 0;
        memcpy(s_jit_c_diag, msg + (n - (s_jit_c_diag_max - 2)),
            s_jit_c_diag_max - 2);
        s_jit_c_diag_len = s_jit_c_diag_max - 2;
        s_jit_c_diag[s_jit_c_diag_len++] = '\n';
        s_jit_c_diag[s_jit_c_diag_len] = '\0';
        return;
    }
    if (s_jit_c_diag_len + n + 1 >= s_jit_c_diag_max) {
        /* drop the oldest lines until the new one fits */
        size_t need = n + 2;
        size_t drop = 0;
        while (s_jit_c_diag_len - drop >= need
            && drop < s_jit_c_diag_len) {
            /* advance one line */
            size_t adv = drop;
            while (adv < s_jit_c_diag_len
                && s_jit_c_diag[adv] != '\n') {
                adv++;
            }
            if (adv < s_jit_c_diag_len) adv++;
            drop = adv;
        }
        if (drop > 0 && drop < s_jit_c_diag_len) {
            memmove(s_jit_c_diag, s_jit_c_diag + drop,
                s_jit_c_diag_len - drop);
            s_jit_c_diag_len -= drop;
        } else if (drop >= s_jit_c_diag_len) {
            s_jit_c_diag_len = 0;
        }
    }
    if (s_jit_c_diag_len + n + 1 < s_jit_c_diag_max) {
        memcpy(s_jit_c_diag + s_jit_c_diag_len, msg, n);
        s_jit_c_diag_len += n;
        s_jit_c_diag[s_jit_c_diag_len++] = '\n';
        s_jit_c_diag[s_jit_c_diag_len] = '\0';
    }
}

static void jit_c_diag_begin(char *buf, size_t cap) {
    s_jit_c_diag = buf;
    s_jit_c_diag_len = 0;
    s_jit_c_diag_max = cap;
    if (buf != NULL && cap > 0) buf[0] = '\0';
}

/* Drop the capture (success path): the diagnostics were only interesting
 * on refusal. */
static void jit_c_diag_end(void) {
    s_jit_c_diag = NULL;
    s_jit_c_diag_len = 0;
    s_jit_c_diag_max = 0;
}

/* Fold the retained diagnostics into errbuf (kept when non-empty; the
 * "compile failed" prefix stays so callers still see the stage). TCC
 * diagnostics end with the error line, so when the whole capture does
 * not fit, keep the tail — the last lines carry the cause. */
static void jit_c_obj_err_diag(char *errbuf, size_t errbuf_len,
    const char *msg) {
    if (errbuf == NULL || errbuf_len == 0) return;
    if (s_jit_c_diag != NULL && s_jit_c_diag[0] != '\0') {
        size_t msg_len = strlen(msg);
        size_t room = errbuf_len > msg_len + 2
            ? errbuf_len - msg_len - 2 : 0;
        const char *diag = s_jit_c_diag;
        size_t skip = 0;
        if (s_jit_c_diag_len + msg_len + 2 > errbuf_len) {
            if (s_jit_c_diag_len > room) {
                skip = s_jit_c_diag_len - room;
                /* advance to the next line so the tail starts clean */
                while (diag[skip] != '\0' && diag[skip] != '\n'
                    && skip < s_jit_c_diag_len) {
                    skip++;
                }
                if (skip < s_jit_c_diag_len) skip++;
            }
            diag = s_jit_c_diag + skip;
        }
        snprintf(errbuf, errbuf_len, "%s: %s", msg, diag);
    } else {
        snprintf(errbuf, errbuf_len, "%s", msg);
    }
    s_jit_c_diag = NULL;
    s_jit_c_diag_len = 0;
    s_jit_c_diag_max = 0;
}

static int32_t jit_c_object_compile_native(pm_util_mem_arena_t *arena,
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

    /* route the whole compile's allocations through the arena (save the
     * prior reallocator — tcc's is a single global) */
    TCCReallocFunc *saved_realloc = NULL;
    s_tcc_arena = arena;
    tcc_set_realloc(pm_metal_jit_c_tcc_arena_realloc);
    /* the arena's own reallocation can move a block tcc still holds, but
     * tlsf_realloc copies contents — same contract as libc realloc */

    s = tcc_new();
    if (s == NULL) {
        tcc_set_realloc(saved_realloc);
        s_tcc_arena = NULL;
        unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: tcc_new failed");
        return -1;
    }
    tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_set_output_type(s, TCC_OUTPUT_OBJ);
    /* route diagnostics into a scratch buffer folded into errbuf on
     * refusal — the capture rides this function's stack frame */
    {
        char diag[1024];
        jit_c_diag_begin(diag, sizeof(diag));
        tcc_set_error_func(s, NULL, jit_c_diag_cb);
    }
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
        tcc_delete(s);
        tcc_set_realloc(saved_realloc);
        s_tcc_arena = NULL;
        unlink(tmpl);
        jit_c_obj_err_diag(errbuf, errbuf_len, "object_compile: tcc compile failed");
        return -1;
    }
    if (tcc_output_file(s, tmpl) != 0) {
        tcc_delete(s);
        tcc_set_realloc(saved_realloc);
        s_tcc_arena = NULL;
        unlink(tmpl);
        jit_c_obj_err_diag(errbuf, errbuf_len, "object_compile: tcc_output_file failed");
        return -1;
    }
    tcc_delete(s);
    /* restore the prior reallocator before the read-back — no tcc allocation
     * happens below this point */
    tcc_set_realloc(saved_realloc);
    s_tcc_arena = NULL;
    jit_c_diag_end();

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
#else
/* wasm32-native seat: never called (the seat router picks the wasm path) —
 * present only for the call shape */
__attribute__((unused))
static int32_t jit_c_object_compile_native(pm_util_mem_arena_t *arena,
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
#endif /* PM_HAS_TCC && !TCC_TARGET_WASM32 */

#if PM_METAL_JIT_C_WASM_PATH
/* WASM object path (wasm32 seats and cross seats): the wasm32 backend
 * serializes the module directly from its code buffer (wasm_build_module),
 * so the "object" is the module itself — the loader instantiates it and the
 * registry publishes its named exports. No temp file: bytes go straight
 * into the arena. */
static int32_t jit_c_object_compile_wasm(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    void *s;
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

    /* the compile's scratch — and wasm32-gen's growable buffers — ride the
     * caller's arena; saved/restored because tcc's reallocator is global */
    s_tcc_arena = arena;
    wasm_tcc_set_realloc(pm_metal_jit_c_tcc_arena_realloc);
    s = wasm_tcc_new();
    if (s == NULL) {
        wasm_tcc_set_realloc(NULL);
        s_tcc_arena = NULL;
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: tcc_new failed");
        }
        return -1;
    }
    wasm_tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    wasm_tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    wasm_tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    for (i = 0; i < n_include_dirs; i++) {
        if (include_dirs[i] != NULL && include_dirs[i][0] != '\0') {
            wasm_tcc_add_include_path(s, include_dirs[i]);
        }
    }
    for (i = 0; i < n_defines; i++) {
        if (defines[i] != NULL && defines[i][0] != '\0') {
            wasm_tcc_define_symbol(s, defines[i], NULL);
        }
    }
    if (wasm_tcc_compile_string(s, source) != 0) {
        wasm_tcc_delete(s);
        wasm_release_bufs(); /* partial emission still rode this arena */
        wasm_tcc_set_realloc(NULL);
        s_tcc_arena = NULL;
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: tcc compile failed");
        }
        return -1;
    }
    if (wasm_build_mod(&mod, &mod_len) != 0 || mod == NULL || mod_len <= 0) {
        if (mod != NULL) {
            wasm_tcc_free(mod); /* free under the arena window that allocated it */
        }
        wasm_tcc_delete(s);
        wasm_release_bufs(); /* same window: their backing is this arena */
        wasm_tcc_set_realloc(NULL);
        s_tcc_arena = NULL;
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: wasm serialize failed");
        }
        return -1;
    }
    wasm_tcc_delete(s);
    buf = (uint8_t *)pm_util_mem_alloc(arena, (size_t)mod_len);
    if (buf == NULL) {
        wasm_tcc_free(mod); /* still inside the arena window */
        wasm_release_bufs();
        wasm_tcc_set_realloc(NULL);
        s_tcc_arena = NULL;
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: arena alloc failed");
        }
        return -1;
    }
    memcpy(buf, mod, (size_t)mod_len);
    wasm_tcc_free(mod); /* must free before the restore — tcc_free after would be
     * libc free on an arena block */
    wasm_release_bufs();
    wasm_tcc_set_realloc(NULL);
    s_tcc_arena = NULL;
    *obj_out = buf;
    *obj_len = (size_t)mod_len;
    return 0;
}
#else
static int32_t jit_c_object_compile_wasm(pm_util_mem_arena_t *arena,
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
            "object_compile: no wasm32 backend on this seat");
    }
    return -1;
}
#endif /* PM_METAL_JIT_C_WASM_PATH */

int32_t pm_metal_jit_c_object_compile_opts(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    /* seat routing, unchanged from before the target knob: the wasm32 seat's
     * "native" object IS the wasm module. */
#if defined(TCC_TARGET_WASM32) && PM_HAS_TCC
    return jit_c_object_compile_wasm(arena, source, source_len,
        include_dirs, n_include_dirs, defines, n_defines,
        obj_out, obj_len, errbuf, errbuf_len);
#else
    return jit_c_object_compile_native(arena, source, source_len,
        include_dirs, n_include_dirs, defines, n_defines,
        obj_out, obj_len, errbuf, errbuf_len);
#endif
}

int32_t pm_metal_jit_c_object_compile_target(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    int32_t target,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    if (target == (int32_t)PM_METAL_JIT_C_TARGET_WASM32) {
#if PM_METAL_JIT_C_WASM_PATH
        return jit_c_object_compile_wasm(arena, source, source_len,
            include_dirs, n_include_dirs, defines, n_defines,
            obj_out, obj_len, errbuf, errbuf_len);
#else
        (void)arena; (void)source; (void)source_len;
        (void)include_dirs; (void)n_include_dirs;
        (void)defines; (void)n_defines;
        (void)obj_out; (void)obj_len;
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len,
                "object_compile: wasm32 target not available on this seat");
        }
        return -1;
#endif
    }
    /* TARGET_SEAT: the native backend this binary embeds. On the wasm32
     * seat that IS the wasm path — same object, same loader. */
#if defined(TCC_TARGET_WASM32) && PM_HAS_TCC
    return jit_c_object_compile_wasm(arena, source, source_len,
        include_dirs, n_include_dirs, defines, n_defines,
        obj_out, obj_len, errbuf, errbuf_len);
#else
    return jit_c_object_compile_native(arena, source, source_len,
        include_dirs, n_include_dirs, defines, n_defines,
        obj_out, obj_len, errbuf, errbuf_len);
#endif
}

int32_t pm_metal_jit_c_object_compile(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    return pm_metal_jit_c_object_compile_opts(arena, source, source_len,
        NULL, 0, NULL, 0, obj_out, obj_len, errbuf, errbuf_len);
}

pm_metal_async_status_t pm_metal_jit_c_compile_step(pm_metal_async_coro_t *self) {
    if (!self) return PM_METAL_ASYNC_ERROR;
    pm_metal_jit_c_frame_t *f = (pm_metal_jit_c_frame_t *)self;
    if (!f->source || !f->source_len) return PM_METAL_ASYNC_ERROR;
#if PM_HAS_TCC && defined(TCC_TARGET_WASM32)
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
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_object_compile_target, pm_metal_jit_c_object_compile_target,
    int32_t(pm_util_mem_arena_t *, const char *, size_t,
        const char **, uint32_t, const char **, uint32_t,
        int32_t,
        uint8_t **, size_t *, char *, size_t));

PM_METAL_EXTERNAL_C(tcc, "0.9.28rc");

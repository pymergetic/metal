/* mkstemp: POSIX, but musl (the browser seat's libc) declares it from
 * stdlib.h, glibc from unistd.h — either way only with a feature-test
 * macro under -std=c99. Defined before the first libc include; jit.c's
 * object path (native or a cross instance, ELF backends all) writes its
 * ET_REL object through a mkstemp temp file.
 */
#define _GNU_SOURCE 1
#include "pymergetic/metal/jit/c/__exports__.h"
#include "pymergetic/metal/coop.h"
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/util/mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PM_METAL_JIT_C_ERR_MAX 256u
#define PM_METAL_JIT_C_WASM_CAP (256u * 1024u)

typedef struct {
    pm_metal_coop_coro_t coro;
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
 * compiles (same posture as the nativecall artifact slot).
 *
 * Phase 5: the window is a first-class lock-guarded API. One pm_util_lock_t
 * serializes every window transition; the arena static and the global
 * reallocator are only written under it. object_compile_opts takes the
 * window around its whole invocation; the build actor takes it around a
 * whole unit compile. Nested windows are refused (the inner caller would
 * restore the outer's reallocator early). */
static pm_util_mem_arena_t *s_tcc_arena;
static pm_util_lock_t s_tcc_arena_lock;   /* 0 = free: BSS zero is unlocked */
static pm_util_mem_arena_t *s_tcc_arena_holder;  /* window owner when held */

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

int32_t pm_metal_jit_c_arena_acquire(pm_util_mem_arena_t *arena);

int32_t pm_metal_jit_c_arena_release(pm_util_mem_arena_t *arena);

#if PM_HAS_TCC && defined(TCC_TARGET_WASM32)
#define PM_METAL_JIT_C_WASM_PATH 1
#endif
#if PM_HAS_TCC && defined(PM_METAL_TCC_CROSS_WASM32)
#define PM_METAL_JIT_C_WASM_PATH 1
#endif

/*------------------ cross-instance seam (one card, N instances) -----
 * Every cross instance is one libtcc.c compiled for its backend with all
 * defined globals renamed <prefix><sym> (tools/tcc_instances.mk +
 * tools/tcc_prefix_syms.sh — rename-ALL; the old tcc_-and-wasm_ grep
 * leaked gen_negf, which wasm32 and arm both define). Each instance's
 * TCCState layout differs from the native one, so only opaque-pointer
 * calls cross this seam. Declare one with PM_TCC_CROSS_INSTANCE(id,
 * prefix): the macro declares the prefixed API as externs and defines
 * static inlines named <id>_tcc_new / ... that forward to it. The API
 * set is exactly what the object paths below call — grow it in the
 * macro, never per-instance. */

#if PM_HAS_TCC && defined(PM_METAL_TCC_CROSS_WASM32) \
    && !defined(TCC_TARGET_WASM32)
#define PM_TCC_CROSS_INSTANCE_WASM32 1
#endif
#if PM_HAS_TCC && defined(PM_METAL_TCC_CROSS_ARM_EABI) \
    && !defined(TCC_TARGET_ARM)
#define PM_TCC_CROSS_INSTANCE_ARM_EABI 1
#endif
#if PM_HAS_TCC && defined(PM_METAL_TCC_CROSS_X86_64) \
    && !defined(TCC_TARGET_X86_64)
#define PM_TCC_CROSS_INSTANCE_X86_64 1
#endif

#define PM_TCC_CROSS_INSTANCE(id, prefix) \
    extern void *prefix##tcc_new(void); \
    extern void prefix##tcc_delete(void *s); \
    extern void prefix##tcc_set_lib_path(void *s, const char *p); \
    extern int prefix##tcc_add_library_path(void *s, const char *p); \
    extern int prefix##tcc_add_include_path(void *s, const char *p); \
    extern int prefix##tcc_set_output_type(void *s, int t); \
    extern int prefix##tcc_compile_string(void *s, const char *b); \
    extern void prefix##tcc_define_symbol(void *s, const char *sym, const char *val); \
    extern void prefix##tcc_set_realloc(TCCReallocFunc *f); \
    extern void prefix##tcc_free(void *ptr); \
    extern void prefix##tcc_set_error_func(void *s, void *opaque, TCCErrorFunc *cb); \
    extern int prefix##tcc_output_file(void *s, const char *filename); \
    __attribute__((unused)) static void *id##_tcc_new(void) { return prefix##tcc_new(); } \
    __attribute__((unused)) static void id##_tcc_delete(void *s) { prefix##tcc_delete(s); } \
    __attribute__((unused)) static void id##_tcc_set_lib_path(void *s, const char *p) { prefix##tcc_set_lib_path(s, p); } \
    __attribute__((unused)) static int id##_tcc_add_library_path(void *s, const char *p) { return prefix##tcc_add_library_path(s, p); } \
    __attribute__((unused)) static int id##_tcc_add_include_path(void *s, const char *p) { return prefix##tcc_add_include_path(s, p); } \
    __attribute__((unused)) static int id##_tcc_set_output_type(void *s, int t) { return prefix##tcc_set_output_type(s, t); } \
    __attribute__((unused)) static int id##_tcc_compile_string(void *s, const char *b) { return prefix##tcc_compile_string(s, b); } \
    __attribute__((unused)) static void id##_tcc_define_symbol(void *s, const char *sym, const char *val) { prefix##tcc_define_symbol(s, sym, val); } \
    __attribute__((unused)) static void id##_tcc_set_realloc(void *f) { prefix##tcc_set_realloc(f); } \
    __attribute__((unused)) static void id##_tcc_free(void *p) { prefix##tcc_free(p); } \
    __attribute__((unused)) static void id##_tcc_set_error_func(void *s, void *opaque, TCCErrorFunc *cb) { prefix##tcc_set_error_func(s, opaque, cb); } \
    __attribute__((unused)) static int id##_tcc_output_file(void *s, const char *filename) { return prefix##tcc_output_file(s, filename); }

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
/* the allocator window routes tcc_set_realloc directly (the wasm-native
 * seat's reallocator IS the unprefixed one) — the shim stays for the
 * object paths' uniform call shape */
__attribute__((unused))
static void wasm_tcc_set_realloc(void *f) { tcc_set_realloc((TCCReallocFunc *)f); }
static void wasm_tcc_free(void *p) { tcc_free(p); }
static void wasm_tcc_set_error_func(void *s, void *opaque, TCCErrorFunc *cb) { tcc_set_error_func((TCCState *)s, opaque, cb); }
static int wasm_build_mod(uint8_t **out_buf, int *out_len) { return wasm_build_module(out_buf, out_len); }
static void wasm_release_bufs(void) { wasm_release_buffers(); }
#else
/* Cross-compiled wasm32 instance (ELF seat): declared through the
 * per-instance prefix macro above. */
extern int pm_tccw_wasm_build_module(uint8_t **out_buf, int *out_len);
extern void pm_tccw_wasm_release_buffers(void);
PM_TCC_CROSS_INSTANCE(wasm, pm_tccw_)
static int wasm_build_mod(uint8_t **out_buf, int *out_len) { return pm_tccw_wasm_build_module(out_buf, out_len); }
static void wasm_release_bufs(void) { return pm_tccw_wasm_release_buffers(); }
#endif
#endif /* PM_METAL_JIT_C_WASM_PATH */

#ifdef PM_TCC_CROSS_INSTANCE_ARM_EABI
/* Cross-compiled arm-eabi instance (ELF seat): same prefix macro, arm
 * backend. An ELF backend — its object path is the tcc_output_file shape
 * (ET_REL), not wasm_build_module. */
PM_TCC_CROSS_INSTANCE(arm, pm_tcca_)
#endif

#ifdef PM_TCC_CROSS_INSTANCE_X86_64
/* Cross-compiled x86_64 instance (wasm32-native seat — the browser): same
 * prefix macro, x86_64 backend. An ELF backend like arm — the object path
 * is tcc_output_file (ET_REL ELF64). The emitted x86_64 object is a
 * distribution artifact; linking it is a property of an x86_64 target
 * seat, so this path proves the emitted object's shape only. */
PM_TCC_CROSS_INSTANCE(x64, pm_tccx_)
#endif

/*------------------ allocator window (Phase 5) ------------------
 * Implemented after the shim blocks: on a cross seat every cross instance
 * is a second, symbol-prefixed libtcc with its own reallocator global, so
 * the window must route ALL instances under one lock. */
int32_t pm_metal_jit_c_arena_acquire(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    /* try-acquire, never spin: a compile holds this window for seconds, and
     * the async seat's runner threads must not burn their cores spinning
     * on it. A contended window refuses; the actor's serial section is the
     * higher-level serializer that makes every TCC caller queue instead. */
    if (pm_util_lock_try_acquire(&s_tcc_arena_lock) == 0) {
        return -1;
    }
    if (s_tcc_arena_holder != NULL) {
        pm_util_lock_release(&s_tcc_arena_lock);
        return -1;
    }
    s_tcc_arena = arena;
    s_tcc_arena_holder = arena;
#if PM_HAS_TCC
    tcc_set_realloc(pm_metal_jit_c_tcc_arena_realloc);
#endif
#if defined(PM_TCC_CROSS_INSTANCE_WASM32)
    /* cross seat: the prefixed wasm32 instance has its own reallocator
     * global — route it through the same window */
    wasm_tcc_set_realloc(pm_metal_jit_c_tcc_arena_realloc);
#endif
#if defined(PM_TCC_CROSS_INSTANCE_ARM_EABI)
    /* same for the prefixed arm-eabi instance */
    arm_tcc_set_realloc(pm_metal_jit_c_tcc_arena_realloc);
#endif
#if defined(PM_TCC_CROSS_INSTANCE_X86_64)
    /* same for the prefixed x86_64 instance (wasm32-native seat) */
    x64_tcc_set_realloc(pm_metal_jit_c_tcc_arena_realloc);
#endif
    return 0;
}

int32_t pm_metal_jit_c_arena_release(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    if (s_tcc_arena_holder != arena) {
        return -1;
    }
#if PM_HAS_TCC
    tcc_set_realloc(NULL);
#endif
#if defined(PM_TCC_CROSS_INSTANCE_WASM32)
    wasm_tcc_set_realloc(NULL);
#endif
#if defined(PM_TCC_CROSS_INSTANCE_ARM_EABI)
    arm_tcc_set_realloc(NULL);
#endif
#if defined(PM_TCC_CROSS_INSTANCE_X86_64)
    x64_tcc_set_realloc(NULL);
#endif
    s_tcc_arena = NULL;
    s_tcc_arena_holder = NULL;
    pm_util_lock_release(&s_tcc_arena_lock);
    return 0;
}

/* unused on cross seats' coro face (native_entry is the seat's own backend)
 * — object_compile_target is the only caller there */
#if PM_METAL_JIT_C_WASM_PATH
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
    pm_util_mem_arena_t *a = pm_metal_coop_arena();
    if (!a) return -1;
    if (pm_metal_jit_c_arena_acquire(a) != 0) return -1;
    s = wasm_tcc_new();
    if (!s) {
        pm_metal_jit_c_arena_release(a);
        return -1;
    }
    wasm_tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    wasm_tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    wasm_tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (wasm_tcc_compile_string(s, source) != 0) {
        wasm_tcc_delete(s);
        wasm_release_bufs();
        pm_metal_jit_c_arena_release(a);
        return -1;
    }
    if (wasm_build_mod(&buf, &len) != 0 || !buf) {
        wasm_tcc_delete(s);
        wasm_release_bufs();
        pm_metal_jit_c_arena_release(a);
        return -1;
    }
    wasm_tcc_delete(s);
    /* copy out while the arena reallocator still owns buf's allocator,
     * then free it under the same window (tcc_free after the restore would
     * be libc free on an arena block) */
    if (len > (int)wasm_cap) {
        wasm_tcc_free(buf);
        wasm_release_bufs();
        pm_metal_jit_c_arena_release(a);
        return -1;
    }
    memcpy(wasm_out, buf, (size_t)len);
    *wasm_len = (size_t)len;
    wasm_tcc_free(buf);
    wasm_release_bufs();
    pm_metal_jit_c_arena_release(a);
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
int32_t pm_metal_jit_c_arena_acquire(pm_util_mem_arena_t *arena) {
    (void)arena;
    return -1;  /* no TCC on this seat: nothing to install */
}
int32_t pm_metal_jit_c_arena_release(pm_util_mem_arena_t *arena) {
    (void)arena;
    return -1;
}
#endif /* PM_HAS_TCC */

pm_metal_coop_coro_t *pm_metal_jit_c_compile_alloc(
    pm_util_mem_arena_t *arena, const char *source, size_t source_len, const char *module_name) {
    size_t name_len; pm_metal_jit_c_frame_t *f; char *src_copy;
    if (arena == NULL || source == NULL || module_name == NULL || source_len == 0) return NULL;
    name_len = strlen(module_name);
    if (!name_len) return NULL;
    f = (pm_metal_jit_c_frame_t *)pm_metal_coop_coro_create(
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

#if PM_HAS_TCC
/* ---- diagnostic capture (shared by both object paths) ---------------------
 * TCC's error callback writes file:line + message lines. The capture is a
 * caller-owned struct passed as the callback's opaque pointer: no module
 * globals, so two compiles in flight never corrupt each other's capture and
 * the buffer's lifetime is exactly the TCC invocation's. */

typedef struct {
    char *buf;      /* capture buffer (caller frame, whole invocation) */
    size_t len;     /* retained bytes (always NUL-terminated) */
    size_t max;     /* capacity of buf */
} jit_c_diag_t;

static void jit_c_diag_cb(void *opaque, const char *msg) {
    jit_c_diag_t *d = (jit_c_diag_t *)opaque;
    size_t n;
    if (d == NULL || msg == NULL || d->buf == NULL || d->max == 0) return;
    /* Keep only the most recent lines: TCC emits include-stack prefixes
     * first and the error line last, so a full buffer would evict the
     * cause. Roll the buffer when this line does not fit. */
    n = strlen(msg);
    if (n >= d->max - 1) {
        /* a single line longer than the buffer: keep its tail */
        d->len = 0;
        memcpy(d->buf, msg + (n - (d->max - 2)), d->max - 2);
        d->len = d->max - 2;
        d->buf[d->len++] = '\n';
        d->buf[d->len] = '\0';
        return;
    }
    if (d->len + n + 1 >= d->max) {
        /* drop the oldest lines until the new one fits */
        size_t need = n + 2;
        size_t drop = 0;
        while (d->len - drop >= need && drop < d->len) {
            /* advance one line */
            size_t adv = drop;
            while (adv < d->len && d->buf[adv] != '\n') {
                adv++;
            }
            if (adv < d->len) adv++;
            drop = adv;
        }
        if (drop > 0 && drop < d->len) {
            memmove(d->buf, d->buf + drop, d->len - drop);
            d->len -= drop;
        } else if (drop >= d->len) {
            d->len = 0;
        }
    }
    if (d->len + n + 1 < d->max) {
        memcpy(d->buf + d->len, msg, n);
        d->len += n;
        d->buf[d->len++] = '\n';
        d->buf[d->len] = '\0';
    }
}

/* Fold the captured diagnostics into errbuf (kept when non-empty; the
 * "compile failed" prefix stays so callers still see the stage). TCC
 * diagnostics end with the error line, so when the whole capture does
 * not fit, keep the tail — the last lines carry the cause. */
static void jit_c_obj_err_diag(char *errbuf, size_t errbuf_len,
    const char *msg, const jit_c_diag_t *d) {
    if (errbuf == NULL || errbuf_len == 0) return;
    if (d != NULL && d->buf != NULL && d->buf[0] != '\0') {
        size_t msg_len = strlen(msg);
        size_t room = errbuf_len > msg_len + 2
            ? errbuf_len - msg_len - 2 : 0;
        const char *diag = d->buf;
        size_t skip = 0;
        if (d->len + msg_len + 2 > errbuf_len) {
            if (d->len > room) {
                skip = d->len - room;
                /* advance to the next line so the tail starts clean */
                while (diag[skip] != '\0' && diag[skip] != '\n'
                    && skip < d->len) {
                    skip++;
                }
                if (skip < d->len) skip++;
            }
            diag = d->buf + skip;
        }
        snprintf(errbuf, errbuf_len, "%s: %s", msg, diag);
    } else {
        snprintf(errbuf, errbuf_len, "%s", msg);
    }
}
#endif /* PM_HAS_TCC */

#if (PM_HAS_TCC && !defined(TCC_TARGET_WASM32)) \
    || defined(PM_TCC_CROSS_INSTANCE_ARM_EABI) \
    || defined(PM_TCC_CROSS_INSTANCE_X86_64)
#define PM_METAL_JIT_C_OBJECT_PATH 1
/* Object path (multi-object build): compile to ET_REL .o via tcc_output_file.
 * Same temp-file convention as the jit.rs mrustc embed (/tmp/.jit_*).
 * Gate: any FILE-BACKED TCC backend on this seat — the native instance
 * when it is not wasm32, or a cross instance (arm-eabi/x86_64 are ELF
 * backends wherever they link, wasm32 seat included: emcc's libc has the
 * POSIX calls, the object is an in-memory distribution artifact there). */
#include <unistd.h>
#include <fcntl.h>

static void jit_c_obj_err(char *errbuf, size_t errbuf_len, const char *msg) {
    if (errbuf == NULL || errbuf_len == 0) return;
    snprintf(errbuf, errbuf_len, "%s", msg);
}

/* On the wasm32-native seat the router always picks the wasm path for
 * TARGET_SEAT, so this body links but is never called there — the
 * unused attribute matches the stub below it and silences -Wunused. */
__attribute__((unused))
static int32_t jit_c_object_compile_native(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    char tmpl[] = "/tmp/.jit_c_obj_XXXXXX";
    char diag_buf[1024]; /* TCC diagnostic capture — whole-invocation lifetime */
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

    /* route the whole compile's allocations through the arena via the
     * lock-guarded allocator window (Phase 5) */
    if (pm_metal_jit_c_arena_acquire(arena) != 0) {
        unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len,
            "object_compile: allocator window busy");
        return -1;
    }
    /* the arena's own reallocation can move a block tcc still holds, but
     * tlsf_realloc copies contents — same contract as libc realloc */

    s = tcc_new();
    if (s == NULL) {
        pm_metal_jit_c_arena_release(arena);
        unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: tcc_new failed");
        return -1;
    }
    tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_set_output_type(s, TCC_OUTPUT_OBJ);
    /* route diagnostics into a scratch buffer folded into errbuf on
     * refusal — the capture rides this function's frame for the WHOLE
     * invocation (through tcc_delete), passed to the callback as its
     * opaque pointer: no globals, safe under reentrancy */
    {
        jit_c_diag_t diag;
        diag.buf = diag_buf;
        diag.len = 0;
        diag.max = sizeof(diag_buf);
        diag_buf[0] = '\0';
        tcc_set_error_func(s, &diag, jit_c_diag_cb);
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
            pm_metal_jit_c_arena_release(arena);
            unlink(tmpl);
            jit_c_obj_err_diag(errbuf, errbuf_len,
                "object_compile: tcc compile failed", &diag);
            return -1;
        }
        if (tcc_output_file(s, tmpl) != 0) {
            tcc_delete(s);
            pm_metal_jit_c_arena_release(arena);
            unlink(tmpl);
            jit_c_obj_err_diag(errbuf, errbuf_len,
                "object_compile: tcc_output_file failed", &diag);
            return -1;
        }
        tcc_delete(s);
    }
    /* release the window before the read-back — no tcc allocation
     * happens below this point */
    pm_metal_jit_c_arena_release(arena);

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

#if defined(PM_TCC_CROSS_INSTANCE_ARM_EABI)
/* ARM object path (cross arm-eabi instance on ELF seats): the arm backend
 * is an ELF backend — the object path is the tcc_output_file shape
 * (ET_REL ELF32), same temp-file convention as the native path. The
 * armv7 ELF is a *distribution* artifact: linking stays a property of the
 * target seat (load.c gains EM_ARM support separately), so this path only
 * proves the emitted object's magic (\\x7fELF) and e_machine (EM_ARM). */
static int32_t jit_c_object_compile_arm(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    char tmpl[] = "/tmp/.jit_c_arm_XXXXXX";
    char diag_buf[1024]; /* TCC diagnostic capture — whole-invocation lifetime */
    jit_c_diag_t diag;
    int fd;
    FILE *f;
    long n;
    uint8_t *buf;
    void *s;
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

    /* route the whole compile's allocations through the arena via the
     * lock-guarded allocator window (N-instance generalization) */
    if (pm_metal_jit_c_arena_acquire(arena) != 0) {
        unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len,
            "object_compile: allocator window busy");
        return -1;
    }

    s = arm_tcc_new();
    if (s == NULL) {
        pm_metal_jit_c_arena_release(arena);
        unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: tcc_new failed");
        return -1;
    }
    arm_tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    arm_tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    arm_tcc_set_output_type(s, TCC_OUTPUT_OBJ);
    {
        diag.buf = diag_buf;
        diag.len = 0;
        diag.max = sizeof(diag_buf);
        diag_buf[0] = '\0';
        arm_tcc_set_error_func(s, &diag, jit_c_diag_cb);
        for (i = 0; i < n_include_dirs; i++) {
            if (include_dirs[i] != NULL && include_dirs[i][0] != '\0') {
                arm_tcc_add_include_path(s, include_dirs[i]);
            }
        }
        for (i = 0; i < n_defines; i++) {
            if (defines[i] != NULL && defines[i][0] != '\0') {
                arm_tcc_define_symbol(s, defines[i], NULL);
            }
        }
        if (arm_tcc_compile_string(s, source) != 0) {
            arm_tcc_delete(s);
            pm_metal_jit_c_arena_release(arena);
            unlink(tmpl);
            jit_c_obj_err_diag(errbuf, errbuf_len,
                "object_compile: tcc compile failed", &diag);
            return -1;
        }
        if (arm_tcc_output_file(s, tmpl) != 0) {
            arm_tcc_delete(s);
            pm_metal_jit_c_arena_release(arena);
            unlink(tmpl);
            jit_c_obj_err_diag(errbuf, errbuf_len,
                "object_compile: tcc_output_file failed", &diag);
            return -1;
        }
        arm_tcc_delete(s);
    }
    pm_metal_jit_c_arena_release(arena);

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
/* seat without the arm-eabi cross instance — the router refuses */
static int32_t jit_c_object_compile_arm(pm_util_mem_arena_t *arena,
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
            "object_compile: arm-eabi target not available on this seat");
    }
    return -1;
}
#endif /* PM_TCC_CROSS_INSTANCE_ARM_EABI */

#if defined(PM_TCC_CROSS_INSTANCE_X86_64)
/* x86_64 object path (cross instance on the wasm32-native seat): same
 * tcc_output_file shape as the native and arm paths — the x86_64 backend
 * is an ELF backend, the emitted ET_REL ELF64 is a distribution artifact
 * for an x86_64 target seat. */
static int32_t jit_c_object_compile_x64(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    const char **include_dirs, uint32_t n_include_dirs,
    const char **defines, uint32_t n_defines,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    char tmpl[] = "/tmp/.jit_c_x64_XXXXXX";
    char diag_buf[1024]; /* TCC diagnostic capture — whole-invocation lifetime */
    jit_c_diag_t diag;
    int fd;
    FILE *f;
    long n;
    uint8_t *buf;
    void *s;
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

    if (pm_metal_jit_c_arena_acquire(arena) != 0) {
        unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len,
            "object_compile: allocator window busy");
        return -1;
    }

    s = x64_tcc_new();
    if (s == NULL) {
        pm_metal_jit_c_arena_release(arena);
        unlink(tmpl);
        jit_c_obj_err(errbuf, errbuf_len, "object_compile: tcc_new failed");
        return -1;
    }
    x64_tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    x64_tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    x64_tcc_set_output_type(s, TCC_OUTPUT_OBJ);
    {
        diag.buf = diag_buf;
        diag.len = 0;
        diag.max = sizeof(diag_buf);
        diag_buf[0] = '\0';
        x64_tcc_set_error_func(s, &diag, jit_c_diag_cb);
        for (i = 0; i < n_include_dirs; i++) {
            if (include_dirs[i] != NULL && include_dirs[i][0] != '\0') {
                x64_tcc_add_include_path(s, include_dirs[i]);
            }
        }
        for (i = 0; i < n_defines; i++) {
            if (defines[i] != NULL && defines[i][0] != '\0') {
                x64_tcc_define_symbol(s, defines[i], NULL);
            }
        }
        if (x64_tcc_compile_string(s, source) != 0) {
            x64_tcc_delete(s);
            pm_metal_jit_c_arena_release(arena);
            unlink(tmpl);
            jit_c_obj_err_diag(errbuf, errbuf_len,
                "object_compile: tcc compile failed", &diag);
            return -1;
        }
        if (x64_tcc_output_file(s, tmpl) != 0) {
            x64_tcc_delete(s);
            pm_metal_jit_c_arena_release(arena);
            unlink(tmpl);
            jit_c_obj_err_diag(errbuf, errbuf_len,
                "object_compile: tcc_output_file failed", &diag);
            return -1;
        }
        x64_tcc_delete(s);
    }
    pm_metal_jit_c_arena_release(arena);

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
/* seat without the x86_64 cross instance — the router refuses */
static int32_t jit_c_object_compile_x64(pm_util_mem_arena_t *arena,
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
            "object_compile: x86_64 target not available on this seat");
    }
    return -1;
}
#endif /* PM_TCC_CROSS_INSTANCE_X86_64 */

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
    char diag_buf[1024]; /* TCC diagnostic capture — whole-invocation lifetime */
    jit_c_diag_t diag;
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
     * caller's arena through the lock-guarded allocator window */
    if (pm_metal_jit_c_arena_acquire(arena) != 0) {
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: allocator window busy");
        }
        return -1;
    }
    s = wasm_tcc_new();
    if (s == NULL) {
        pm_metal_jit_c_arena_release(arena);
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: tcc_new failed");
        }
        return -1;
    }
    wasm_tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    wasm_tcc_add_library_path(s, PM_METAL_TCC_LIB_DIR);
    wasm_tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    /* same per-call diagnostic capture as the native path: opaque pointer,
     * whole-invocation lifetime, no globals */
    diag.buf = diag_buf;
    diag.len = 0;
    diag.max = sizeof(diag_buf);
    diag_buf[0] = '\0';
    wasm_tcc_set_error_func(s, &diag, jit_c_diag_cb);
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
        pm_metal_jit_c_arena_release(arena);
        jit_c_obj_err_diag(errbuf, errbuf_len,
            "object_compile: tcc compile failed", &diag);
        return -1;
    }
    if (wasm_build_mod(&mod, &mod_len) != 0 || mod == NULL || mod_len <= 0) {
        if (mod != NULL) {
            wasm_tcc_free(mod); /* free under the arena window that allocated it */
        }
        wasm_tcc_delete(s);
        wasm_release_bufs(); /* same window: their backing is this arena */
        pm_metal_jit_c_arena_release(arena);
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
        pm_metal_jit_c_arena_release(arena);
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "object_compile: arena alloc failed");
        }
        return -1;
    }
    memcpy(buf, mod, (size_t)mod_len);
    wasm_tcc_free(mod); /* must free before the release — tcc_free after would be
     * libc free on an arena block */
    wasm_release_bufs();
    pm_metal_jit_c_arena_release(arena);
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
    if (target == (int32_t)PM_METAL_JIT_C_TARGET_ARM_EABI) {
        return jit_c_object_compile_arm(arena, source, source_len,
            include_dirs, n_include_dirs, defines, n_defines,
            obj_out, obj_len, errbuf, errbuf_len);
    }
    if (target == (int32_t)PM_METAL_JIT_C_TARGET_X86_64) {
        return jit_c_object_compile_x64(arena, source, source_len,
            include_dirs, n_include_dirs, defines, n_defines,
            obj_out, obj_len, errbuf, errbuf_len);
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

pm_metal_coop_status_t pm_metal_jit_c_compile_step(pm_metal_coop_coro_t *self) {
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
    const pm_metal_coop_coro_t *self) {
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
    pm_metal_coop_coro_t *(pm_util_mem_arena_t *, const char *, size_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_compile_step, pm_metal_jit_c_compile_step,
    pm_metal_coop_status_t(pm_metal_coop_coro_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_result_free, pm_metal_jit_c_result_free,
    void(pm_util_mem_arena_t *, pm_metal_jit_c_result_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_result_of, pm_metal_jit_c_result_of,
    const pm_metal_jit_c_result_t *(const pm_metal_coop_coro_t *));
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
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_arena_acquire, pm_metal_jit_c_arena_acquire,
    int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.jit.c, pm_metal_jit_c_arena_release, pm_metal_jit_c_arena_release,
    int32_t(pm_util_mem_arena_t *));

PM_METAL_EXTERNAL_C(tcc, "0.9.28rc");

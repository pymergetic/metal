#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* Hosted fill for pymergetic.metal.boot: mmap/malloc span + emcc js.fetch. */
#include "extmod/metal/boot.h"

#if defined(__EMSCRIPTEN__)
#include "ports/webassembly/io_browser.h"
#endif
#include "ports/common/boot.h"
#include "ports/micropython/importhook.h"
#include "pymergetic/metal/boot.h"
#include "pymergetic/metal/boot/__types__.h"
#include "pymergetic/metal/async/__exports__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/io.h"
#include "pymergetic/wasmmod/net/cdn.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "py/mpstate.h"
#include "py/obj.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#if !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

#ifndef PM_METAL_UNIX_ARENA_SPAN
#define PM_METAL_UNIX_ARENA_SPAN (4u * 1024u * 1024u)
#endif
#ifndef PM_METAL_CDN_DEFAULT
#define PM_METAL_CDN_DEFAULT "https://cdn.pymergetic.com/cdn"
#endif

static void *s_backing;
static size_t s_backing_len;

int pm_metal_boot_fill_hosted_span(void **base, size_t *len) {
    if (base == NULL || len == NULL) {
        return -1;
    }
    s_backing_len = PM_METAL_UNIX_ARENA_SPAN;
#if defined(__EMSCRIPTEN__)
    s_backing = malloc(s_backing_len);
    if (s_backing == NULL) {
        s_backing_len = 0;
        return -1;
    }
#else
    s_backing = mmap(NULL, s_backing_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (s_backing == MAP_FAILED) {
        s_backing = NULL;
        s_backing_len = 0;
        return -1;
    }
#endif
    *base = s_backing;
    *len = s_backing_len;
    return 0;
}

void pm_metal_boot_fill_release(void *base, size_t len) {
    if (base == NULL || len == 0) {
        return;
    }
#if defined(__EMSCRIPTEN__)
    free(base);
#else
    munmap(base, len);
#endif
    if (base == s_backing) {
        s_backing = NULL;
        s_backing_len = 0;
    }
}

const char *pm_metal_boot_fill_seat(void) {
#if defined(__EMSCRIPTEN__)
    return "emcc";
#else
    return "unix";
#endif
}

void pm_metal_boot_fill_io(void) {
#if defined(__EMSCRIPTEN__)
    /* WAN HTTP is js.fetch. sim L2 stays packets. Fetch itself is Asyncify —
     * do not call it from sync import (vanilla CLI uses mp_js_do_exec). */
    pm_wasmmod_io_set(&pm_wasmmod_io_browser);
#endif
}

void *pm_metal_wasm_malloc(size_t n) {
    pm_util_mem_arena_t *arena = pm_metal_boot_arena();
    if (arena == NULL) {
        return NULL;
    }
    if (n == 0) {
        n = 1;
    }
    return pm_util_mem_alloc(arena, n);
}

void pm_metal_wasm_free(void *p) {
    pm_util_mem_arena_t *arena = pm_metal_boot_arena();
    if (arena == NULL) {
        return;
    }
    pm_util_mem_free(arena, p);
}

void *pm_metal_wasm_realloc(void *p, size_t n) {
    pm_util_mem_arena_t *arena = pm_metal_boot_arena();
    if (arena == NULL) {
        return NULL;
    }
    return pm_util_mem_realloc(arena, p, n);
}

/* Strong fill for wasmmod's weak host-kernel hook (ports/common/boot.h): a CDN
 * fetch needs Metal's net up, and wasmmod must not name pm_metal_* to say so. */
int pm_wasmmod_host_kernel_ready(void) {
    if (pm_metal_ready()) {
        return 0;
    }
    return pm_metal_boot() == 0 ? 0 : -1;
}

void pm_metal_upy_port_init(void) {
    if (pm_wasmmod_net_cdn_base_count() == 0u) {
        const char *url = getenv("WASMMOD_CDN_URL");
        const char *tok = getenv("WASMMOD_CDN_TOKEN");
        if (url == NULL || url[0] == 0) {
            url = PM_METAL_CDN_DEFAULT;
        }
        pm_wasmmod_net_cdn_configure(url, tok);
    }
    if (pm_metal_boot() != 0) {
        fputs("metal boot failed\n", stderr);
    }
    /* Give the seat a bench clock so wm.bench_all()/wm.bench() report ns/op
     * instead of "no clock". Benches are informational and never gate; without
     * this a bench just stays honest. The clock is the async mono_us the
     * host bench binary already installs by hand. */
    pm_wasmmod_registry_set_bench_clock(pm_metal_async_mono_us);
    mp_wasm_port_init();
#if MICROPY_PY_SYS_PS1_PS2
    {
        static const char ps1[] = MICROPY_REPL_PS1;
        static const char ps2[] = MICROPY_REPL_PS2;
        MP_STATE_VM(sys_mutable[MP_SYS_MUTABLE_PS1]) = mp_obj_new_str(ps1, sizeof(ps1) - 1u);
        MP_STATE_VM(sys_mutable[MP_SYS_MUTABLE_PS2]) = mp_obj_new_str(ps2, sizeof(ps2) - 1u);
    }
#endif
}

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* Metal hosted boot: feed a CLASS_MEM span → pick arena → boot_run → probe → L3 sim.
 * Unix: anonymous mmap. Browser: malloc (same feed_mmap / pick as firmware). */
#include "extmod/metal/boot.h"

#if defined(__EMSCRIPTEN__)
#include "ports/webassembly/io_browser.h"
#endif
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/fw/memmap.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/io.h"

#include <stdint.h>
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <stdlib.h>
#else
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

#ifndef PM_METAL_UNIX_ARENA_SPAN
#define PM_METAL_UNIX_ARENA_SPAN (4u * 1024u * 1024u)
#endif

static pm_util_mem_arena_t *s_arena;
static void *s_backing;
static size_t s_backing_len;
static int s_ready;

void *pm_metal_wasm_malloc(size_t n) {
    if (s_arena == NULL) {
        return NULL;
    }
    if (n == 0) {
        n = 1;
    }
    return pm_util_mem_alloc(s_arena, n);
}

void pm_metal_wasm_free(void *p) {
    pm_util_mem_free(s_arena, p);
}

void *pm_metal_wasm_realloc(void *p, size_t n) {
    if (s_arena == NULL) {
        return NULL;
    }
    return pm_util_mem_realloc(s_arena, p, n);
}

int pm_metal_ready(void) {
    return s_ready;
}

static void boot_unwind(void) {
    pm_mod_boot_unwind();
    if (s_arena != NULL) {
        pm_util_mem_arena_destroy(s_arena);
        s_arena = NULL;
    }
    if (s_backing != NULL) {
#if defined(__EMSCRIPTEN__)
        free(s_backing);
#else
        if (s_backing != MAP_FAILED) {
            munmap(s_backing, s_backing_len);
        }
#endif
        s_backing = NULL;
        s_backing_len = 0;
    }
    s_ready = 0;
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int pm_metal_boot(void) {
    int32_t h;
    uint8_t rec[24];
    uint64_t base = 0;
    uint64_t len = 0;
    uintptr_t span;
    if (s_ready) {
        return 0;
    }
    s_backing_len = PM_METAL_UNIX_ARENA_SPAN;
#if defined(__EMSCRIPTEN__)
    s_backing = malloc(s_backing_len);
    if (s_backing == NULL) {
        return -1;
    }
#else
    s_backing = mmap(NULL, s_backing_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (s_backing == MAP_FAILED) {
        s_backing = NULL;
        return -1;
    }
#endif
    span = (uintptr_t)s_backing;
    memset(rec, 0, sizeof(rec));
    rec[0] = 20;
    put_u32(rec + 4, (uint32_t)span);
    put_u32(rec + 8, (uint32_t)((uint64_t)span >> 32));
    put_u32(rec + 12, (uint32_t)s_backing_len);
    rec[20] = 1;
    if (pm_metal_fw_memmap_feed_mmap(rec, sizeof(rec)) != 0
        || pm_metal_fw_memmap_pick(0, 0, s_backing_len, &base, &len) != 0) {
        boot_unwind();
        return -1;
    }
    s_arena = pm_util_mem_arena_create((void *)(uintptr_t)base, (size_t)len);
    if (s_arena == NULL) {
        boot_unwind();
        return -1;
    }
    if (pm_mod_boot_run(s_arena) != 0 || pm_metal_drivers_probe() != 0) {
        boot_unwind();
        return -1;
    }
    if (pm_metal_fw_memmap_count() <= 0) {
        boot_unwind();
        return -1;
    }
    h = pm_metal_drivers_net_by_compat("sim", 0);
    if (h < 0 || pm_metal_net_ip_if_up_h(h, 0x0a000001u) != 0) {
        boot_unwind();
        return -1;
    }
#if defined(__EMSCRIPTEN__)
    /* WAN HTTP is js.fetch. sim L2 stays packets. Fetch itself is Asyncify —
     * do not call it from sync import (vanilla CLI uses mp_js_do_exec). */
    pm_wasmmod_io_set(&pm_wasmmod_io_browser);
#endif
    /* unix: POSIX native_http in wasmmod.io (host loopback). Firmware cake
     * fills io_ops with metal.net.http in board main.c after boot_run. */
    s_ready = 1;
    return 0;
}

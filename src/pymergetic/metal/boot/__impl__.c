/* pymergetic.metal.boot — arena → cards → probe → NICs → one tree.
 * Platforms feed memmap / hosted span / io / seat name. Not a second bring-up. */
#include "pymergetic/metal/boot/__exports__.h"

#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/fw/memmap.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/__version__.h"
#include "pymergetic/wasmmod/boot.h"
#include "third_party/wamr/core/version.h"

#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_DT_WALK
#define PM_METAL_DT_WALK 128
#endif

#ifndef PM_METAL_BOOT_ARENA_SPAN
#if defined(PM_METAL_FIRMWARE)
#define PM_METAL_BOOT_ARENA_SPAN 0u
#else
#define PM_METAL_BOOT_ARENA_SPAN (4u * 1024u * 1024u)
#endif
#endif

static pm_util_mem_arena_t *s_arena;
static void *s_hosted;
static size_t s_hosted_len;
static int s_ready;

__attribute__((weak)) int pm_metal_boot_fill_hosted_span(void **base, size_t *len) {
    (void)base;
    (void)len;
    return -1;
}

__attribute__((weak)) void pm_metal_boot_fill_release(void *base, size_t len) {
    (void)base;
    (void)len;
}

__attribute__((weak)) void pm_metal_boot_fill_avoid(uint64_t *lo, uint64_t *hi) {
    if (lo != NULL) {
        *lo = 0;
    }
    if (hi != NULL) {
        *hi = 0;
    }
}

__attribute__((weak)) size_t pm_metal_boot_fill_arena_need(void) {
    return PM_METAL_BOOT_ARENA_SPAN;
}

__attribute__((weak)) void pm_metal_boot_fill_io(void) {}

__attribute__((weak)) void pm_metal_boot_fill_bind_arena(pm_util_mem_arena_t *arena) {
    (void)arena;
}

__attribute__((weak)) const char *pm_metal_boot_fill_seat(void) {
    return "host";
}

extern char __pm_metal_image_base[] __attribute__((weak));
extern char __pm_metal_image_end[] __attribute__((weak));

__attribute__((weak)) const char *pm_metal_boot_fill_map_label(void) {
    return NULL;
}

__attribute__((weak)) int pm_metal_boot_fill_kernel(uint64_t *base, uint64_t *len) {
    uintptr_t lo;
    uintptr_t hi;
    if (base == NULL || len == NULL) {
        return -1;
    }
    lo = (uintptr_t)__pm_metal_image_base;
    hi = (uintptr_t)__pm_metal_image_end;
    if (lo == 0 || hi <= lo) {
        return -1;
    }
    *base = (uint64_t)lo;
    *len = (uint64_t)(hi - lo);
    return 0;
}

int pm_metal_ready(void) {
    return s_ready;
}

pm_util_mem_arena_t *pm_metal_boot_arena(void) {
    return s_arena;
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int32_t pm_metal_boot_feed_span(uint64_t base, uint64_t len) {
    uint8_t rec[24];
    if (len == 0) {
        return -1;
    }
    memset(rec, 0, sizeof(rec));
    rec[0] = 20;
    put_u32(rec + 4, (uint32_t)base);
    put_u32(rec + 8, (uint32_t)(base >> 32));
    put_u32(rec + 12, (uint32_t)len);
    put_u32(rec + 16, (uint32_t)(len >> 32));
    rec[20] = 1;
    return pm_metal_fw_memmap_feed_mmap(rec, sizeof(rec));
}

static void boot_unwind(void) {
    pm_mod_boot_unwind();
    if (s_arena != NULL) {
        pm_util_mem_arena_destroy(s_arena);
        s_arena = NULL;
    }
    if (s_hosted != NULL) {
        pm_metal_boot_fill_release(s_hosted, s_hosted_len);
        s_hosted = NULL;
        s_hosted_len = 0;
    }
    s_ready = 0;
}

static int32_t pick_arena(uint64_t *base, uint64_t *len) {
    uint64_t avoid_lo = 0;
    uint64_t avoid_hi = 0;
    size_t want = pm_metal_boot_fill_arena_need();
    pm_metal_boot_fill_avoid(&avoid_lo, &avoid_hi);
    return pm_metal_fw_memmap_pick(avoid_lo, avoid_hi, (uint64_t)want, base, len);
}

static int32_t nics_up(void) {
    int32_t i;
    int32_t up = 0;
    uint32_t addr = 0x0a000001u;
    if (pm_metal_drivers_net_count() <= 0) {
        return -1;
    }
    for (i = 0; i < PM_METAL_DT_WALK; i++) {
        if (pm_metal_drivers_net_dt_id(i) < 0) {
            continue;
        }
        if (pm_metal_net_ip_if_up_h(i, addr) == 0) {
            up++;
            addr++;
        }
    }
    return up > 0 ? 0 : -1;
}

int pm_metal_boot(void) {
    uint64_t base = 0;
    uint64_t len = 0;
    if (s_ready) {
        return 0;
    }
    if (pick_arena(&base, &len) != 0) {
        void *span = NULL;
        size_t span_len = 0;
        if (pm_metal_boot_fill_hosted_span(&span, &span_len) != 0 || span == NULL || span_len == 0) {
            return -1;
        }
        s_hosted = span;
        s_hosted_len = span_len;
        if (pm_metal_boot_feed_span((uint64_t)(uintptr_t)span, (uint64_t)span_len) != 0
            || pick_arena(&base, &len) != 0) {
            boot_unwind();
            return -1;
        }
    }
    s_arena = pm_util_mem_arena_create((void *)(uintptr_t)base, (size_t)len);
    if (s_arena == NULL) {
        boot_unwind();
        return -1;
    }
    pm_metal_boot_fill_bind_arena(s_arena);
    if (pm_mod_boot_run(s_arena) != 0) {
        boot_unwind();
        return -1;
    }
    pm_metal_boot_fill_io();
    if (pm_metal_drivers_probe() != 0) {
        boot_unwind();
        return -1;
    }
    if (pm_metal_fw_memmap_count() <= 0 || nics_up() != 0) {
        boot_unwind();
        return -1;
    }
    {
        uint64_t alo = 0;
        uint64_t ahi = 0;
        pm_metal_boot_fill_avoid(&alo, &ahi);
        (void)pm_metal_fw_memmap_add_spare(s_arena, alo, ahi, base, base + len);
    }
    s_ready = 1;
    (void)pm_metal_boot_tree_print();
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.boot, pm_metal_boot, pm_metal_boot, int(void));
PM_MOD_EXPORT_C(pymergetic.metal.boot, pm_metal_ready, pm_metal_ready, int(void));
PM_MOD_EXPORT_C(pymergetic.metal.boot, pm_metal_boot_feed_span, pm_metal_boot_feed_span, int32_t(uint64_t, uint64_t));

/* tlsf.h: "Two Level Segregated Fit memory allocator, version 3.1." */
PM_METAL_EXTERNAL_C(tlsf, "3.1");
PM_METAL_EXTERNAL_C(wasmmod, PYMERGETIC_WASMMOD_VERSION);
PM_METAL_EXTERNAL_C(wamr, PM_METAL_EXT_STR(WAMR_VERSION_MAJOR) "." PM_METAL_EXT_STR(WAMR_VERSION_MINOR) "."
    PM_METAL_EXT_STR(WAMR_VERSION_PATCH));

/* pymergetic.metal.boot — mem face on the boot.tree surface. */
#include "pymergetic/metal/boot/__types__.h"
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <stdio.h>

static void fmt_size64(char *out, unsigned cap, uint64_t n) {
    if (n >= (1024ull * 1024ull)) {
        snprintf(out, cap, "%u MiB", (unsigned)(n / (1024ull * 1024ull)));
        return;
    }
    if (n >= 1024ull) {
        snprintf(out, cap, "%u KiB", (unsigned)(n / 1024ull));
        return;
    }
    snprintf(out, cap, "%u B", (unsigned)n);
}

static void fmt_base_size(char *out, unsigned cap, uint64_t base, uint64_t len) {
    char human[24];
    fmt_size64(human, sizeof(human), len);
    snprintf(out, cap, "base=0x%08x%08x size=%s", (unsigned)(base >> 32), (unsigned)base, human);
}

static void msg_mem(int last) {
    pm_util_mem_arena_t *arena = pm_metal_boot_arena();
    uint64_t kbase = 0;
    uint64_t klen = 0;
    int have_k = pm_metal_boot_fill_kernel(&kbase, &klen) == 0 && klen != 0;
    int nreg = 0;
    int have_spare = 0;
    char loc[80];
    char detail[sizeof(loc) + 8];
    char human[24];
    size_t bytes;
    size_t mapped;
    size_t hole;
    size_t heap;
    size_t spare = 0;
    const char *tag;
    uint64_t abase;

    (void)last;
    if (arena != NULL) {
        nreg++;
        spare = pm_util_mem_arena_spare(arena);
        if (spare != 0) {
            have_spare = 1;
            nreg++;
        }
    }
    if (have_k) {
        nreg++;
    }
    if (nreg == 0) {
        pm_metal_boot_msg_item(0, 0, 0, "mem", "-");
        return;
    }
    pm_metal_boot_msg_count(detail, sizeof(detail), "ok  ", (unsigned)nreg, "region");
    pm_metal_boot_msg_item(0, 0, 0, "mem", detail);
    if (have_k) {
        fmt_base_size(loc, sizeof(loc), kbase, klen);
        snprintf(detail, sizeof(detail), "ok  %s", loc);
        pm_metal_boot_msg_item(arena == NULL, 1, 1, "kernel", detail);
    }
    if (arena == NULL) {
        return;
    }
    bytes = pm_util_mem_arena_bytes(arena);
    mapped = pm_util_mem_arena_map_used(arena);
    hole = pm_util_mem_arena_hole(arena);
    heap = pm_util_mem_arena_heap_used(arena);
    abase = (uint64_t)(uintptr_t)arena;
    fmt_base_size(loc, sizeof(loc), abase, (uint64_t)bytes);
    pm_metal_boot_msg_item(!have_spare, 1, 1, "area", loc);
    if (mapped != 0) {
        fmt_size64(human, sizeof(human), (uint64_t)mapped);
        tag = pm_metal_boot_fill_map_label();
        if (tag != NULL && tag[0] != 0) {
            snprintf(detail, sizeof(detail), "%s  %s", human, tag);
        } else {
            snprintf(detail, sizeof(detail), "%s", human);
        }
        pm_metal_boot_msg_item(0, 2, have_spare, "map", detail);
    }
    fmt_size64(human, sizeof(human), (uint64_t)hole);
    pm_metal_boot_msg_item(0, 2, have_spare, "hole", human);
    fmt_size64(human, sizeof(human), (uint64_t)heap);
    snprintf(detail, sizeof(detail), "ok  %s", human);
    pm_metal_boot_msg_item(1, 2, have_spare, "tlsf (heap)", detail);
    if (have_spare) {
        fmt_size64(human, sizeof(human), (uint64_t)spare);
        pm_metal_boot_msg_item(1, 1, 1, "spare", human);
    }
}

PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_MEM, msg_mem);

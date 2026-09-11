/* pymergetic.metal.boot — the rows for the two util cards a metal seat runs
 * on: the arena it was handed (mem) and the capacities it grows against
 * (limits). Both live in the wasmmod tree, so their boot-tree rows live here,
 * with the card that boots them, rather than in a card that has no metal. */
#include "pymergetic/metal/boot/__types__.h"
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/util/limits.h"

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

/* What this seat has been told it may grow to.
 *
 * The full listing belongs in `m.limits()`, not in a boot tree that has to fit
 * on a console: the branch says how many knobs there are and then names only
 * the ones this seat has moved off the number it shipped with. On a stock boot
 * that is no rows at all, which is the honest answer. */
static uint32_t moved_count(void) {
    uint32_t n = pm_util_limits_count();
    uint32_t moved = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (pm_util_limits_soft((int32_t)i) != pm_util_limits_default((int32_t)i)) {
            moved++;
        }
    }
    return moved;
}

static void msg_limits_tree(int last) {
    char head[48];
    char knobs[24];
    uint32_t n = pm_util_limits_count();
    uint32_t moved = moved_count();
    uint32_t shown = 0;
    uint32_t i;

    pm_metal_boot_msg_count(knobs, sizeof(knobs), "", n, "knob");
    snprintf(head, sizeof(head), "%s  %s", pm_util_limits_ready() ? "ok" : "-", knobs);
    if (!pm_util_limits_ready()) {
        pm_metal_boot_msg_fail();
    }
    pm_metal_boot_msg_item(last, 0, 1, "limits", head);

    for (i = 0; i < n && moved != 0; i++) {
        char row[64];
        uint32_t soft = pm_util_limits_soft((int32_t)i);
        uint32_t hard = pm_util_limits_hard((int32_t)i);
        const char *name = pm_util_limits_name((int32_t)i);
        if (soft == pm_util_limits_default((int32_t)i)) {
            continue;
        }
        if (hard == 0u) {
            snprintf(row, sizeof(row), "%u  of any  (was %u)", (unsigned)soft,
                (unsigned)pm_util_limits_default((int32_t)i));
        } else {
            snprintf(row, sizeof(row), "%u  of %u  (was %u)", (unsigned)soft, (unsigned)hard,
                (unsigned)pm_util_limits_default((int32_t)i));
        }
        shown++;
        pm_metal_boot_msg_item(shown == moved, 1, !last, name != NULL ? name : "?", row);
    }
}

PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_LIMITS, msg_limits_tree);

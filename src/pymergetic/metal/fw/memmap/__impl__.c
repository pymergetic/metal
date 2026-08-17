/* pymergetic.metal.fw.memmap — e820/UEFI-style ranges as dt CLASS_MEM nodes. */
#include "pymergetic/metal/fw/memmap/__exports__.h"

#include "pymergetic/metal/dt.h"

#include "pymergetic/util/mem.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PM_METAL_MEMMAP_BLOB_MAX 4096u
#define EFI_CONVENTIONAL_MEMORY 7u

static pm_util_mem_arena_t *s_arena;
static uint32_t s_mb_magic;
static const void *s_mb_info;
static uint8_t s_blob[PM_METAL_MEMMAP_BLOB_MAX];
static uint32_t s_blob_len;
static int s_kind; /* 0 none, 1 mb, 2 mmap */

int32_t pm_metal_fw_memmap_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    return 0;
}

void pm_metal_fw_memmap_deinit(void) {
    s_arena = NULL;
    s_kind = 0;
    s_mb_info = NULL;
    s_mb_magic = 0;
    s_blob_len = 0;
}

int32_t pm_metal_fw_memmap_add(uint32_t base_lo, uint32_t base_hi, uint32_t len_lo, uint32_t len_hi) {
    if (s_arena == NULL || (len_lo == 0 && len_hi == 0)) {
        return -1;
    }
    return pm_metal_dt_add(PM_METAL_DT_CLASS_MEM, "memory", PM_METAL_DT_BUS_PLATFORM, base_lo,
        base_hi, len_lo, len_hi);
}

int32_t pm_metal_fw_memmap_count(void) {
    return pm_metal_dt_count_class(PM_METAL_DT_CLASS_MEM);
}

static uint32_t mmap_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t mmap_u64(const uint8_t *p) {
    return (uint64_t)mmap_u32(p) | ((uint64_t)mmap_u32(p + 4) << 32);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_u64(uint8_t *p, uint64_t v) {
    put_u32(p, (uint32_t)v);
    put_u32(p + 4, (uint32_t)(v >> 32));
}

static const uint8_t *fed_mmap(uint32_t *bytes) {
    if (s_kind == 2) {
        *bytes = s_blob_len;
        return s_blob;
    }
    if (s_kind == 1) {
        const uint32_t *w;
        if (s_mb_magic != 0x2BADB002u || s_mb_info == NULL) {
            return NULL;
        }
        w = (const uint32_t *)s_mb_info;
        if ((w[0] & (1u << 6)) == 0) {
            return NULL;
        }
        *bytes = w[11];
        return (const uint8_t *)(uintptr_t)w[12];
    }
    return NULL;
}

int32_t pm_metal_fw_memmap_load_mmap(const void *mmap, uint32_t bytes) {
    const uint8_t *p;
    const uint8_t *end;
    int32_t n = 0;
    if (s_arena == NULL || mmap == NULL || bytes < 24u) {
        return -1;
    }
    p = (const uint8_t *)mmap;
    end = p + bytes;
    while (p + 24u <= end) {
        uint32_t sz = mmap_u32(p);
        uint32_t type = mmap_u32(p + 20);
        uint32_t base_lo = mmap_u32(p + 4);
        uint32_t base_hi = mmap_u32(p + 8);
        uint32_t len_lo = mmap_u32(p + 12);
        uint32_t len_hi = mmap_u32(p + 16);
        uint32_t step = sz + 4u;
        if (type == 1u && (len_lo != 0 || len_hi != 0)) {
            if (pm_metal_fw_memmap_add(base_lo, base_hi, len_lo, len_hi) < 0) {
                return -1;
            }
            n++;
        }
        if (step < 24u) {
            break;
        }
        p += step;
    }
    return n > 0 ? 0 : -1;
}

int32_t pm_metal_fw_memmap_load_mb(uint32_t magic, const void *info) {
    const uint32_t *w;
    if (magic != 0x2BADB002u || info == NULL) {
        return -1;
    }
    w = (const uint32_t *)info;
    if ((w[0] & (1u << 6)) == 0) {
        return -1;
    }
    return pm_metal_fw_memmap_load_mmap((const void *)(uintptr_t)w[12], w[11]);
}

int32_t pm_metal_fw_memmap_load_efi(const void *map, uint32_t desc_size, uint32_t bytes) {
    if (pm_metal_fw_memmap_feed_efi(map, desc_size, bytes) != 0) {
        return -1;
    }
    return pm_metal_fw_memmap_load_mmap(s_blob, s_blob_len);
}

int32_t pm_metal_fw_memmap_feed(uint32_t magic, const void *info) {
    s_mb_magic = magic;
    s_mb_info = info;
    s_kind = 1;
    s_blob_len = 0;
    return 0;
}

int32_t pm_metal_fw_memmap_feed_mmap(const void *mmap, uint32_t bytes) {
    if (mmap == NULL || bytes < 24u || bytes > PM_METAL_MEMMAP_BLOB_MAX) {
        return -1;
    }
    memcpy(s_blob, mmap, bytes);
    s_blob_len = bytes;
    s_kind = 2;
    s_mb_info = NULL;
    s_mb_magic = 0;
    return 0;
}

static void rec_copy(uint8_t *dst, const uint8_t *src) {
    memcpy(dst, src, 24);
}

static void blob_sort_merge(uint8_t *blob, uint32_t *n) {
    uint32_t i;
    uint32_t j;
    uint32_t w;
    uint8_t tmp[24];
    if (*n < 48u) {
        return;
    }
    for (i = 24; i < *n; i += 24u) {
        rec_copy(tmp, blob + i);
        j = i;
        while (j >= 24u && mmap_u64(blob + j - 24u + 4) > mmap_u64(tmp + 4)) {
            rec_copy(blob + j, blob + j - 24u);
            j -= 24u;
        }
        rec_copy(blob + j, tmp);
    }
    w = 0;
    rec_copy(tmp, blob);
    for (i = 24; i < *n; i += 24u) {
        uint64_t a0 = mmap_u64(tmp + 4);
        uint64_t a1 = a0 + mmap_u64(tmp + 12);
        uint64_t b0 = mmap_u64(blob + i + 4);
        uint64_t b1 = b0 + mmap_u64(blob + i + 12);
        if (b0 <= a1) {
            if (b1 > a1) {
                put_u64(tmp + 12, b1 - a0);
            }
        } else {
            rec_copy(blob + w, tmp);
            w += 24u;
            rec_copy(tmp, blob + i);
        }
    }
    rec_copy(blob + w, tmp);
    *n = w + 24u;
}

int32_t pm_metal_fw_memmap_feed_efi(const void *map, uint32_t desc_size, uint32_t bytes) {
    const uint8_t *p;
    const uint8_t *end;
    uint32_t n = 0;
    if (map == NULL || desc_size < 32u || bytes < desc_size) {
        return -1;
    }
    p = (const uint8_t *)map;
    end = p + bytes;
    while (p + desc_size <= end && n + 24u <= PM_METAL_MEMMAP_BLOB_MAX) {
        uint32_t type = mmap_u32(p);
        uint64_t phys = mmap_u64(p + 8);
        uint64_t pages = mmap_u64(p + 24);
        uint64_t len = pages << 12;
        if (type == EFI_CONVENTIONAL_MEMORY && len >= 32u * 1024u) {
            put_u32(s_blob + n, 20);
            put_u64(s_blob + n + 4, phys);
            put_u64(s_blob + n + 12, len);
            put_u32(s_blob + n + 20, 1);
            n += 24u;
        }
        p += desc_size;
    }
    if (n < 24u) {
        return -1;
    }
    blob_sort_merge(s_blob, &n);
    s_blob_len = n;
    s_kind = 2;
    s_mb_info = NULL;
    s_mb_magic = 0;
    return 0;
}

static uint64_t align_up_u64(uint64_t x, uint64_t a) {
    return (x + (a - 1u)) & ~(a - 1u);
}

static int consider(uint64_t r0, uint64_t r1, uint64_t want, uint64_t *best_base, uint64_t *best_src) {
    uint64_t a;
    uint64_t n;
    if (r1 <= r0) {
        return 0;
    }
    a = align_up_u64(r0, 4096u);
    if (a >= r1) {
        return 0;
    }
    n = r1 - a;
    if (n < 32u * 1024u) {
        return 0;
    }
    if (n > *best_src) {
        *best_base = a;
        *best_src = n;
        return 1;
    }
    (void)want;
    return 0;
}

int32_t pm_metal_fw_memmap_pick(uint64_t avoid_lo, uint64_t avoid_hi, uint64_t want,
    uint64_t *out_base, uint64_t *out_len) {
    const uint8_t *p;
    const uint8_t *end;
    uint32_t bytes = 0;
    uint64_t best_base = 0;
    uint64_t best_src = 0;
    uint64_t n;
    if (out_base == NULL || out_len == NULL) {
        return -1;
    }
    p = fed_mmap(&bytes);
    if (p == NULL || bytes < 24u) {
        return -1;
    }
    if (avoid_hi < avoid_lo) {
        avoid_hi = avoid_lo;
    }
    end = p + bytes;
    while (p + 24u <= end) {
        uint32_t sz = mmap_u32(p);
        uint32_t type = mmap_u32(p + 20);
        uint64_t base = mmap_u64(p + 4);
        uint64_t len = mmap_u64(p + 12);
        uint64_t r1 = base + len;
        uint32_t step = sz + 4u;
        if (type == 1u && len != 0) {
            if (avoid_hi <= avoid_lo || r1 <= avoid_lo || base >= avoid_hi) {
                (void)consider(base, r1, want, &best_base, &best_src);
            } else {
                (void)consider(base, avoid_lo < r1 ? avoid_lo : r1, want, &best_base, &best_src);
                (void)consider(avoid_hi > base ? avoid_hi : base, r1, want, &best_base, &best_src);
            }
        }
        if (step < 24u) {
            break;
        }
        p += step;
    }
    if (best_src == 0) {
        return -1;
    }
    n = best_src;
    if (want != 0 && n > want) {
        n = want;
    }
    *out_base = best_base;
    *out_len = n;
    return 0;
}

#define PM_METAL_MEMMAP_SPARE_MIN (256u * 1024u)
#define PM_METAL_MEMMAP_SPARE_FLOOR (16u * 1024u * 1024u)

static int32_t spare_take(pm_util_mem_arena_t *arena, uint64_t r0, uint64_t r1) {
    uint64_t n;
    if (arena == NULL || r1 <= r0) {
        return 0;
    }
    r0 = align_up_u64(r0, 4096u);
    if (r0 < (uint64_t)PM_METAL_MEMMAP_SPARE_FLOOR) {
        r0 = (uint64_t)PM_METAL_MEMMAP_SPARE_FLOOR;
    }
    if (r1 <= r0) {
        return 0;
    }
    n = r1 - r0;
    if (n < (uint64_t)PM_METAL_MEMMAP_SPARE_MIN) {
        return 0;
    }
    if (r0 > (uint64_t)(uintptr_t)-1 || n > (uint64_t)(size_t)-1) {
        return 0;
    }
    (void)pm_util_mem_arena_add_pool(arena, (void *)(uintptr_t)r0, (size_t)n);
    return 0;
}

static int32_t spare_apply(pm_util_mem_arena_t *arena, uint64_t r0, uint64_t r1, const uint64_t *cuts,
    uint32_t ncut) {
    uint64_t clo;
    uint64_t chi;
    if (r1 <= r0) {
        return 0;
    }
    if (ncut == 0) {
        return spare_take(arena, r0, r1);
    }
    clo = cuts[0];
    chi = cuts[1];
    if (chi <= clo || r1 <= clo || r0 >= chi) {
        return spare_apply(arena, r0, r1, cuts + 2, ncut - 1u);
    }
    (void)spare_apply(arena, r0, clo < r1 ? clo : r1, cuts + 2, ncut - 1u);
    return spare_apply(arena, chi > r0 ? chi : r0, r1, cuts + 2, ncut - 1u);
}

static void stack_cut(uint64_t *lo, uint64_t *hi) {
    uintptr_t sp = 0;
#if defined(__x86_64__)
    __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
#elif defined(__i386__)
    __asm__ volatile("mov %%esp, %0" : "=r"(sp));
#elif defined(__aarch64__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#elif defined(__arm__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#endif
    if (sp == 0) {
        *lo = 0;
        *hi = 0;
        return;
    }
    *lo = (uint64_t)sp > (1024ull * 1024ull) ? (uint64_t)sp - (1024ull * 1024ull) : 0;
    *hi = (uint64_t)sp + (64ull * 1024ull);
}

static int32_t spare_punch(pm_util_mem_arena_t *arena, uint64_t r0, uint64_t r1, uint64_t used_lo,
    uint64_t used_hi) {
    uint64_t stack_lo = 0;
    uint64_t stack_hi = 0;
    uint64_t cuts[4];
    stack_cut(&stack_lo, &stack_hi);
    cuts[0] = used_lo;
    cuts[1] = used_hi;
    cuts[2] = stack_lo;
    cuts[3] = stack_hi;
    return spare_apply(arena, r0, r1, cuts, 2u);
}

int32_t pm_metal_fw_memmap_add_spare(pm_util_mem_arena_t *arena, uint64_t avoid_lo, uint64_t avoid_hi,
    uint64_t used_lo, uint64_t used_hi) {
    const uint8_t *p;
    const uint8_t *end;
    uint32_t bytes = 0;
    if (arena == NULL) {
        return -1;
    }
    p = fed_mmap(&bytes);
    if (p == NULL || bytes < 24u) {
        return 0;
    }
    if (avoid_hi < avoid_lo) {
        avoid_hi = avoid_lo;
    }
    if (used_hi < used_lo) {
        used_hi = used_lo;
    }
    end = p + bytes;
    while (p + 24u <= end) {
        uint32_t sz = mmap_u32(p);
        uint32_t type = mmap_u32(p + 20);
        uint64_t base = mmap_u64(p + 4);
        uint64_t len = mmap_u64(p + 12);
        uint64_t r1 = base + len;
        uint32_t step = sz + 4u;
        if (type == 1u && len != 0) {
            if (avoid_hi <= avoid_lo || r1 <= avoid_lo || base >= avoid_hi) {
                (void)spare_punch(arena, base, r1, used_lo, used_hi);
            } else {
                (void)spare_punch(arena, base, avoid_lo < r1 ? avoid_lo : r1, used_lo, used_hi);
                (void)spare_punch(arena, avoid_hi > base ? avoid_hi : base, r1, used_lo, used_hi);
            }
        }
        if (step < 24u) {
            break;
        }
        p += step;
    }
    return 0;
}

int32_t pm_metal_fw_memmap_probe(void) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_kind == 0) {
        return 0;
    }
    if (s_kind == 1) {
        if (pm_metal_fw_memmap_load_mb(s_mb_magic, s_mb_info) != 0) {
            return -1;
        }
    } else if (pm_metal_fw_memmap_load_mmap(s_blob, s_blob_len) != 0) {
        return -1;
    }
    return pm_metal_fw_memmap_count() > 0 ? 0 : -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_init, pm_metal_fw_memmap_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_deinit, pm_metal_fw_memmap_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_add, pm_metal_fw_memmap_add, int32_t(uint32_t, uint32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_count, pm_metal_fw_memmap_count, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_load_mmap, pm_metal_fw_memmap_load_mmap, int32_t(const void *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_load_mb, pm_metal_fw_memmap_load_mb, int32_t(uint32_t, const void *));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_load_efi, pm_metal_fw_memmap_load_efi, int32_t(const void *, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_feed, pm_metal_fw_memmap_feed, int32_t(uint32_t, const void *));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_feed_mmap, pm_metal_fw_memmap_feed_mmap, int32_t(const void *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_feed_efi, pm_metal_fw_memmap_feed_efi, int32_t(const void *, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_pick, pm_metal_fw_memmap_pick, int32_t(uint64_t, uint64_t, uint64_t, uint64_t *, uint64_t *));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_add_spare, pm_metal_fw_memmap_add_spare, int32_t(pm_util_mem_arena_t *, uint64_t, uint64_t, uint64_t, uint64_t));
PM_MOD_EXPORT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_probe, pm_metal_fw_memmap_probe, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.fw.memmap, pm_metal_fw_memmap_init, pm_metal_fw_memmap_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.fw.memmap, pymergetic.metal.dt);

/*
 * Product Metal heap — dual-span arena + TLSF on the high side.
 *
 *   low (map_brk →)                 (← heap_brk) high
 *   [ map / stacks / grants ][ HOLE ][ TLSF pools ]
 *
 * Same path on firmware claim_arena and browser HAL claim (malloc window).
 */
#include "pymergetic/metal/mem.h"

#include "tlsf.h"

#include <stdint.h>
#include <string.h>

enum { PAGE_SIZE = 4096u, MIN_ARENA = PAGE_SIZE * 8u };

/*
 * Initial TLSF is a *seed*, not “take the arena”. Hole stays majority so map
 * (and later heap_grow pools) have room at every scale:
 *
 *   want = clamp(span/8, MIN_SEED, CAP_SEED);  // ~12.5%
 *   if span is comfortable: never more than 25% at init (≥75% hole)
 *
 *   4 MiB   → ~512 KiB tlsf / ~3.5 MiB hole
 *   16 MiB  → ~2 MiB / ~14 MiB
 *   128 MiB → ~16 MiB / ~112 MiB
 *   128 GiB → CAP 128 MiB tlsf / rest hole
 */
#ifndef PM_METAL_MEM_TLSF_INIT_CAP
#define PM_METAL_MEM_TLSF_INIT_CAP (128u * 1024u * 1024u)
#endif
#ifndef PM_METAL_MEM_TLSF_INIT_MIN
#define PM_METAL_MEM_TLSF_INIT_MIN (256u * 1024u)
#endif

static tlsf_t g_tlsf;
static uint8_t *g_base;
static uint8_t *g_end;
static uint8_t *g_map_brk;
static uint8_t *g_heap_brk;
static size_t g_bytes;
static size_t g_free_est;

void uart_puts(const char *s) __attribute__((weak));

static size_t align_up(size_t x, size_t a)
{
    return (x + (a - 1u)) & ~(a - 1u);
}

static size_t align_down(size_t x, size_t a)
{
    return x & ~(a - 1u);
}

/* First TLSF pool size for a claimed span (page-aligned). */
static size_t initial_tlsf_bytes(size_t span, size_t structural_min)
{
    size_t min_seed;
    size_t want;
    size_t min_hole;

    min_seed = structural_min;
    if (min_seed < (size_t)PM_METAL_MEM_TLSF_INIT_MIN) {
        min_seed = (size_t)PM_METAL_MEM_TLSF_INIT_MIN;
    }
    min_seed = align_up(min_seed, PAGE_SIZE);
    if (min_seed > span) {
        min_seed = align_down(span, PAGE_SIZE);
    }

    /* ~⅛ of span, clamped to [min_seed, CAP]. */
    want = span / 8u;
    if (want < min_seed) {
        want = min_seed;
    }
    if (want > (size_t)PM_METAL_MEM_TLSF_INIT_CAP) {
        want = (size_t)PM_METAL_MEM_TLSF_INIT_CAP;
    }

    min_hole = (size_t)PAGE_SIZE * 8u;
    if (span >= (4u * 1024u * 1024u)) {
        /* Comfortable (≥4 MiB): keep ≥75% hole for map + future pools. */
        size_t max_init = align_down(span / 4u, PAGE_SIZE);
        if (max_init < min_seed) {
            max_init = min_seed;
        }
        if (want > max_init) {
            want = max_init;
        }
    } else {
        /*
         * Small product/static heaps: TLSF gets almost the whole span.
         * Leaving ⅞ as hole here starved vt/tui (~250 KiB FB) on 512 KiB.
         * Grow-from-hole still works on larger seats after a tiny seed.
         */
        if (span > min_hole + structural_min) {
            want = align_down(span - min_hole, PAGE_SIZE);
        } else {
            want = align_down(span, PAGE_SIZE);
        }
        if (want < min_seed && span >= min_seed) {
            want = min_seed;
        }
    }

    want = align_down(want, PAGE_SIZE);
    if (want < structural_min) {
        want = align_up(structural_min, PAGE_SIZE);
        if (want > span) {
            want = align_down(span, PAGE_SIZE);
        }
    }
    return want;
}

int32_t pm_metal_mem_init(uint8_t *base, size_t bytes)
{
    uintptr_t b;
    uintptr_t al;
    size_t skip;
    size_t structural_min;
    size_t want;
    uint8_t *pool;

    if (base == NULL || bytes < MIN_ARENA) {
        return -1;
    }

    /* Page-align the window (static product heap may only be 16-byte aligned). */
    b = (uintptr_t)base;
    al = align_up(b, PAGE_SIZE);
    skip = (size_t)(al - b);
    if (bytes <= skip + MIN_ARENA) {
        return -1;
    }
    base = (uint8_t *)al;
    bytes = align_down(bytes - skip, PAGE_SIZE);
    if (bytes < MIN_ARENA) {
        return -1;
    }

    g_base = base;
    g_end = base + bytes;
    g_map_brk = base;
    g_heap_brk = g_end;
    g_bytes = bytes;
    g_tlsf = NULL;

    structural_min = tlsf_size() + tlsf_pool_overhead() + 64u;
    structural_min = align_up(structural_min, PAGE_SIZE);
    want = initial_tlsf_bytes(bytes, structural_min);
    if (want < structural_min || want > bytes) {
        return -1;
    }

    g_heap_brk = g_end - want;
    pool = g_heap_brk;
    g_tlsf = tlsf_create_with_pool(pool, want);
    if (g_tlsf == NULL) {
        g_base = NULL;
        g_end = NULL;
        g_map_brk = NULL;
        g_heap_brk = NULL;
        g_bytes = 0;
        return -1;
    }
    g_free_est = want;
    return 0;
}

/* Carve more high-side pool from the hole; returns 0 on success. */
static int heap_grow_pool(size_t need)
{
    size_t hole;
    size_t grow;
    uint8_t *pool;
    pool_t added;

    if (g_tlsf == NULL || g_map_brk == NULL || g_heap_brk == NULL) {
        return -1;
    }
    hole = (size_t)(g_heap_brk - g_map_brk);
    grow = align_up(need + tlsf_pool_overhead() + 64u, PAGE_SIZE);
    /* Prefer growing in useful chunks, not one page at a time. */
    if (grow < (size_t)PM_METAL_MEM_TLSF_INIT_MIN) {
        grow = (size_t)PM_METAL_MEM_TLSF_INIT_MIN;
    }
    grow = align_up(grow, PAGE_SIZE);
    if (grow > hole) {
        grow = align_down(hole, PAGE_SIZE);
    }
    if (grow < tlsf_pool_overhead() + 64u) {
        return -1;
    }
    g_heap_brk -= grow;
    pool = g_heap_brk;
    added = tlsf_add_pool(g_tlsf, pool, grow);
    if (added == NULL) {
        g_heap_brk += grow;
        return -1;
    }
    g_free_est += grow;
    return 0;
}

uint8_t *pm_metal_mem_alloc(size_t size)
{
    void *p;
    if (g_tlsf == NULL || size == 0) {
        return NULL;
    }
    p = tlsf_malloc(g_tlsf, size);
    if (p == NULL && heap_grow_pool(size) == 0) {
        p = tlsf_malloc(g_tlsf, size);
    }
    return (uint8_t *)p;
}

void pm_metal_mem_free(uint8_t *ptr)
{
    pm_metal_mem_free_checked(ptr, __builtin_return_address(0));
}

void pm_metal_mem_free_checked(uint8_t *ptr, const void *retaddr)
{
    (void)retaddr;
    if (g_tlsf == NULL || ptr == NULL) {
        return;
    }
    if (g_base != NULL && (ptr < g_base || ptr >= g_end)) {
        if (uart_puts) {
            uart_puts("bad free\n");
        }
        return;
    }
    tlsf_free(g_tlsf, ptr);
}

uint8_t *pm_metal_mem_memalign(size_t align, size_t size)
{
    void *p;
    if (g_tlsf == NULL || size == 0) {
        return NULL;
    }
    if (align < sizeof(void *)) {
        align = sizeof(void *);
    }
    p = tlsf_memalign(g_tlsf, align, size);
    if (p == NULL && heap_grow_pool(size + align) == 0) {
        p = tlsf_memalign(g_tlsf, align, size);
    }
    return (uint8_t *)p;
}

uint8_t *pm_metal_mem_realloc(uint8_t *ptr, size_t size)
{
    void *p;
    if (g_tlsf == NULL) {
        return NULL;
    }
    if (size == 0) {
        pm_metal_mem_free(ptr);
        return NULL;
    }
    p = tlsf_realloc(g_tlsf, ptr, size);
    if (p == NULL && size > 0 && heap_grow_pool(size) == 0) {
        p = tlsf_realloc(g_tlsf, ptr, size);
    }
    return (uint8_t *)p;
}

size_t pm_metal_mem_heap_bytes(void)
{
    /* High-side TLSF carve (durable heap), not the full claimed window. */
    if (g_base == NULL || g_heap_brk == NULL || g_end == NULL) {
        return 0;
    }
    return (size_t)(g_end - g_heap_brk);
}

size_t pm_metal_mem_free_bytes(void)
{
    (void)g_base;
    return g_free_est;
}

size_t pm_metal_mem_span_bytes(void)
{
    return g_bytes;
}

uintptr_t pm_metal_mem_base(void)
{
    return (uintptr_t)g_base;
}

size_t pm_metal_mem_map_used(void)
{
    if (g_base == NULL || g_map_brk == NULL) {
        return 0;
    }
    return (size_t)(g_map_brk - g_base);
}

size_t pm_metal_mem_hole(void)
{
    if (g_map_brk == NULL || g_heap_brk == NULL) {
        return 0;
    }
    return (size_t)(g_heap_brk - g_map_brk);
}

uint8_t *pm_metal_mem_map(size_t bytes)
{
    size_t need;
    uint8_t *p;
    if (g_base == NULL || bytes == 0) {
        return NULL;
    }
    need = align_up(bytes, PAGE_SIZE);
    if (need > (size_t)(g_heap_brk - g_map_brk)) {
        return NULL;
    }
    p = g_map_brk;
    g_map_brk += need;
    return p;
}

#ifndef PM_METAL_MEM_GUEST_SLOTS
#define PM_METAL_MEM_GUEST_SLOTS 64u
#endif

typedef struct {
    uint8_t *ptr;
    uint32_t size;
} guest_slot_t;

static guest_slot_t g_guest[PM_METAL_MEM_GUEST_SLOTS];

uint32_t pm_metal_mem_guest_alloc(uint32_t size)
{
    uint32_t i;
    uint8_t *p;

    if (size == 0u) {
        return 0u;
    }
    for (i = 1u; i < PM_METAL_MEM_GUEST_SLOTS; i++) {
        if (g_guest[i].ptr == NULL) {
            p = pm_metal_mem_alloc((size_t)size);
            if (p == NULL) {
                return 0u;
            }
            g_guest[i].ptr = p;
            g_guest[i].size = size;
            return i;
        }
    }
    return 0u;
}

void pm_metal_mem_guest_free(uint32_t cookie)
{
    if (cookie == 0u || cookie >= PM_METAL_MEM_GUEST_SLOTS) {
        return;
    }
    if (g_guest[cookie].ptr != NULL) {
        pm_metal_mem_free(g_guest[cookie].ptr);
        g_guest[cookie].ptr = NULL;
        g_guest[cookie].size = 0u;
    }
}

uint8_t *pm_metal_mem_guest_ptr(uint32_t cookie)
{
    if (cookie == 0u || cookie >= PM_METAL_MEM_GUEST_SLOTS) {
        return NULL;
    }
    return g_guest[cookie].ptr;
}

uint32_t pm_metal_mem_guest_size(uint32_t cookie)
{
    if (cookie == 0u || cookie >= PM_METAL_MEM_GUEST_SLOTS) {
        return 0u;
    }
    return g_guest[cookie].size;
}

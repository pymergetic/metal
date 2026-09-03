/* rsx_hwm.c — arena high-water audit for the rsx compiler: compile sources
 * with 0..200 functions in fresh arenas and report the heap draw per
 * compile. The compiler allocates one fixed block per table at Lower::new
 * (Lower ~26 KiB + FnTab ~84 KiB + SymTab ~1.03 MiB — the n=0 row) and one
 * 23 KiB LocalTab per function body, never freed until the arena dies — so
 * the draw is linear in function count.
 *
 * Reading the numbers: pm_util_mem_arena_heap_used only moves when the
 * tlsf heap carves a NEW pool (first pool = span/8, min 256 KiB), so the
 * draw is pool-growth quantized — the slope columns understate the true
 * 23 KiB/fn between pool growths and jump when a new pool is carved. The
 * "fixed" row (n=0) shows the compiler's resident cost. tools/ posture:
 * measurement only, not a prove gate. */
#include "pymergetic/metal/jit/rs/compiler/__types__.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build a source with n functions into buf (arena-free: caller's stack is
 * fine in a tools/ binary — the no-large-stack-buffers rule is for
 * in-kernel cards). */
static size_t build_src(char *buf, size_t cap, uint32_t n) {
    size_t w = 0;
    uint32_t i;
    w += (size_t)snprintf(buf + w, cap - w,
        "#![allow(non_camel_case_types)]\n");
    for (i = 0; i < n; i++) {
        int k = snprintf(buf + w, cap - w,
            "fn f%u(a: u32) -> u32 { let b = a + %u; b }\n", i, i + 1u);
        if (k < 0 || (size_t)k >= cap - w) {
            return 0;
        }
        w += (size_t)k;
    }
    return w;
}

static int measure(uint32_t n, size_t *draw_out, size_t *c_len_out) {
    /* The span must FIT the draw (fixed ~1.2 MB of tables + one 23 KiB
     * LocalTab per function + token spans + the C output) but keep the
     * first pool below it, hence the per-function headroom. */
    enum { SRC_CAP = 4u * 1024u * 1024u };
    static char src[SRC_CAP];
    size_t span = 4u * 1024u * 1024u + (size_t)n * 128u * 1024u;
    size_t src_len = build_src(src, sizeof(src), n);
    void *backing = malloc(span);
    pm_util_mem_arena_t *arena;
    char *c = NULL;
    size_t c_len = 0;
    char err[256];
    size_t used0, used1;
    int32_t rc;

    if (src_len == 0) {
        fprintf(stderr, "rsx_hwm: source build failed for %u fns\n", n);
        return -1;
    }
    if (backing == NULL) {
        return -1;
    }
    arena = pm_util_mem_arena_create(backing, span);
    if (arena == NULL) {
        free(backing);
        return -1;
    }
    err[0] = '\0';
    used0 = pm_util_mem_arena_heap_used(arena);
    rc = pm_metal_jit_rsx_compile(arena, src, src_len, &c, &c_len,
        err, sizeof(err));
    used1 = pm_util_mem_arena_heap_used(arena);
    if (rc != 0) {
        fprintf(stderr, "rsx_hwm: compile failed for %u fns: %s\n", n,
            err[0] != '\0' ? err : "(no message)");
        pm_util_mem_arena_destroy(arena);
        free(backing);
        return -1;
    }
    *draw_out = used1 - used0;
    *c_len_out = c_len;
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

int main(void) {
    static const uint32_t Ns[] = { 0, 1, 2, 4, 8, 16, 32, 64, 128, 200 };
    size_t prev_draw = 0;
    size_t draw, c_len;
    uint32_t prev_n = 0;
    size_t k;

    /* No boot needed: pm_metal_jit_rsx_compile is a plain link-time C face
     * taking the arena explicitly. The finer sweep (0..200, powers of two)
     * separates the fixed draw (Lower+FnTab+SymTab: the n=0 row) from the
     * per-function slope (LocalTab 23 KiB/fn, pool-growth quantized). */

    printf("%6s %14s %14s %12s\n", "fns", "heap draw", "C bytes", "per fn");
    for (k = 0; k < sizeof(Ns) / sizeof(Ns[0]); k++) {
        uint32_t n = Ns[k];
        if (measure(n, &draw, &c_len) != 0) {
            return 1;
        }
        printf("%6u %14zu %14zu", n, draw, c_len);
        if (prev_n != 0 && n > prev_n) {
            printf(" %12zu", (draw - prev_draw) / (size_t)(n - prev_n));
        } else {
            printf(" %12s", "-");
        }
        printf("\n");
        prev_draw = draw;
        prev_n = n;
    }
    return 0;
}

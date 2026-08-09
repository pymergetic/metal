/*
 * Browser seat mem HAL — claim page-aligned dual-span window via malloc.
 * Budget = loadMicroPython({ heapsize }) so the UI selector sizes Metal area.
 */
#include "api.h"

#include <stdlib.h>

#ifndef PM_METAL_WASM_ARENA_BYTES
#define PM_METAL_WASM_ARENA_BYTES (4u * 1024u * 1024u)
#endif

enum { PAGE = 4096u };

static size_t g_budget;

void pm_metal_hal_mem_set_budget(size_t bytes)
{
    g_budget = bytes;
}

size_t pm_metal_hal_mem_budget(void)
{
    return g_budget ? g_budget : (size_t)PM_METAL_WASM_ARENA_BYTES;
}

int pm_metal_hal_mem_claim(uint8_t **base, size_t *bytes)
{
    void *p;
    size_t n = pm_metal_hal_mem_budget();

    if (base == NULL || bytes == NULL) {
        return -1;
    }
    /* Page-align up so dual-span init does not shrink the seat budget. */
    n = (n + (PAGE - 1u)) & ~(size_t)(PAGE - 1u);
    if (n < PAGE * 8u) {
        n = PAGE * 8u;
    }
#if defined(__EMSCRIPTEN__) || defined(__wasm__)
    p = aligned_alloc(PAGE, n);
#else
    if (posix_memalign(&p, PAGE, n) != 0) {
        p = NULL;
    }
#endif
    if (p == NULL) {
        return -1;
    }
    *base = (uint8_t *)p;
    *bytes = n;
    return 0;
}

void uart_puts(const char *s)
{
    pm_metal_hal_console_puts(s);
}

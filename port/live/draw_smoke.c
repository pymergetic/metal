#include "draw_smoke.h"

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/draw.h"

void uart_puts(const char *s);

/* Soft RGB565 surface — console→DS blit path without real FB. */
static uint8_t g_soft_fb[160 * 48 * 2] __attribute__((aligned(8)));

int pm_metal_draw_smoke(void)
{
    pm_metal_draw_surface_t ds;
    uint32_t sum;

    if (pm_metal_draw_soft_init(&ds, g_soft_fb, 160, 48, 16) != 0) {
        uart_puts("draw init fail\n");
        return -1;
    }
    pm_metal_draw_fill(&ds, 0xFF001122u);
    pm_metal_draw_text8(&ds, 8, 8, "draw ok", 0xFFFFFFFFu, 0xFF001122u);
    sum = pm_metal_draw_checksum(&ds);
    if (sum == 0) {
        uart_puts("draw checksum fail\n");
        return -1;
    }
    uart_puts("draw ok\n");
    return 0;
}

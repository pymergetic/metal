#include "tui_smoke.h"

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/draw.h"
#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/tui.h"
#include "pymergetic/metal/vt.h"

void uart_puts(const char *s);

int pm_metal_tui_smoke(void)
{
    pm_metal_draw_surface_t ds;
    uint8_t *fb;
    uint32_t need;
    uint32_t sum;

    /* Soft FB: 80×25 @ 8×8 glyphs, RGB565 (640×200). */
    need = (uint32_t)PM_METAL_VT_COLS * 8u * (uint32_t)PM_METAL_VT_ROWS * 8u * 2u;
    fb = pm_metal_mem_alloc(need);
    if (fb == NULL) {
        uart_puts("tui alloc fail\n");
        return -1;
    }

    if (!pm_metal_vt_ready()) {
        if (pm_metal_vt_init() != 0) {
            uart_puts("tui vt init fail\n");
            pm_metal_mem_free(fb);
            return -1;
        }
    }

    if (pm_metal_tui_init() != 0) {
        uart_puts("tui init fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    if (pm_metal_draw_soft_init(&ds, fb, (uint32_t)PM_METAL_VT_COLS * 8u,
                                (uint32_t)PM_METAL_VT_ROWS * 8u, 16) != 0) {
        uart_puts("tui ds fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    pm_metal_vt_bind_surface(PM_METAL_TUI_VT_INDEX, &ds);

    if (pm_metal_tui_render_vt(PM_METAL_TUI_VT_INDEX) != 0) {
        uart_puts("tui render fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    sum = pm_metal_draw_checksum(&ds);
    if (sum == 0u) {
        uart_puts("tui checksum fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    pm_metal_mem_free(fb);
    uart_puts("tui ok\n");
    return 0;
}

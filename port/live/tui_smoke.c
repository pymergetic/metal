#include "tui_smoke.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/draw.h"
#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/shell/tui/__init__.h"
#include "pymergetic/metal/shell/vt/__init__.h"

void uart_puts(const char *s);

static int row_has(const char *row, const char *needle)
{
    size_t n;
    size_t i;

    if (row == NULL || needle == NULL) {
        return 0;
    }
    n = strlen(needle);
    if (n == 0u || n > (size_t)PM_METAL_VT_COLS) {
        return 0;
    }
    for (i = 0; i + n <= (size_t)PM_METAL_VT_COLS; i++) {
        if (memcmp(row + i, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

int pm_metal_tui_smoke(void)
{
    pm_metal_draw_surface_t ds;
    pm_metal_vt_t *vt;
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

    vt = pm_metal_vt_get(PM_METAL_TUI_VT_INDEX);
    if (vt == NULL || !row_has(vt->cells[13], "ip ") || !row_has(vt->cells[13], "dhcp")) {
        uart_puts("tui net pane fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }
    /* sshc optional until DIY client lands; stub marks ssh only. */
    if (!row_has(vt->cells[14], "http") || !row_has(vt->cells[14], "ssh") ||
        !row_has(vt->cells[14], "ntp") || !row_has(vt->cells[14], "tftp")) {
        uart_puts("tui faces fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    pm_metal_mem_free(fb);
    uart_puts("tui ok\n");
    return 0;
}

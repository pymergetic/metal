#include "vt_smoke.h"

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/draw.h"
#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/vt.h"

void uart_puts(const char *s);

int pm_metal_vt_smoke(void)
{
    pm_metal_draw_surface_t ds;
    uint8_t *fb;
    uint32_t need;
    uint32_t sum0;
    uint32_t sum1;
    pm_metal_vt_t *vt0;
    pm_metal_vt_t *vt1;

    /* Soft FB for 80×25 @ 8×8 glyphs, RGB565. Needs floor TLSF. */
    need = (uint32_t)PM_METAL_VT_COLS * 8u * (uint32_t)PM_METAL_VT_ROWS * 8u * 2u;
    fb = pm_metal_mem_alloc(need);
    if (fb == NULL) {
        uart_puts("vt alloc fail\n");
        return -1;
    }

    if (pm_metal_vt_init() != 0) {
        uart_puts("vt init fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    if (pm_metal_draw_soft_init(&ds, fb, (uint32_t)PM_METAL_VT_COLS * 8u,
                                (uint32_t)PM_METAL_VT_ROWS * 8u, 16) != 0) {
        uart_puts("vt ds fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    pm_metal_vt_bind_surface(0, &ds);

    if (pm_metal_vt_switch(0) != 0) {
        uart_puts("vt switch0 fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }
    pm_metal_vt_puts("F1 shell\n");

    if (pm_metal_vt_switch(1) != 0) {
        uart_puts("vt switch1 fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }
    pm_metal_vt_puts("F2 shell\n");

    if (pm_metal_vt_active() != 1) {
        uart_puts("vt active fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    vt0 = pm_metal_vt_get(0);
    vt1 = pm_metal_vt_get(1);
    if (vt0 == NULL || vt1 == NULL || vt0->cells[0][0] != 'F' || vt1->cells[0][0] != 'F') {
        uart_puts("vt cells fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    if (pm_metal_vt_render(0) != 0) {
        uart_puts("vt render fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }
    sum0 = pm_metal_draw_checksum(&ds);
    if (sum0 == 0) {
        uart_puts("vt checksum0 fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    /* Switch back to F1 and confirm isolation + re-render still non-empty. */
    if (pm_metal_vt_switch(0) != 0 || pm_metal_vt_active() != 0) {
        uart_puts("vt back fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }
    if (pm_metal_vt_render(0) != 0) {
        uart_puts("vt render2 fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }
    sum1 = pm_metal_draw_checksum(&ds);
    if (sum1 == 0 || sum1 != sum0) {
        uart_puts("vt checksum1 fail\n");
        pm_metal_mem_free(fb);
        return -1;
    }

    pm_metal_mem_free(fb);
    uart_puts("vt ok\n");
    return 0;
}

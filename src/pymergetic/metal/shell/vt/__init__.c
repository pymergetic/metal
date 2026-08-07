#include "pymergetic/metal/vt.h"

#include <string.h>

static pm_metal_vt_t g_vts[PM_METAL_VT_COUNT];
static int32_t g_active;
static int32_t g_ready;

static void vt_clear(pm_metal_vt_t *vt) {
    int r, c;
    memset(vt->cells, ' ', sizeof(vt->cells));
    for (r = 0; r < PM_METAL_VT_ROWS; r++) {
        for (c = 0; c < PM_METAL_VT_COLS; c++) {
            vt->cells[r][c] = ' ';
        }
    }
    vt->cursor_c = 0;
    vt->cursor_r = 0;
    vt->ds = NULL;
}

static void vt_scroll(pm_metal_vt_t *vt) {
    memmove(&vt->cells[0][0], &vt->cells[1][0],
            (size_t)(PM_METAL_VT_ROWS - 1) * (size_t)PM_METAL_VT_COLS);
    memset(&vt->cells[PM_METAL_VT_ROWS - 1][0], ' ', (size_t)PM_METAL_VT_COLS);
    vt->cursor_r = PM_METAL_VT_ROWS - 1;
    vt->cursor_c = 0;
}

static void vt_putc(pm_metal_vt_t *vt, char ch) {
    if (ch == '\r') {
        vt->cursor_c = 0;
        return;
    }
    if (ch == '\n') {
        vt->cursor_c = 0;
        vt->cursor_r++;
        if (vt->cursor_r >= PM_METAL_VT_ROWS) {
            vt_scroll(vt);
        }
        return;
    }
    if (ch == '\t') {
        int next = (vt->cursor_c + 8) & ~7;
        while (vt->cursor_c < next && vt->cursor_c < PM_METAL_VT_COLS) {
            vt->cells[vt->cursor_r][vt->cursor_c++] = ' ';
        }
        if (vt->cursor_c >= PM_METAL_VT_COLS) {
            vt_putc(vt, '\n');
        }
        return;
    }
    if (ch < 0x20 || ch > 0x7E) {
        ch = '?';
    }
    if (vt->cursor_c >= PM_METAL_VT_COLS) {
        vt_putc(vt, '\n');
    }
    vt->cells[vt->cursor_r][vt->cursor_c++] = ch;
    if (vt->cursor_c >= PM_METAL_VT_COLS) {
        vt_putc(vt, '\n');
    }
}

int32_t pm_metal_vt_init(void) {
    int i;
    for (i = 0; i < PM_METAL_VT_COUNT; i++) {
        vt_clear(&g_vts[i]);
    }
    g_active = 0;
    g_ready = 1;
    return 0;
}

int32_t pm_metal_vt_ready(void) {
    return g_ready;
}

int32_t pm_metal_vt_switch(int32_t index) {
    if (!g_ready || index < 0 || index >= PM_METAL_VT_COUNT) {
        return -1;
    }
    g_active = index;
    return 0;
}

int32_t pm_metal_vt_active(void) {
    return g_active;
}

pm_metal_vt_t *pm_metal_vt_get(int32_t index) {
    if (index < 0 || index >= PM_METAL_VT_COUNT) {
        return NULL;
    }
    return &g_vts[index];
}

void pm_metal_vt_bind_surface(int32_t index, pm_metal_draw_surface_t *ds) {
    pm_metal_vt_t *vt = pm_metal_vt_get(index);
    if (vt != NULL) {
        vt->ds = ds;
    }
}

void pm_metal_vt_write(const char *s, size_t n) {
    pm_metal_vt_t *vt;
    size_t i;
    if (!g_ready || s == NULL) {
        return;
    }
    vt = &g_vts[g_active];
    for (i = 0; i < n; i++) {
        vt_putc(vt, s[i]);
    }
}

void pm_metal_vt_puts(const char *s) {
    if (s == NULL) {
        return;
    }
    pm_metal_vt_write(s, strlen(s));
}

int32_t pm_metal_vt_render(int32_t index) {
    pm_metal_vt_t *vt;
    int r, c;
    if (!g_ready) {
        return -1;
    }
    vt = pm_metal_vt_get(index);
    if (vt == NULL || vt->ds == NULL) {
        return -1;
    }
    pm_metal_draw_fill(vt->ds, 0xFF000000u);
    for (r = 0; r < PM_METAL_VT_ROWS; r++) {
        for (c = 0; c < PM_METAL_VT_COLS; c++) {
            char ch = vt->cells[r][c];
            if (ch == ' ') {
                continue;
            }
            pm_metal_draw_glyph8(vt->ds, c * 8, r * 8, ch, 0xFFFFFFFFu, 0xFF000000u);
        }
    }
    return 0;
}

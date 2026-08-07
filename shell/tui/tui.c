#include "pymergetic/metal/tui.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/console.h"
#include "pymergetic/metal/dev/net.h"
#include "pymergetic/metal/draw.h"
#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/net/faces.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/vt.h"

#ifndef METAL_ENGINE
#define METAL_ENGINE "mp"
#endif

#define TUI_SPLIT 39
#define TUI_LEFT_INNER 38
#define TUI_RIGHT_START 41
#define TUI_RIGHT_INNER 38

static int32_t g_ready;

static void str_copy(char *out, size_t cap, const char *src)
{
    size_t i;

    if (out == NULL || cap == 0u) {
        return;
    }
    if (src == NULL) {
        out[0] = '\0';
        return;
    }
    for (i = 0; i + 1u < cap && src[i] != '\0'; i++) {
        out[i] = src[i];
    }
    out[i] = '\0';
}

static void str_append(char *out, size_t cap, const char *src)
{
    size_t len;
    size_t i;

    if (out == NULL || cap == 0u || src == NULL) {
        return;
    }
    len = strlen(out);
    for (i = 0; len + i + 1u < cap && src[i] != '\0'; i++) {
        out[len + i] = src[i];
    }
    out[len + i] = '\0';
}

static void utoa_size(size_t v, char *out, size_t cap)
{
    char tmp[24];
    size_t n = 0;
    size_t i;

    if (out == NULL || cap == 0u) {
        return;
    }
    if (v == 0u) {
        if (cap > 1u) {
            out[0] = '0';
            out[1] = '\0';
        }
        return;
    }
    while (v > 0u && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    if (n >= cap) {
        n = cap - 1u;
    }
    for (i = 0; i < n; i++) {
        out[i] = tmp[n - 1u - i];
    }
    out[n] = '\0';
}

static void vt_fill_row(char row[PM_METAL_VT_COLS], char ch)
{
    memset(row, ch, (size_t)PM_METAL_VT_COLS);
}

static void vt_put(pm_metal_vt_t *vt, int32_t r, int32_t c, char ch)
{
    if (vt == NULL || r < 0 || c < 0 || r >= PM_METAL_VT_ROWS || c >= PM_METAL_VT_COLS) {
        return;
    }
    vt->cells[r][c] = ch;
}

static void vt_puts_at(pm_metal_vt_t *vt, int32_t r, int32_t c, const char *s)
{
    int32_t col = c;
    if (vt == NULL || s == NULL) {
        return;
    }
    while (*s != '\0' && col < PM_METAL_VT_COLS) {
        vt_put(vt, r, col++, *s++);
    }
}

static void vt_hline(pm_metal_vt_t *vt, int32_t r, int32_t c0, int32_t c1, char ch)
{
    int32_t c;
    for (c = c0; c <= c1 && c < PM_METAL_VT_COLS; c++) {
        vt_put(vt, r, c, ch);
    }
}

static void vt_box_row(pm_metal_vt_t *vt, int32_t r)
{
    vt_put(vt, r, 0, '+');
    vt_hline(vt, r, 1, TUI_SPLIT - 1, '-');
    vt_put(vt, r, TUI_SPLIT, '+');
    vt_hline(vt, r, TUI_SPLIT + 1, PM_METAL_VT_COLS - 2, '-');
    vt_put(vt, r, PM_METAL_VT_COLS - 1, '+');
}

static void vt_pane_row(pm_metal_vt_t *vt, int32_t r, const char *left, const char *right)
{
    vt_put(vt, r, 0, '|');
    vt_puts_at(vt, r, 1, left == NULL ? "" : left);
    vt_put(vt, r, TUI_SPLIT, '|');
    vt_put(vt, r, TUI_SPLIT + 1, '|');
    vt_puts_at(vt, r, TUI_RIGHT_START, right == NULL ? "" : right);
    vt_put(vt, r, PM_METAL_VT_COLS - 1, '|');
}

static void build_log_line(char *out, size_t cap)
{
    uint8_t tail[48];
    size_t n;
    size_t i;
    size_t o;

    if (out == NULL || cap == 0u) {
        return;
    }
    n = pm_metal_console_copy_tail(tail, sizeof(tail));
    if (n == 0u) {
        str_copy(out, cap, "console live");
        return;
    }
    o = 0;
    for (i = 0; i < n && o + 1u < cap; i++) {
        uint8_t b = tail[i];
        if (b == '\n' || b == '\r' || b == '\t') {
            b = ' ';
        }
        if (b < 0x20u || b > 0x7Eu) {
            b = '?';
        }
        out[o++] = (char)b;
    }
    out[o] = '\0';
}

static void build_system_line(char *out, size_t cap)
{
    if (out == NULL || cap == 0u) {
        return;
    }
    str_copy(out, cap, "metal ");
    str_append(out, cap, METAL_ENGINE);
}

static void append_u8_dec(char *out, size_t cap, uint8_t v)
{
    char tmp[4];
    unsigned n = 0;
    unsigned x = v;

    if (x >= 100u) {
        tmp[n++] = (char)('0' + x / 100u);
        x %= 100u;
        tmp[n++] = (char)('0' + x / 10u);
        x %= 10u;
        tmp[n++] = (char)('0' + x);
    } else if (x >= 10u) {
        tmp[n++] = (char)('0' + x / 10u);
        x %= 10u;
        tmp[n++] = (char)('0' + x);
    } else {
        tmp[n++] = (char)('0' + x);
    }
    tmp[n] = '\0';
    str_append(out, cap, tmp);
}

static void append_ipv4(char *out, size_t cap, uint32_t be)
{
    append_u8_dec(out, cap, (uint8_t)(be >> 24));
    str_append(out, cap, ".");
    append_u8_dec(out, cap, (uint8_t)(be >> 16));
    str_append(out, cap, ".");
    append_u8_dec(out, cap, (uint8_t)(be >> 8));
    str_append(out, cap, ".");
    append_u8_dec(out, cap, (uint8_t)be);
}

static void build_network_line(char *out, size_t cap)
{
    const uint8_t *mac;
    static const char hex[] = "0123456789abcdef";
    size_t i;
    size_t pos;
    uint32_t addr;

    if (out == NULL || cap < 16u) {
        return;
    }
    if (!pm_metal_dev_net_virtio_ready()) {
        str_copy(out, cap, "net: down");
        return;
    }
    addr = pm_metal_ip_ready() ? pm_metal_ip_addr() : 0u;
    if (addr != 0u) {
        str_copy(out, cap, "ip ");
        append_ipv4(out, cap, addr);
        str_append(out, cap, " dhcp");
        return;
    }
    mac = pm_metal_dev_net_virtio_mac();
    if (mac == NULL) {
        str_copy(out, cap, "net: down");
        return;
    }
    pos = 0;
    memcpy(out + pos, "MAC ", 4);
    pos += 4;
    for (i = 0; i < 6u && pos + 3u < cap; i++) {
        if (i > 0u && pos + 1u < cap) {
            out[pos++] = ':';
        }
        out[pos++] = hex[(mac[i] >> 4) & 0x0Fu];
        out[pos++] = hex[mac[i] & 0x0Fu];
    }
    out[pos] = '\0';
}

static void build_memory_line(char *out, size_t cap)
{
    char heap[16];
    char freeb[16];

    if (out == NULL || cap == 0u) {
        return;
    }
    utoa_size(pm_metal_mem_heap_bytes(), heap, sizeof(heap));
    utoa_size(pm_metal_mem_free_bytes(), freeb, sizeof(freeb));
    str_copy(out, cap, "heap ");
    str_append(out, cap, heap);
    str_append(out, cap, " free ");
    str_append(out, cap, freeb);
}

static void paint_dashboard_cells(pm_metal_vt_t *vt)
{
    char log_line[TUI_LEFT_INNER + 1];
    char sys_line[TUI_RIGHT_INNER + 1];
    char net_line[TUI_LEFT_INNER + 1];
    char faces_line[TUI_LEFT_INNER + 1];
    char mem_line[TUI_RIGHT_INNER + 1];
    char footer[PM_METAL_VT_COLS + 1];
    int32_t r;

    if (vt == NULL) {
        return;
    }

    for (r = 0; r < PM_METAL_VT_ROWS; r++) {
        vt_fill_row(vt->cells[r], ' ');
    }

    vt_puts_at(vt, 0, 1, " File  View  Help ");

    vt_box_row(vt, 1);
    build_log_line(log_line, sizeof(log_line));
    build_system_line(sys_line, sizeof(sys_line));
    build_network_line(net_line, sizeof(net_line));
    pm_metal_net_face_format(faces_line, (uint32_t)sizeof(faces_line));
    build_memory_line(mem_line, sizeof(mem_line));

    vt_pane_row(vt, 2, " log", " system");
    vt_pane_row(vt, 3, log_line, sys_line);
    for (r = 4; r <= 10; r++) {
        vt_pane_row(vt, r, "", "");
    }

    vt_box_row(vt, 11);
    vt_pane_row(vt, 12, " network", " memory");
    vt_pane_row(vt, 13, net_line, mem_line);
    vt_pane_row(vt, 14, faces_line, "");
    for (r = 15; r <= 20; r++) {
        vt_pane_row(vt, r, "", "");
    }
    vt_box_row(vt, 21);

    footer[0] = '\0';
    str_append(footer, sizeof(footer), " F7 Dashboard ");
    str_append(footer, sizeof(footer), "metal ");
    str_append(footer, sizeof(footer), METAL_ENGINE);
    str_append(footer, sizeof(footer), "  Ctrl+Q quit");
    vt_puts_at(vt, PM_METAL_VT_ROWS - 1, 0, footer);
}

int32_t pm_metal_tui_init(void)
{
    g_ready = 1;
    return 0;
}

int32_t pm_metal_tui_paint_vt(int32_t vt_index)
{
    pm_metal_vt_t *vt;

    if (!g_ready || !pm_metal_vt_ready()) {
        return -1;
    }
    vt = pm_metal_vt_get(vt_index);
    if (vt == NULL) {
        return -1;
    }
    paint_dashboard_cells(vt);
    return 0;
}

int32_t pm_metal_tui_render_vt(int32_t vt_index)
{
    if (pm_metal_tui_paint_vt(vt_index) != 0) {
        return -1;
    }
    return pm_metal_vt_render(vt_index);
}

int32_t pm_metal_tui_render_draw(pm_metal_draw_surface_t *ds)
{
    pm_metal_vt_t scratch;
    int32_t r;

    if (!g_ready || ds == NULL) {
        return -1;
    }
    memset(&scratch, 0, sizeof(scratch));
    paint_dashboard_cells(&scratch);
    pm_metal_draw_fill(ds, 0xFF000033u);
    for (r = 0; r < PM_METAL_VT_ROWS; r++) {
        int32_t c;
        for (c = 0; c < PM_METAL_VT_COLS; c++) {
            char ch = scratch.cells[r][c];
            if (ch == ' ') {
                continue;
            }
            pm_metal_draw_glyph8(ds, c * 8, r * 8, ch, 0xFFFFFFFFu, 0xFF000033u);
        }
    }
    return 0;
}

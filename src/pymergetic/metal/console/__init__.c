#include "pymergetic/metal/console.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    pm_metal_console_sink_fn sink;
    void *user;
    int32_t console_id;
    int live;
} viewport_t;

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t start;
    size_t len;
    uint64_t seq;
    int ready;
} console_t;

static console_t g_cons[PM_METAL_CONSOLE_MAX];
static viewport_t g_vps[PM_METAL_CONSOLE_VP_MAX];

static void ring_push(console_t *c, const uint8_t *data, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (c->len == c->cap) {
            c->start = (c->start + 1u) % c->cap;
            c->len--;
        }
        c->buf[(c->start + c->len) % c->cap] = data[i];
        c->len++;
        c->seq++;
    }
}

static void replay(console_t *c, pm_metal_console_sink_fn sink, void *user)
{
    size_t i;
    uint8_t chunk[64];
    size_t pos;
    size_t left;

    pos = c->start;
    left = c->len;
    while (left > 0) {
        size_t take = left < sizeof(chunk) ? left : sizeof(chunk);
        for (i = 0; i < take; i++) {
            chunk[i] = c->buf[pos];
            pos = (pos + 1u) % c->cap;
        }
        sink(chunk, take, user);
        left -= take;
    }
}

static void fanout(int32_t id, const uint8_t *data, size_t n)
{
    int i;
    for (i = 0; i < PM_METAL_CONSOLE_VP_MAX; i++) {
        if (g_vps[i].live && g_vps[i].console_id == id && g_vps[i].sink != NULL) {
            g_vps[i].sink(data, n, g_vps[i].user);
        }
    }
}

int32_t pm_metal_console_init(uint8_t *buf, size_t cap)
{
    int i;
    if (buf == NULL || cap < 64u) {
        return -1;
    }
    memset(g_cons, 0, sizeof(g_cons));
    memset(g_vps, 0, sizeof(g_vps));
    for (i = 0; i < PM_METAL_CONSOLE_VP_MAX; i++) {
        g_vps[i].console_id = -1;
    }
    g_cons[0].buf = buf;
    g_cons[0].cap = cap;
    g_cons[0].ready = 1;
    return 0;
}

int32_t pm_metal_console_create(int32_t id, uint8_t *buf, size_t cap)
{
    if (id <= 0 || id >= PM_METAL_CONSOLE_MAX || buf == NULL || cap < 64u) {
        return -1;
    }
    memset(&g_cons[id], 0, sizeof(g_cons[id]));
    g_cons[id].buf = buf;
    g_cons[id].cap = cap;
    g_cons[id].ready = 1;
    return 0;
}

int32_t pm_metal_console_ready(void)
{
    return g_cons[0].ready ? 1 : 0;
}

int32_t pm_metal_console_ready_id(int32_t id)
{
    if (id < 0 || id >= PM_METAL_CONSOLE_MAX) {
        return 0;
    }
    return g_cons[id].ready ? 1 : 0;
}

size_t pm_metal_console_write_id(int32_t id, const uint8_t *data, size_t n)
{
    if (id < 0 || id >= PM_METAL_CONSOLE_MAX || !g_cons[id].ready || data == NULL || n == 0) {
        return 0;
    }
    ring_push(&g_cons[id], data, n);
    fanout(id, data, n);
    return n;
}

size_t pm_metal_console_write(const uint8_t *data, size_t n)
{
    return pm_metal_console_write_id(0, data, n);
}

pm_metal_console_vp_id pm_metal_console_viewport_attach(int32_t console_id,
                                                        pm_metal_console_sink_fn sink, void *user)
{
    int i;
    if (console_id < 0 || console_id >= PM_METAL_CONSOLE_MAX || !g_cons[console_id].ready ||
        sink == NULL) {
        return -1;
    }
    for (i = 0; i < PM_METAL_CONSOLE_VP_MAX; i++) {
        if (!g_vps[i].live) {
            g_vps[i].sink = sink;
            g_vps[i].user = user;
            g_vps[i].console_id = console_id;
            g_vps[i].live = 1;
            replay(&g_cons[console_id], sink, user);
            return (pm_metal_console_vp_id)i;
        }
    }
    return -1;
}

int32_t pm_metal_console_attach(pm_metal_console_sink_fn sink, void *user)
{
    return pm_metal_console_viewport_attach(0, sink, user) >= 0 ? 0 : -1;
}

int32_t pm_metal_console_viewport_rebind(pm_metal_console_vp_id vp, int32_t console_id)
{
    if (vp < 0 || vp >= PM_METAL_CONSOLE_VP_MAX || !g_vps[vp].live) {
        return -1;
    }
    if (console_id < 0 || console_id >= PM_METAL_CONSOLE_MAX || !g_cons[console_id].ready) {
        return -1;
    }
    g_vps[vp].console_id = console_id;
    replay(&g_cons[console_id], g_vps[vp].sink, g_vps[vp].user);
    return 0;
}

void pm_metal_console_viewport_detach(pm_metal_console_vp_id vp)
{
    if (vp < 0 || vp >= PM_METAL_CONSOLE_VP_MAX) {
        return;
    }
    memset(&g_vps[vp], 0, sizeof(g_vps[vp]));
    g_vps[vp].console_id = -1;
}

void pm_metal_console_detach(void)
{
    int i;
    for (i = 0; i < PM_METAL_CONSOLE_VP_MAX; i++) {
        if (g_vps[i].live && g_vps[i].console_id == 0) {
            pm_metal_console_viewport_detach((pm_metal_console_vp_id)i);
        }
    }
}

int32_t pm_metal_console_set_sink(pm_metal_console_sink_fn sink, void *user)
{
    /* Legacy: replace first console #0 viewport or attach. */
    int i;
    if (!g_cons[0].ready || sink == NULL) {
        return -1;
    }
    for (i = 0; i < PM_METAL_CONSOLE_VP_MAX; i++) {
        if (g_vps[i].live && g_vps[i].console_id == 0) {
            g_vps[i].sink = sink;
            g_vps[i].user = user;
            return 0;
        }
    }
    return pm_metal_console_attach(sink, user);
}

uint64_t pm_metal_console_seq(void)
{
    return g_cons[0].seq;
}

size_t pm_metal_console_len(void)
{
    return g_cons[0].len;
}

size_t pm_metal_console_copy_tail(uint8_t *out, size_t cap)
{
    size_t n;
    size_t i;
    size_t pos;
    console_t *c = &g_cons[0];

    if (!c->ready || out == NULL || cap == 0u) {
        return 0;
    }
    n = c->len < cap ? c->len : cap;
    if (n == 0u) {
        return 0;
    }
    pos = (c->start + c->len - n) % c->cap;
    for (i = 0; i < n; i++) {
        out[i] = c->buf[pos];
        pos = (pos + 1u) % c->cap;
    }
    return n;
}

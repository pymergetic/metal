#include "pymergetic/metal/console.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t start; /* index of oldest byte */
    size_t len;
    uint64_t seq;
    pm_metal_console_sink_fn sink;
    void *sink_user;
    int ready;
} console_t;

static console_t g_con;

int32_t pm_metal_console_init(uint8_t *buf, size_t cap)
{
    if (buf == NULL || cap < 64u) {
        return -1;
    }
    memset(&g_con, 0, sizeof(g_con));
    g_con.buf = buf;
    g_con.cap = cap;
    g_con.ready = 1;
    return 0;
}

int32_t pm_metal_console_ready(void)
{
    return g_con.ready ? 1 : 0;
}

static void ring_push(const uint8_t *data, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (g_con.len == g_con.cap) {
            g_con.start = (g_con.start + 1u) % g_con.cap;
            g_con.len--;
        }
        g_con.buf[(g_con.start + g_con.len) % g_con.cap] = data[i];
        g_con.len++;
        g_con.seq++;
    }
}

size_t pm_metal_console_write(const uint8_t *data, size_t n)
{
    if (!g_con.ready || data == NULL || n == 0) {
        return 0;
    }
    ring_push(data, n);
    if (g_con.sink != NULL) {
        g_con.sink(data, n, g_con.sink_user);
    }
    return n;
}

int32_t pm_metal_console_attach(pm_metal_console_sink_fn sink, void *user)
{
    size_t i;
    uint8_t chunk[64];
    size_t pos;
    size_t left;

    if (!g_con.ready || sink == NULL) {
        return -1;
    }
    /* Replay history in order. */
    pos = g_con.start;
    left = g_con.len;
    while (left > 0) {
        size_t take = left < sizeof(chunk) ? left : sizeof(chunk);
        for (i = 0; i < take; i++) {
            chunk[i] = g_con.buf[pos];
            pos = (pos + 1u) % g_con.cap;
        }
        sink(chunk, take, user);
        left -= take;
    }
    g_con.sink = sink;
    g_con.sink_user = user;
    return 0;
}

void pm_metal_console_detach(void)
{
    g_con.sink = NULL;
    g_con.sink_user = NULL;
}

uint64_t pm_metal_console_seq(void)
{
    return g_con.seq;
}

size_t pm_metal_console_len(void)
{
    return g_con.len;
}

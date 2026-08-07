#include "pymergetic/metal/net/http.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/net/tcp.h"

static int32_t g_ready;
static int32_t g_served;

static const char k_resp[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "Connection: close\r\n"
    "\r\n"
    "metal ok\n";

int32_t pm_metal_http_init(void)
{
    g_ready = 1;
    g_served = 0;
    return 0;
}

int32_t pm_metal_http_poll(void)
{
    uint8_t buf[256];
    uint32_t n;
    int32_t rc;

    if (!g_ready || !pm_metal_tcp_established() || g_served) {
        return 0;
    }
    rc = pm_metal_tcp_recv(buf, sizeof(buf) - 1u, &n);
    if (rc != 1 || n < 3u) {
        return 0;
    }
    buf[n] = '\0';
    if (buf[0] != 'G' || buf[1] != 'E' || buf[2] != 'T') {
        return 0;
    }
    if (pm_metal_tcp_send(k_resp, (uint32_t)(sizeof(k_resp) - 1u)) != 0) {
        return -1;
    }
    g_served = 1;
    return 1;
}

int32_t pm_metal_http_served(void)
{
    return g_served;
}

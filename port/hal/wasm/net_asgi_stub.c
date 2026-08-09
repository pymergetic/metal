/*
 * Browser net.asgi — same C ABI; no TCP accept in the browser seat.
 */
#include "pymergetic/metal/net/asgi/__init__.h"

int32_t pm_metal_asgi_init(uint16_t port)
{
    (void)port;
    return -1;
}

int32_t pm_metal_asgi_init_tls(uint16_t port)
{
    (void)port;
    return -1;
}

void pm_metal_asgi_release(void) {}

int32_t pm_metal_asgi_poll(void)
{
    return 0;
}

int32_t pm_metal_asgi_ready(void)
{
    return 0;
}

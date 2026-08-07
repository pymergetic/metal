#include "live_http.h"

#include "pymergetic/metal/net/http.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/tcp.h"

void uart_puts(const char *s);

void pm_metal_live_http(void)
{
    if (pm_metal_net_ip_tcp_listen(80) != 0) {
        uart_puts("live listen fail\n");
        return;
    }
    (void)pm_metal_http_init();
    uart_puts("live http\n");
    for (;;) {
        pm_metal_net_ip_poll();
        (void)pm_metal_http_poll();
    }
}

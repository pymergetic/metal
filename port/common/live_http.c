#include "live_http.h"

#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/net/http.h"
#include "pymergetic/metal/net/ip/tcp.h"
#include "pymergetic/metal/net/pump.h"

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
        if (pm_metal_async_ready()) {
            (void)pm_metal_async_run_poll();
        } else {
            pm_metal_net_pump_once();
        }
        (void)pm_metal_http_poll();
    }
}

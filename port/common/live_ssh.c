#include "live_ssh.h"

#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/net/ip/tcp.h"
#include "pymergetic/metal/net/pump/__init__.h"
#include "pymergetic/metal/net/ssh/__init__.h"

void uart_puts(const char *s);

void pm_metal_live_ssh(void)
{
    if (pm_metal_net_ssh_autoload() != 0) {
        uart_puts("live ssh init fail\n");
        return;
    }
    pm_metal_net_ssh_banner_reset();
    if (pm_metal_net_ssh_listen(22) == 0u) {
        uart_puts("live ssh listen fail\n");
        return;
    }
    uart_puts("live ssh\n");
    for (;;) {
        if (pm_metal_async_ready()) {
            (void)pm_metal_async_run_poll();
        } else {
            pm_metal_net_pump_once();
        }
        pm_metal_net_ip_tcp_focus_passive();
        if (!pm_metal_net_ip_tcp_passive_established()) {
            continue;
        }
        {
            int32_t rc = pm_metal_net_ssh_poll();
            if (pm_metal_net_ssh_served() || rc < 0) {
                /* KEX floor done (or error); auth/encrypt TODO. */
                pm_metal_net_ssh_banner_reset();
                (void)pm_metal_net_ip_tcp_passive_relisten(22);
            }
        }
    }
}

#include "live_ssh.h"

#include <pymergetic/metal/net/ip/__init__.h>
#include <pymergetic/metal/net/ssh/__init__.h>
#include <pymergetic/metal/net/ip/tcp.h>

void uart_puts(const char *s);

void pm_metal_live_ssh(void)
{
    pm_metal_net_ssh_banner_reset();
    if (pm_metal_net_ip_tcp_listen(22) != 0) {
        uart_puts("live ssh listen fail\n");
        return;
    }
    uart_puts("live ssh\n");
    for (;;) {
        pm_metal_net_ip_poll();
        pm_metal_net_ip_tcp_focus_passive();
        if (!pm_metal_net_ip_tcp_passive_established()) {
            continue;
        }
        if (pm_metal_net_ip_tcp_established()) {
            (void)pm_metal_net_ssh_banner_send();
        }
    }
}

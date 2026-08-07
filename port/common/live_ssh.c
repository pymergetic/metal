#include "live_ssh.h"

#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ssh.h"
#include "pymergetic/metal/net/tcp.h"

void uart_puts(const char *s);

void pm_metal_live_ssh(void)
{
    pm_metal_ssh_banner_reset();
    if (pm_metal_tcp_listen(22) != 0) {
        uart_puts("live ssh listen fail\n");
        return;
    }
    uart_puts("live ssh\n");
    for (;;) {
        pm_metal_ip_poll();
        if (pm_metal_tcp_established()) {
            (void)pm_metal_ssh_banner_send();
        }
    }
}

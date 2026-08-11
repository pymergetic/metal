#include "live_ssh.h"

#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/net/ssh/__init__.h"

void uart_puts(const char *s);

void pm_metal_live_ssh(void)
{
    /* Boot already auto-starts sshd on FW; this lab loop only binds if still down. */
    if (pm_metal_net_ssh_listen_port() == 0u) {
        if (pm_metal_net_ssh_init() != 0) {
            uart_puts("live ssh init fail\n");
            return;
        }
        pm_metal_net_ssh_banner_reset();
        if (pm_metal_net_ssh_listen(22) == 0u) {
            uart_puts("live ssh listen fail\n");
            return;
        }
    }
    uart_puts("live ssh\n");
    for (;;) {
        (void)pm_metal_async_run_poll();
        if (pm_metal_net_ssh_served()) {
            pm_metal_net_ssh_banner_reset();
        }
    }
}

/*
 * Co-located reg seat test for net.ssh (registered via weak symbol).
 */
#include <pymergetic/metal/net/ssh/__init__.h>

extern void uart_puts(const char *s);

int32_t pm_metal_net_ssh_seat_test(void)
{
    if (!pm_metal_net_ssh_available()) {
        return -1;
    }
    if (pm_metal_net_ssh_init() != 0) {
        return -1;
    }
    uart_puts("ssh ok\n");
    return 0;
}

#include "ip_smoke.h"

#include <stdint.h>

#include "pymergetic/metal/net/ip.h"

void uart_puts(const char *s);

int pm_metal_ip_smoke(void)
{
    int i;

    if (pm_metal_ip_init(PM_METAL_IP_DEFAULT_ADDR, PM_METAL_IP_DEFAULT_MASK,
                         PM_METAL_IP_DEFAULT_GW) != 0) {
        uart_puts("ip init fail\n");
        return -1;
    }
    if (!pm_metal_ip_ready()) {
        uart_puts("ip ready fail\n");
        return -1;
    }
    if (pm_metal_ip_announce() != 0) {
        uart_puts("ip announce fail\n");
        return -1;
    }
    for (i = 0; i < 64; i++) {
        pm_metal_ip_poll();
    }
    uart_puts("ip ok\n");
    return 0;
}

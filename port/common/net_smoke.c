#include "net_smoke.h"

#include <stddef.h>

#include "pymergetic/metal/dev/net.h"

void uart_puts(const char *s);

int pm_metal_net_smoke(void)
{
    if (pm_metal_dev_net_virtio_probe(NULL) != 0) {
        uart_puts("net probe fail\n");
        return -1;
    }
    uart_puts("net ok\n");
    return 0;
}

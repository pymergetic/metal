/*
 * Browser dev.net.bge — same C ABI as bge_netif.h (glue face); no PCI NIC.
 */
#include "pymergetic/metal/dev/net/bge/bge_netif.h"

#include <stddef.h>

int pm_metal_bge_netif_detect(void) { return -1; }

int pm_metal_bge_netif_open(uint8_t mac_out[6])
{
    if (mac_out) {
        mac_out[0] = mac_out[1] = mac_out[2] = mac_out[3] = mac_out[4] = mac_out[5] = 0;
    }
    return -1;
}

int pm_metal_bge_netif_ready(void) { return 0; }

const uint8_t *pm_metal_bge_netif_mac(void) { return NULL; }

int pm_metal_bge_netif_tx(const void *frame, uint32_t len)
{
    (void)frame;
    (void)len;
    return -1;
}

void pm_metal_bge_netif_poll(pm_metal_bge_netif_rx_fn on_frame, void *ctx)
{
    (void)on_frame;
    (void)ctx;
}

/*
 * Browser dev.net.virtio_net — same C ABI as dev/net.h (glue face); no virtio.
 */
#include "pymergetic/metal/dev/net.h"

#include <stddef.h>

int pm_metal_dev_net_virtio_probe(uint8_t mac_out[6])
{
    if (mac_out) {
        mac_out[0] = mac_out[1] = mac_out[2] = mac_out[3] = mac_out[4] = mac_out[5] = 0;
    }
    return -1;
}

int pm_metal_dev_net_virtio_open(uint8_t mac_out[6])
{
    if (mac_out) {
        mac_out[0] = mac_out[1] = mac_out[2] = mac_out[3] = mac_out[4] = mac_out[5] = 0;
    }
    return -1;
}

int pm_metal_dev_net_virtio_ready(void) { return 0; }

const uint8_t *pm_metal_dev_net_virtio_mac(void) { return NULL; }

int pm_metal_dev_net_virtio_tx(const void *frame, uint32_t len)
{
    (void)frame;
    (void)len;
    return -1;
}

void pm_metal_dev_net_virtio_poll(pm_metal_dev_net_virtio_rx_fn on_frame, void *ctx)
{
    (void)on_frame;
    (void)ctx;
}

int pm_metal_dev_net_virtio_reap_tx(void) { return 0; }

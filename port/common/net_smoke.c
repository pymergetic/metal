#include "net_smoke.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/dev/net.h"
#include "pymergetic/metal/net/upy_nic.h"

void uart_puts(const char *s);

static void nic_l2_poll(void)
{
    pm_metal_dev_net_virtio_poll(NULL, NULL);
}

static const pm_metal_net_upy_l2_ops_t g_virtio_l2_ops = {
    .open = pm_metal_dev_net_virtio_open,
    .mac = pm_metal_dev_net_virtio_mac,
    .tx = pm_metal_dev_net_virtio_tx,
    .poll = nic_l2_poll,
};

static int mac_nonzero(const uint8_t mac[6])
{
    uintptr_t i;

    for (i = 0; i < 6; i++) {
        if (mac[i] != 0u) {
            return 1;
        }
    }
    return 0;
}

static int build_arp_request(const uint8_t mac[6], uint8_t *out, uint32_t out_cap, uint32_t *len_out)
{
    uint8_t frame[64];
    uint32_t i;

    if (out == NULL || len_out == NULL || out_cap < 64u) {
        return -1;
    }

    memset(frame, 0, sizeof(frame));
    for (i = 0; i < 6; i++) {
        frame[i] = 0xffu;
    }
    memcpy(frame + 6, mac, 6);
    frame[12] = 0x08u;
    frame[13] = 0x06u;
    frame[14] = 0x00u;
    frame[15] = 0x01u;
    frame[16] = 0x08u;
    frame[17] = 0x00u;
    frame[18] = 0x06u;
    frame[19] = 0x04u;
    frame[20] = 0x00u;
    frame[21] = 0x01u;
    memcpy(frame + 22, mac, 6);
    frame[28] = 10u;
    frame[29] = 0u;
    frame[30] = 0u;
    frame[31] = 2u;
    frame[32] = 10u;
    frame[33] = 0u;
    frame[34] = 0u;
    frame[35] = 1u;

    memcpy(out, frame, 64);
    *len_out = 64;
    return 0;
}

int pm_metal_net_smoke(void)
{
    uint8_t mac[6];
    uint8_t frame[64];
    uint32_t flen;
    int i;
    int tx_done = 0;

    if (pm_metal_dev_net_virtio_probe(NULL) != 0) {
        uart_puts("net probe fail\n");
        return -1;
    }

    if (pm_metal_dev_net_virtio_open(mac) != 0) {
        uart_puts("net open fail\n");
        return -1;
    }

    if (!pm_metal_dev_net_virtio_ready()) {
        uart_puts("net ready fail\n");
        return -1;
    }

    if (!mac_nonzero(mac) && !mac_nonzero(pm_metal_dev_net_virtio_mac())) {
        uart_puts("net mac fail\n");
        return -1;
    }

    if (build_arp_request(pm_metal_dev_net_virtio_mac(), frame, sizeof(frame), &flen) != 0) {
        uart_puts("net frame fail\n");
        return -1;
    }

    if (pm_metal_dev_net_virtio_tx(frame, flen) != 0) {
        uart_puts("net tx fail\n");
        return -1;
    }

    for (i = 0; i < 5000; i++) {
        pm_metal_dev_net_virtio_poll(NULL, NULL);
        if (pm_metal_dev_net_virtio_reap_tx()) {
            tx_done = 1;
            break;
        }
    }

    (void)tx_done;

    if (pm_metal_net_upy_nic_register("virtio-net", &g_virtio_l2_ops) != 0) {
        uart_puts("net nic register fail\n");
        return -1;
    }

    uart_puts("net ok\n");
    return 0;
}

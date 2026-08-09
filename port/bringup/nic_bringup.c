#include "nic_bringup.h"

#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/dev/net.h"
#include "pymergetic/metal/dev/net/bge/bge_netif.h"
#include "pymergetic/metal/net/ip/lwip_start.h"
#include "pymergetic/metal/net/nic/__init__.h"

static void virtio_poll(void)
{
    pm_metal_dev_net_virtio_poll(NULL, NULL);
}

static void bge_poll(void)
{
    pm_metal_bge_netif_poll(NULL, NULL);
}

static const pm_metal_net_nic_l2_ops_t g_virtio_l2 = {
    .open = pm_metal_dev_net_virtio_open,
    .mac = pm_metal_dev_net_virtio_mac,
    .tx = pm_metal_dev_net_virtio_tx,
    .poll = virtio_poll,
};

static const pm_metal_net_nic_l2_ops_t g_bge_l2 = {
    .open = pm_metal_bge_netif_open,
    .mac = pm_metal_bge_netif_mac,
    .tx = pm_metal_bge_netif_tx,
    .poll = bge_poll,
};

int pm_metal_nic_bringup(void)
{
    int got = 0;

    (void)pm_metal_net_ip_loopback_start();

    if (pm_metal_dev_net_virtio_probe(NULL) == 0) {
        if (pm_metal_net_ip_virtio_start() == 0) {
            (void)pm_metal_net_nic_register("virtio-net", &g_virtio_l2);
            pm_metal_boot_tree_item("nic", PM_METAL_BOOT_TREE_OK, "virtio-net");
            got = 1;
        }
    }

    if (pm_metal_bge_netif_detect() == 0) {
        if (pm_metal_net_ip_bge_start() == 0) {
            (void)pm_metal_net_nic_register("bge", &g_bge_l2);
            pm_metal_boot_tree_item(got ? "nic1" : "nic", PM_METAL_BOOT_TREE_OK, "bge");
            got = 1;
        }
    }

    if (!got) {
        pm_metal_boot_tree_item("nic", PM_METAL_BOOT_TREE_FAIL, "none");
        return -1;
    }
    pm_metal_boot_tree_item("ip", PM_METAL_BOOT_TREE_OK, "lwip");
    return 0;
}

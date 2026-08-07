#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/dev/net.h"

int pm_metal_dev_net_virtio_probe(uint8_t mac_out[6])
{
    pm_metal_virtio_dev_t dev;
    uint64_t feats;
    uint8_t mac[6];
    int mac_ok;
    uintptr_t i;

    if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_NET, &dev) != 0 &&
        pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_NET_LEGACY, &dev) != 0) {
        return -1;
    }

    feats = pm_metal_virtio_get_features(&dev);
    feats &= PM_METAL_VIRTIO_F_VERSION_1 | PM_METAL_VIRTIO_F_MAC;
    if (pm_metal_virtio_set_features(&dev, feats) != 0) {
        pm_metal_virtio_set_status(&dev, 0);
        pm_metal_virtio_set_status(&dev, (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));
        if (pm_metal_virtio_set_features(&dev, PM_METAL_VIRTIO_F_MAC) != 0) {
            pm_metal_virtio_close(&dev);
            return -1;
        }
        feats = PM_METAL_VIRTIO_F_MAC;
    }

    mac_ok = 0;
    if ((feats & PM_METAL_VIRTIO_F_MAC) != 0 &&
        pm_metal_virtio_cfg_read(&dev, 0, mac, 6) == 0 &&
        (mac[0] & 0x01u) == 0u) {
        for (i = 0; i < 6; i++) {
            if (mac[i] != 0u) {
                mac_ok = 1;
                break;
            }
        }
    }

    if (mac_out != NULL) {
        if (mac_ok) {
            memcpy(mac_out, mac, 6);
        } else {
            memset(mac_out, 0, 6);
        }
    }

    pm_metal_virtio_close(&dev);
    return 0;
}

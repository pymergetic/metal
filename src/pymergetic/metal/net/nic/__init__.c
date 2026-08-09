/* Metal L2 NIC face — slim registry for bring-up / µPy network.LAN. */

#include "pymergetic/metal/net/nic/__init__.h"

#include <stddef.h>

static const pm_metal_net_nic_l2_ops_t *s_ops;
static const char *s_name;

int32_t pm_metal_net_nic_register(const char *name, const pm_metal_net_nic_l2_ops_t *ops)
{
    if (name == NULL || ops == NULL) {
        return -1;
    }
    if (ops->open == NULL || ops->mac == NULL || ops->tx == NULL || ops->poll == NULL) {
        return -1;
    }
    s_name = name;
    s_ops = ops;
    return 0;
}

const pm_metal_net_nic_l2_ops_t *pm_metal_net_nic_ops(void)
{
    return s_ops;
}

const char *pm_metal_net_nic_name(void)
{
    return s_name;
}

int32_t __attribute__((weak)) pm_metal_net_nic_attach_upy(void)
{
    return s_ops != NULL ? 0 : -1;
}

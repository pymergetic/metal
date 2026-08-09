/* Metal L2 NIC face — slim registry for bring-up / µPy network.LAN. */

#include "pymergetic/metal/net/nic/__init__.h"

#include <stddef.h>

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_net_nic_reg_load. */
static pm_metal_reg_export_t net_nic_exports[] = {
    PM_METAL_REG_EXPORT(register),
    PM_METAL_REG_EXPORT(attach_upy),
};
PM_METAL_REG_REF(net_nic, register, 0);
PM_METAL_REG_REF(net_nic, attach_upy, 1);
PM_METAL_REG_MOD(net_nic, "pymergetic.metal.net.nic")

static int32_t net_nic_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(net_nic_register, (void *)pm_metal_net_nic_register);
    pm_metal_reg_export_publish(net_nic_attach_upy, (void *)pm_metal_net_nic_attach_upy);
    return 0;
}

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

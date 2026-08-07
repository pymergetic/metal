/* Metal L2 → AbstractNIC adapter border (constellation). */

#ifndef PM_METAL_NET_UPY_NIC_H_
#define PM_METAL_NET_UPY_NIC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_net_upy_l2_ops {
    int (*open)(uint8_t mac_out[6]);
    const uint8_t *(*mac)(void);
    int (*tx)(const void *frame, uint32_t len);
    void (*poll)(void);
} pm_metal_net_upy_l2_ops_t;

/* Record L2 ops for µPy NIC hand-off. Returns 0 on success, <0 on error. */
int32_t pm_metal_net_upy_nic_register(const char *name, const pm_metal_net_upy_l2_ops_t *ops);

const pm_metal_net_upy_l2_ops_t *pm_metal_net_upy_nic_ops(void);
const char *pm_metal_net_upy_nic_name(void);

/* Attach into µPy mod_network when NETWORK is enabled (weak no-op otherwise). */
int32_t pm_metal_net_upy_nic_attach_upy(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_NET_UPY_NIC_H_ */

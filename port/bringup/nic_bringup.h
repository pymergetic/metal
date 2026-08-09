#ifndef PM_METAL_NIC_BRINGUP_H_
#define PM_METAL_NIC_BRINGUP_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Probe virtio-net then bge; register first live L2. 0 = ok. */
int pm_metal_nic_bringup(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef PM_METAL_PRODUCT_BRINGUP_H_
#define PM_METAL_PRODUCT_BRINGUP_H_

/*
 * Lean product init for REPL / live faces — not the CI smoke battery.
 * Console + floor + virtio-net + DHCP/IP. Returns 0 on success.
 */
int pm_metal_product_bringup(void);

#endif

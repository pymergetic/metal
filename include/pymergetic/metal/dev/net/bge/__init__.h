#ifndef PYMERGETIC_METAL_DEV_NET_BGE_H_
#define PYMERGETIC_METAL_DEV_NET_BGE_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_bge_netif_detect(void);
int32_t pm_metal_bge_netif_open(void);
int32_t pm_metal_bge_netif_ready(void);
int32_t pm_metal_bge_netif_mac(void);
int32_t pm_metal_bge_netif_tx(void);
int32_t pm_metal_bge_netif_poll(void);
#ifdef __cplusplus
}
#endif
#endif /* PYMERGETIC_METAL_DEV_NET_BGE_H_ */

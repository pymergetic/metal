#ifndef PYMERGETIC_METAL_PACK_H_
#define PYMERGETIC_METAL_PACK_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
const uint8_t *pm_metal_pack_inspect(void);
uint32_t pm_metal_pack_inspect_len(void);
const uint8_t *pm_metal_pack_metal(void);
uint32_t pm_metal_pack_metal_len(void);
int32_t pm_metal_mod_packs_mount_all(void);
int32_t pm_metal_pack_names(void);
#ifdef __cplusplus
}
#endif
#endif

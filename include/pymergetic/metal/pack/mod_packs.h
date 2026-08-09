#ifndef PM_METAL_MOD_PACKS_H_
#define PM_METAL_MOD_PACKS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build-generated MPWP pack blobs (metal modules only). */

extern const uint8_t pm_metal_pack_inspect[];
extern const unsigned pm_metal_pack_inspect_len;

extern const uint8_t pm_metal_pack_metal[];
extern const unsigned pm_metal_pack_metal_len;

/* Mount metal product packs at /mods/<name>. Returns 0 ok.
 * Does not mount pymergetic.wasmmod — that is wasmmod's own pack. */
int32_t pm_metal_mod_packs_mount_all(void);

/** Load pymergetic.metal.pack RegMod (idempotent). */
int32_t pm_metal_pack_reg_load(void);

#ifdef __cplusplus
}
#endif

#endif

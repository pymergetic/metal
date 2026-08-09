#include "pymergetic/metal/pack/mod_packs.h"

#include <stddef.h>

#include "pymergetic/metal/fs.h"

int32_t pm_metal_mod_packs_mount_all(void)
{
    if (pm_metal_fs_wasmmod_mount_mpwp(NULL, pm_metal_pack_metal,
                                       pm_metal_pack_metal_len) != 0) {
        return -1;
    }
    if (pm_metal_fs_wasmmod_mount_mpwp(NULL, pm_metal_pack_inspect,
                                       pm_metal_pack_inspect_len) != 0) {
        return -1;
    }
    return 0;
}

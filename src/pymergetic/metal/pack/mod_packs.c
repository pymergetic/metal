#include "pymergetic/metal/pack/mod_packs.h"

#include <stddef.h>

#include "pymergetic/metal/fs.h"
#include <pymergetic/metal/reg/mod.h>

static pm_metal_reg_export_t pack_exports[] = {
    PM_METAL_REG_EXPORT(mount_all),
};
PM_METAL_REG_REF(pack, mount_all, 0);
PM_METAL_REG_MOD(pack, "pymergetic.metal.pack")

static int32_t pack_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(pack_mount_all, (void *)pm_metal_mod_packs_mount_all);
    return 0;
}

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

/** @file
  Doom package — name + extra assets only. Guest AOT/wasm is framework convention.
**/
#include <pymergetic/metal/guest/pkg/pkg.h>
#include <pymergetic/metal/fs/esp/esp.h>

#include <stdint.h>

static const pm_metal_pkg_asset_t mDoomAssets[] = {
  { "doom1.wad", 8u * 1024u * 1024u },
};

static int32_t DoomEspExists(const char *path)
{
  uint32_t sz;

  return (pm_metal_esp_file_size(path, &sz) == 0) ? 1 : 0;
}

static int DoomReady(void)
{
  if (!DoomEspExists("mods/apps/doom/doom1.wad")) {
    return 0;
  }

  return pm_metal_pkg_guest_ready("doom");
}

static const pm_metal_pkg_t mDoomPkg = {
  "doom", mDoomAssets, (uint32_t)(sizeof(mDoomAssets) / sizeof(mDoomAssets[0])), DoomReady
};

void pm_metal_pkg_doom_register(void)
{
  (void)pm_metal_pkg_register(&mDoomPkg);
}

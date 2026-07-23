/** @file
  Doom package — name + extra assets only. Guest AOT/wasm is framework convention.
**/
#include <pymergetic/metal/guest/pkg/pkg.h>
#include <pymergetic/metal/fs/esp/esp.h>

#include <Uefi.h>

STATIC CONST pm_metal_pkg_asset_t  mDoomAssets[] = {
  { "doom1.wad", 8u * 1024u * 1024u },
};

STATIC
INT32
DoomEspExists (
  CONST CHAR8  *path
  )
{
  UINT32  sz;

  return (pm_metal_esp_file_size (path, &sz) == 0) ? 1 : 0;
}

STATIC
INT32
DoomReady (
  VOID
  )
{
  if (!DoomEspExists ("mods/apps/doom/doom1.wad")) {
    return 0;
  }

  return pm_metal_pkg_guest_ready ("doom");
}

STATIC CONST pm_metal_pkg_t  mDoomPkg = {
  "doom",
  mDoomAssets,
  (UINT32)(sizeof (mDoomAssets) / sizeof (mDoomAssets[0])),
  DoomReady
};

VOID
pm_metal_pkg_doom_register (
  VOID
  )
{
  (VOID)pm_metal_pkg_register (&mDoomPkg);
}

/** @file
  Package registry — standard guest paths by host arch; assets from pkg.
**/
#include <pymergetic/metal/guest/pkg/pkg.h>
#include <pymergetic/metal/dev/net/net_life.h>
#include <pymergetic/metal/fs/esp/esp.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#ifndef PM_METAL_PKG_MAX
#define PM_METAL_PKG_MAX  8u
#endif

#ifndef PM_METAL_PKG_SEED_MAX
#define PM_METAL_PKG_SEED_MAX  16u
#endif

#ifndef PM_METAL_PKG_AOT_CAP
#define PM_METAL_PKG_AOT_CAP   (4u * 1024u * 1024u)
#endif

#ifndef PM_METAL_PKG_WASM_CAP
#define PM_METAL_PKG_WASM_CAP  (2u * 1024u * 1024u)
#endif

#ifndef PM_METAL_PKG_SIG_CAP
#define PM_METAL_PKG_SIG_CAP   512u
#endif

STATIC CONST pm_metal_pkg_t  *mPkgs[PM_METAL_PKG_MAX];
STATIC UINT32                 mPkgN;
STATIC UINT8                  mInited;

/* Scratch for dynamic seed plan (paths + slots). */
STATIC CHAR8                  mSeedPath[PM_METAL_PKG_SEED_MAX][96];
STATIC pm_metal_pkg_file_t    mSeed[PM_METAL_PKG_SEED_MAX];
STATIC UINT32                 mSeedN;

VOID  pm_metal_pkg_doom_register (
  VOID
  );

CONST CHAR8 *
pm_metal_host_aot_arch (
  VOID
  )
{
#if defined(BUILD_TARGET_X86_32)
  return "i386";
#elif defined(BUILD_TARGET_X86_64)
  return "x86_64";
#elif defined(BUILD_TARGET_AARCH64)
  return "aarch64";
#elif defined(BUILD_TARGET_ARM) || defined(BUILD_TARGET_THUMB)
  return "arm";
#elif defined(BUILD_TARGET_RISCV64)
  return "riscv64";
#elif defined(BUILD_TARGET_RISCV32)
  return "riscv32";
#else
#error "pm_metal_host_aot_arch: unknown BUILD_TARGET_*"
#endif
}

STATIC
INT32
PkgEspExists (
  CONST CHAR8  *path
  )
{
  UINT32  sz;

  return (pm_metal_esp_file_size (path, &sz) == 0) ? 1 : 0;
}

STATIC
VOID
PkgPathGuest (
  CHAR8        *out,
  UINTN         cap,
  CONST CHAR8  *name,
  CONST CHAR8  *ext
  )
{
  AsciiSPrint (out, cap, "mods/apps/%a/%a.%a", name, name, ext);
}

INT32
pm_metal_pkg_guest_ready (
  CONST CHAR8  *name
  )
{
  CHAR8  path[96];

  if (name == NULL || name[0] == '\0') {
    return 0;
  }

  AsciiSPrint (
    path,
    sizeof (path),
    "mods/apps/%a/%a.%a.aot",
    name,
    name,
    pm_metal_host_aot_arch ()
    );
  if (PkgEspExists (path)) {
    return 1;
  }

  PkgPathGuest (path, sizeof (path), name, "wasm");
  return PkgEspExists (path);
}

STATIC
VOID
PkgSeedAdd (
  CONST CHAR8  *path,
  UINT32        cap
  )
{
  UINT32  i;

  if (mSeedN >= PM_METAL_PKG_SEED_MAX || path == NULL) {
    return;
  }

  i = mSeedN;
  AsciiStrCpyS (mSeedPath[i], sizeof (mSeedPath[i]), path);
  mSeed[i].esp_path = mSeedPath[i];
  mSeed[i].url_path = mSeedPath[i];
  mSeed[i].cap      = cap;
  mSeedN++;
}

STATIC
VOID
PkgSeedBuild (
  CONST pm_metal_pkg_t  *pkg
  )
{
  CHAR8         path[96];
  CHAR8         sig[112];
  CONST CHAR8  *arch;
  UINT32        i;

  mSeedN = 0;
  if (pkg == NULL || pkg->name == NULL) {
    return;
  }

  arch = pm_metal_host_aot_arch ();

  /* Standard guest entry: this host's AOT, then wasm. No other arches. */
  AsciiSPrint (
    path,
    sizeof (path),
    "mods/apps/%a/%a.%a.aot",
    pkg->name,
    pkg->name,
    arch
    );
  PkgSeedAdd (path, PM_METAL_PKG_AOT_CAP);
  AsciiSPrint (sig, sizeof (sig), "%a.sig", path);
  PkgSeedAdd (sig, PM_METAL_PKG_SIG_CAP);

  PkgPathGuest (path, sizeof (path), pkg->name, "wasm");
  PkgSeedAdd (path, PM_METAL_PKG_WASM_CAP);
  AsciiSPrint (sig, sizeof (sig), "%a.sig", path);
  PkgSeedAdd (sig, PM_METAL_PKG_SIG_CAP);

  for (i = 0; i < pkg->nassets; i++) {
    if (pkg->assets[i].name == NULL || pkg->assets[i].name[0] == '\0') {
      continue;
    }

    AsciiSPrint (
      path,
      sizeof (path),
      "mods/apps/%a/%a",
      pkg->name,
      pkg->assets[i].name
      );
    PkgSeedAdd (path, pkg->assets[i].cap);
  }
}

VOID
pm_metal_pkg_init (
  VOID
  )
{
  if (mInited != 0u) {
    return;
  }

  mInited = 1;
  mPkgN   = 0;
  pm_metal_pkg_doom_register ();
}

INT32
pm_metal_pkg_register (
  CONST pm_metal_pkg_t  *pkg
  )
{
  UINT32  i;

  if (pkg == NULL || pkg->name == NULL || pkg->name[0] == '\0') {
    return -1;
  }

  pm_metal_pkg_init ();

  for (i = 0; i < mPkgN; i++) {
    if (AsciiStrCmp (mPkgs[i]->name, pkg->name) == 0) {
      return -1;
    }
  }

  if (mPkgN >= PM_METAL_PKG_MAX) {
    return -1;
  }

  mPkgs[mPkgN++] = pkg;
  return 0;
}

CONST pm_metal_pkg_t *
pm_metal_pkg_lookup (
  CONST CHAR8  *name
  )
{
  UINT32  i;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  pm_metal_pkg_init ();

  for (i = 0; i < mPkgN; i++) {
    if (AsciiStrCmp (mPkgs[i]->name, name) == 0) {
      return mPkgs[i];
    }
  }

  return NULL;
}

INT32
pm_metal_pkg_ready (
  CONST CHAR8  *name
  )
{
  CONST pm_metal_pkg_t  *pkg;

  pkg = pm_metal_pkg_lookup (name);
  if (pkg == NULL) {
    return 0;
  }

  if (pkg->ready != NULL) {
    return pkg->ready () ? 1 : 0;
  }

  return pm_metal_pkg_guest_ready (name);
}

CONST pm_metal_pkg_file_t *
pm_metal_pkg_files (
  CONST CHAR8  *name,
  UINT32       *out_n
  )
{
  CONST pm_metal_pkg_t  *pkg;

  pkg = pm_metal_pkg_lookup (name);
  if (pkg == NULL) {
    mSeedN = 0;
    if (out_n != NULL) {
      *out_n = 0;
    }

    return NULL;
  }

  PkgSeedBuild (pkg);
  if (out_n != NULL) {
    *out_n = mSeedN;
  }

  return (mSeedN > 0u) ? mSeed : NULL;
}

INT32
pm_metal_pkg_file_optional (
  CONST CHAR8                *name,
  CONST pm_metal_pkg_file_t  *f
  )
{
  if (name == NULL || f == NULL || f->esp_path == NULL) {
    return 0;
  }

  /* Sigs never block readiness. */
  if (AsciiStrStr (f->esp_path, ".sig") != NULL) {
    return 1;
  }

  /* Guest slots: skip once host AOT or wasm is already cached. */
  if (AsciiStrStr (f->esp_path, ".aot") != NULL
      || AsciiStrStr (f->esp_path, ".wasm") != NULL)
  {
    return pm_metal_pkg_guest_ready (name);
  }

  return 0;
}

INT32
pm_metal_pkg_ensure (
  CONST CHAR8  *name
  )
{
  if (pm_metal_pkg_lookup (name) == NULL) {
    return 0;
  }

  if (pm_metal_pkg_ready (name)) {
    return 0;
  }

  return pm_metal_net_life_seed_ensure (name);
}

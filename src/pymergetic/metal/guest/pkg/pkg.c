/** @file
  Package registry — standard guest paths by host arch; assets from pkg.
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/guest/pkg/pkg.h>
#include <pymergetic/metal/dev/net/net_life.h>
#include <pymergetic/metal/fs/esp/esp.h>

#ifndef PM_METAL_PKG_MAX
#define PM_METAL_PKG_MAX 8u
#endif

#ifndef PM_METAL_PKG_SEED_MAX
#define PM_METAL_PKG_SEED_MAX 16u
#endif

#ifndef PM_METAL_PKG_AOT_CAP
#define PM_METAL_PKG_AOT_CAP (4u * 1024u * 1024u)
#endif

#ifndef PM_METAL_PKG_WASM_CAP
#define PM_METAL_PKG_WASM_CAP (2u * 1024u * 1024u)
#endif

#ifndef PM_METAL_PKG_SIG_CAP
#define PM_METAL_PKG_SIG_CAP 512u
#endif

static const pm_metal_pkg_t *mPkgs[PM_METAL_PKG_MAX];
static uint32_t              mPkgN;
static uint8_t               mInited;

/* Scratch for dynamic seed plan (paths + slots). */
static char                mSeedPath[PM_METAL_PKG_SEED_MAX][96];
static pm_metal_pkg_file_t mSeed[PM_METAL_PKG_SEED_MAX];
static uint32_t            mSeedN;

void pm_metal_pkg_doom_register(void);

const char *pm_metal_host_aot_arch(void)
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

static int32_t PkgEspExists(const char *path)
{
  uint32_t sz;

  return (pm_metal_esp_file_size(path, &sz) == 0) ? 1 : 0;
}

static void PkgPathGuest(char *out, uintptr_t cap, const char *name, const char *ext)
{
  snprintf(out, cap, "mods/apps/%s/%s.%s", name, name, ext);
}

int32_t pm_metal_pkg_guest_ready(const char *name)
{
  char path[96];

  if (name == NULL || name[0] == '\0') {
    return 0;
  }

  snprintf(path, sizeof(path), "mods/apps/%s/%s.%s.aot", name, name, pm_metal_host_aot_arch());
  if (PkgEspExists(path)) {
    return 1;
  }

  PkgPathGuest(path, sizeof(path), name, "wasm");
  return PkgEspExists(path);
}

static void PkgSeedAdd(const char *path, uint32_t cap)
{
  uint32_t i;

  if (mSeedN >= PM_METAL_PKG_SEED_MAX || path == NULL) {
    return;
  }

  i = mSeedN;
  snprintf(mSeedPath[i], sizeof(mSeedPath[i]), "%s", path);
  mSeed[i].esp_path = mSeedPath[i];
  mSeed[i].url_path = mSeedPath[i];
  mSeed[i].cap      = cap;
  mSeedN++;
}

static void PkgSeedBuild(const pm_metal_pkg_t *pkg)
{
  char        path[96];
  char        sig[112];
  const char *arch;
  uint32_t    i;

  mSeedN = 0;
  if (pkg == NULL || pkg->name == NULL) {
    return;
  }

  arch = pm_metal_host_aot_arch();

  /* Standard guest entry: this host's AOT, then wasm. No other arches. */
  snprintf(path, sizeof(path), "mods/apps/%s/%s.%s.aot", pkg->name, pkg->name, arch);
  PkgSeedAdd(path, PM_METAL_PKG_AOT_CAP);
  snprintf(sig, sizeof(sig), "%s.sig", path);
  PkgSeedAdd(sig, PM_METAL_PKG_SIG_CAP);

  PkgPathGuest(path, sizeof(path), pkg->name, "wasm");
  PkgSeedAdd(path, PM_METAL_PKG_WASM_CAP);
  snprintf(sig, sizeof(sig), "%s.sig", path);
  PkgSeedAdd(sig, PM_METAL_PKG_SIG_CAP);

  for (i = 0; i < pkg->nassets; i++) {
    if (pkg->assets[i].name == NULL || pkg->assets[i].name[0] == '\0') {
      continue;
    }

    snprintf(path, sizeof(path), "mods/apps/%s/%s", pkg->name, pkg->assets[i].name);
    PkgSeedAdd(path, pkg->assets[i].cap);
  }
}

void pm_metal_pkg_init(void)
{
  if (mInited != 0u) {
    return;
  }

  mInited = 1;
  mPkgN   = 0;
  pm_metal_pkg_doom_register();
}

int32_t pm_metal_pkg_register(const pm_metal_pkg_t *pkg)
{
  uint32_t i;

  if (pkg == NULL || pkg->name == NULL || pkg->name[0] == '\0') {
    return -1;
  }

  pm_metal_pkg_init();

  for (i = 0; i < mPkgN; i++) {
    if (strcmp(mPkgs[i]->name, pkg->name) == 0) {
      return -1;
    }
  }

  if (mPkgN >= PM_METAL_PKG_MAX) {
    return -1;
  }

  mPkgs[mPkgN++] = pkg;
  return 0;
}

const pm_metal_pkg_t *pm_metal_pkg_lookup(const char *name)
{
  uint32_t i;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  pm_metal_pkg_init();

  for (i = 0; i < mPkgN; i++) {
    if (strcmp(mPkgs[i]->name, name) == 0) {
      return mPkgs[i];
    }
  }

  return NULL;
}

int32_t pm_metal_pkg_ready(const char *name)
{
  const pm_metal_pkg_t *pkg;

  pkg = pm_metal_pkg_lookup(name);
  if (pkg == NULL) {
    return 0;
  }

  if (pkg->ready != NULL) {
    return pkg->ready() ? 1 : 0;
  }

  return pm_metal_pkg_guest_ready(name);
}

const pm_metal_pkg_file_t *pm_metal_pkg_files(const char *name, uint32_t *out_n)
{
  const pm_metal_pkg_t *pkg;

  pkg = pm_metal_pkg_lookup(name);
  if (pkg == NULL) {
    mSeedN = 0;
    if (out_n != NULL) {
      *out_n = 0;
    }

    return NULL;
  }

  PkgSeedBuild(pkg);
  if (out_n != NULL) {
    *out_n = mSeedN;
  }

  return (mSeedN > 0u) ? mSeed : NULL;
}

int32_t pm_metal_pkg_file_optional(const char *name, const pm_metal_pkg_file_t *f)
{
  if (name == NULL || f == NULL || f->esp_path == NULL) {
    return 0;
  }

  /* Sigs never block readiness. */
  if (strstr(f->esp_path, ".sig") != NULL) {
    return 1;
  }

  /* Guest slots: skip once host AOT or wasm is already cached. */
  if (strstr(f->esp_path, ".aot") != NULL || strstr(f->esp_path, ".wasm") != NULL) {
    return pm_metal_pkg_guest_ready(name);
  }

  return 0;
}

int32_t pm_metal_pkg_ensure(const char *name)
{
  if (pm_metal_pkg_lookup(name) == NULL) {
    return 0;
  }

  if (pm_metal_pkg_ready(name)) {
    return 0;
  }

  return pm_metal_net_life_seed_ensure(name);
}

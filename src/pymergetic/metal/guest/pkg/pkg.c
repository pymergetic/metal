/** @file
  Package registry — standard guest paths by host arch; assets from pkg.
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pymergetic/metal/guest/pkg/pkg.h>
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

#ifndef PM_METAL_PKG_ASSET_MAX
#define PM_METAL_PKG_ASSET_MAX 8u
#endif

#ifndef PM_METAL_PKG_MANIFEST_CAP
#define PM_METAL_PKG_MANIFEST_CAP 512u
#endif

static const pm_metal_pkg_t *mPkgs[PM_METAL_PKG_MAX];
static uint32_t              mPkgN;
static uint8_t               mInited;

/* Scratch for dynamic seed plan (paths + slots). */
static char                mSeedPath[PM_METAL_PKG_SEED_MAX][96];
static pm_metal_pkg_file_t mSeed[PM_METAL_PKG_SEED_MAX];
static uint32_t            mSeedN;

/* Scratch for a package synthesized on the fly from an ESP assets.list
 * manifest (external app, no compiled-in pm_metal_pkg_register() call). */
static pm_metal_pkg_t       mSynthPkg;
static char                 mSynthName[64];
static pm_metal_pkg_asset_t mSynthAssets[PM_METAL_PKG_ASSET_MAX];
static char                 mSynthAssetName[PM_METAL_PKG_ASSET_MAX][64];
static char                 mManifestBuf[PM_METAL_PKG_MANIFEST_CAP];

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

static void PkgManifestPath(char *out, uintptr_t cap, const char *name)
{
  snprintf(out, cap, "mods/apps/%s/assets.list", name);
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

  /*
   * Manifest first — cold BIOS/PXE has an empty RAM ESP, so assets are
   * unknown until assets.list is fetched. The seed coro re-calls pkg_files()
   * after each put; once the list is cached, the WAD rows appear below.
   */
  PkgManifestPath(path, sizeof(path), pkg->name);
  PkgSeedAdd(path, PM_METAL_PKG_MANIFEST_CAP);

  /*
   * Wasm before AOT so cold PXE always has an interpreter fallback even if
   * the GUEST ensure pass is interrupted after the first binary lands.
   * Fetch still prefers AOT when both are cached.
   */
  PkgPathGuest(path, sizeof(path), pkg->name, "wasm");
  PkgSeedAdd(path, PM_METAL_PKG_WASM_CAP);
  snprintf(sig, sizeof(sig), "%s.sig", path);
  PkgSeedAdd(sig, PM_METAL_PKG_SIG_CAP);

  snprintf(path, sizeof(path), "mods/apps/%s/%s.%s.aot", pkg->name, pkg->name, arch);
  PkgSeedAdd(path, PM_METAL_PKG_AOT_CAP);
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

/*
 * Parse a raw "<filename> <cap_bytes>" per-line manifest buffer (blank
 * lines and '#' comments skipped) into asset slots. Mutates buf in place
 * (splits lines/fields with NUL).
 */
static uint32_t PkgManifestParse(char                 *buf,
                                 pm_metal_pkg_asset_t *out,
                                 char                  out_name[][64],
                                 uint32_t              cap)
{
  uint32_t n;
  char    *p;

  n = 0;
  p = buf;
  while (*p != '\0' && n < cap) {
    char *line;
    char *nl;
    char *sp;
    char *rest;

    line = p;
    nl   = strchr(p, '\n');
    if (nl != NULL) {
      *nl = '\0';
      p   = nl + 1;
    } else {
      p += strlen(p);
    }

    while (*line == ' ' || *line == '\t') {
      line++;
    }

    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }

    sp = strchr(line, ' ');
    if (sp == NULL) {
      continue;
    }

    *sp  = '\0';
    rest = sp + 1;
    while (*rest == ' ' || *rest == '\t') {
      rest++;
    }

    snprintf(out_name[n], sizeof(out_name[0]), "%s", line);
    out[n].name = out_name[n];
    out[n].cap  = (uint32_t)strtoul(rest, NULL, 10);
    n++;
  }

  return n;
}

/*
 * Fallback for external apps (built + signed outside this repo, e.g.
 * metal-doom) with no compiled-in pm_metal_pkg_register() call.
 *
 * Always synthesize by name so cold BIOS/PXE (empty RAM ESP) can still
 * HTTP-seed via pkg_ensure — requiring a local assets.list/wasm first was
 * a chicken-egg that skipped seed entirely. Unknown names 404 on :8080.
 * When assets.list is already cached, parse it for WAD/asset rows.
 */
static const pm_metal_pkg_t *PkgSynthFromManifest(const char *name)
{
  char     path[96];
  uint32_t nread;
  int32_t  have_manifest;

  nread = 0;
  PkgManifestPath(path, sizeof(path), name);
  have_manifest =
    (pm_metal_esp_read_at(path, 0, (uint8_t *)mManifestBuf, sizeof(mManifestBuf) - 1, &nread) == 0)
      ? 1
      : 0;

  if (!have_manifest) {
    nread           = 0;
    mManifestBuf[0] = '\0';
  } else {
    mManifestBuf[nread] = '\0';
  }

  mSynthPkg.nassets =
    have_manifest
      ? PkgManifestParse(mManifestBuf, mSynthAssets, mSynthAssetName, PM_METAL_PKG_ASSET_MAX)
      : 0u;

  snprintf(mSynthName, sizeof(mSynthName), "%s", name);
  mSynthPkg.name   = mSynthName;
  mSynthPkg.assets = mSynthAssets;
  mSynthPkg.ready  = NULL;
  return &mSynthPkg;
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

  return PkgSynthFromManifest(name);
}

int32_t pm_metal_pkg_ready(const char *name)
{
  const pm_metal_pkg_t *pkg;
  uint32_t              i;
  char                  path[96];

  pkg = pm_metal_pkg_lookup(name);
  if (pkg == NULL) {
    return 0;
  }

  if (pkg->ready != NULL) {
    return pkg->ready() ? 1 : 0;
  }

  if (!pm_metal_pkg_guest_ready(name)) {
    return 0;
  }

  /*
   * Guest binary plus at least one declared asset (when any). Requiring
   * every IWAD blocked PXE doom: two ~28MiB Freedoom WADs timed out / OOM'd
   * before load. Extra assets stay optional seed rows; seed stops once
   * ready() is true.
   */
  if (pkg->nassets == 0u) {
    return 1;
  }

  for (i = 0; i < pkg->nassets; i++) {
    if (pkg->assets[i].name == NULL || pkg->assets[i].name[0] == '\0') {
      continue;
    }

    snprintf(path, sizeof(path), "mods/apps/%s/%s", name, pkg->assets[i].name);
    if (PkgEspExists(path)) {
      return 1;
    }
  }

  return 0;
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

  (void)name;

  /* Sigs never block readiness. Per-path existence is handled by the seed coro. */
  if (strstr(f->esp_path, ".sig") != NULL) {
    return 1;
  }

  return 0;
}

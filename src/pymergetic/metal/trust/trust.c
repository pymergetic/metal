/** @file
  Multi-CA trust store: baked Root / Kernel-CA / Mods-CA pubs + ECDSA verify.
  Wasm/kernel sigs accepted if ANY matching signer CA verifies (multi-team).
**/
#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/trust/trust.h>
#include <pymergetic/metal/net/tls/mbedtls_metal_config.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

/*
 * Prefer baked pubs from -I build/trust (metal_trust_bake.inc.c).
 * Fall back to co-located metal_trust.inc.c so clangd / no-PKI builds
 * never see a missing include (and stay soft-disabled).
 */
#if defined(__has_include)
#if __has_include("metal_trust_bake.inc.c")
#include "metal_trust_bake.inc.c"
#else
#include "metal_trust.inc.c"
#endif
#else
#include "metal_trust.inc.c"
#endif

#ifndef PM_METAL_TRUST_BAKED
#define PM_METAL_TRUST_BAKED 0
#endif

#ifndef PM_METAL_TRUST_MODE
#define PM_METAL_TRUST_MODE PM_METAL_TRUST_MODE_OFF
#endif

#ifndef PM_METAL_TRUST_MAX_CAS
#define PM_METAL_TRUST_MAX_CAS 16u
#endif

#if PM_METAL_TRUST_BAKED

static mbedtls_x509_crt mRoots[PM_METAL_TRUST_MAX_CAS];
static uint32_t         mRootN;
static mbedtls_x509_crt mKernelCas[PM_METAL_TRUST_MAX_CAS];
static uint32_t         mKernelN;
static mbedtls_x509_crt mModsCas[PM_METAL_TRUST_MAX_CAS];
static uint32_t         mModsN;
static int32_t          mReady;
static int32_t          mInitTried;

static int32_t TrustParseList(mbedtls_x509_crt           *slots,
                              uint32_t                    slot_cap,
                              uint32_t                   *out_n,
                              const pm_metal_trust_der_t *list,
                              uint32_t                    list_n,
                              const char                 *kind)
{
  uint32_t i;
  uint32_t n;

  (void)kind; /* reserved for quiet diagnostics */
  n = 0;
  for (i = 0; i < list_n && n < slot_cap; i++) {
    int32_t e;

    mbedtls_x509_crt_init(&slots[n]);
    e = mbedtls_x509_crt_parse_der(slots + n, list[i].der, list[i].len);
    if (e != 0) {
      /* Keep quiet during boot tree; BOOT_TRUST line shows overall status. */
      mbedtls_x509_crt_free(&slots[n]);
      continue;
    }

    n++;
  }

  *out_n = n;
  return (n > 0u) ? 0 : -1;
}

static int32_t TrustEnsure(void)
{
  if (mInitTried) {
    return mReady;
  }

  mInitTried = 1;
  mRootN     = 0;
  mKernelN   = 0;
  mModsN     = 0;

  /* PLATFORM_MEMORY defaults to a null calloc until this hook runs. */
  pm_metal_net_tls_mbedtls_runtime_init();

  if (TrustParseList(mRoots,
                     PM_METAL_TRUST_MAX_CAS,
                     &mRootN,
                     g_pm_metal_trust_roots,
                     g_pm_metal_trust_root_count,
                     "root") != 0) {
    return 0;
  }

  if (TrustParseList(mKernelCas,
                     PM_METAL_TRUST_MAX_CAS,
                     &mKernelN,
                     g_pm_metal_trust_kernel_cas,
                     g_pm_metal_trust_kernel_ca_count,
                     "kernel") != 0) {
    return 0;
  }

  if (TrustParseList(mModsCas,
                     PM_METAL_TRUST_MAX_CAS,
                     &mModsN,
                     g_pm_metal_trust_mods_cas,
                     g_pm_metal_trust_mods_ca_count,
                     "mods") != 0) {
    return 0;
  }

  mReady = 1;
  return 1;
}

static int32_t TrustVerifyAny(mbedtls_x509_crt *signers,
                              uint32_t          n,
                              const void       *data,
                              uint32_t          data_len,
                              const void       *sig,
                              uint32_t          sig_len)
{
  uint8_t  hash[32];
  uint32_t i;
  int32_t  e;

  if (data == NULL || sig == NULL || data_len == 0 || sig_len == 0 || n == 0) {
    return -1;
  }

  if (!TrustEnsure()) {
    return -1;
  }

  e = mbedtls_sha256((const uint8_t *)data, data_len, hash, 0);
  if (e != 0) {
    return -1;
  }

  for (i = 0; i < n; i++) {
    e = mbedtls_pk_verify(
      &signers[i].pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), (const uint8_t *)sig, sig_len);
    if (e == 0) {
      return 0;
    }
  }

  return -1;
}
#endif /* PM_METAL_TRUST_BAKED */

int pm_metal_trust_baked(void)
{
#if PM_METAL_TRUST_BAKED
  return 1;
#else
  return 0;
#endif
}

int pm_metal_trust_mode(void)
{
  return (int)PM_METAL_TRUST_MODE;
}

const char *pm_metal_trust_mode_str(void)
{
  switch (pm_metal_trust_mode()) {
  case PM_METAL_TRUST_MODE_SOFT:
    return "soft";
  case PM_METAL_TRUST_MODE_ENFORCE:
    return "enforce";
  case PM_METAL_TRUST_MODE_OFF:
  default:
    return "off";
  }
}

int pm_metal_trust_ready(void)
{
#if PM_METAL_TRUST_BAKED
  if (pm_metal_trust_mode() == PM_METAL_TRUST_MODE_OFF) {
    return 0;
  }

  return TrustEnsure();
#else
  return 0;
#endif
}

static int32_t TrustAccept(
  int32_t (*verify_fn)(const void *data, uint32_t data_len, const void *sig, uint32_t sig_len),
  const void *data,
  uint32_t    data_len,
  const void *sig,
  uint32_t    sig_len)
{
  int32_t mode;

  mode = pm_metal_trust_mode();
  if (mode == PM_METAL_TRUST_MODE_OFF) {
    return 0;
  }

  if (sig == NULL || sig_len == 0) {
    return (mode == PM_METAL_TRUST_MODE_ENFORCE) ? -1 : 0;
  }

  if (!pm_metal_trust_ready()) {
    return -1;
  }

  return verify_fn(data, data_len, sig, sig_len);
}

int pm_metal_trust_verify_mods(const void *data,
                               uint32_t    data_len,
                               const void *sig,
                               uint32_t    sig_len)
{
#if PM_METAL_TRUST_BAKED
  return TrustVerifyAny(mModsCas, mModsN, data, data_len, sig, sig_len);
#else
  (void)data;
  (void)data_len;
  (void)sig;
  (void)sig_len;
  return (pm_metal_trust_mode() == PM_METAL_TRUST_MODE_ENFORCE) ? -1 : 0;
#endif
}

int pm_metal_trust_verify_kernel(const void *data,
                                 uint32_t    data_len,
                                 const void *sig,
                                 uint32_t    sig_len)
{
#if PM_METAL_TRUST_BAKED
  return TrustVerifyAny(mKernelCas, mKernelN, data, data_len, sig, sig_len);
#else
  (void)data;
  (void)data_len;
  (void)sig;
  (void)sig_len;
  return (pm_metal_trust_mode() == PM_METAL_TRUST_MODE_ENFORCE) ? -1 : 0;
#endif
}

int pm_metal_trust_accept_mods(const void *data,
                               uint32_t    data_len,
                               const void *sig,
                               uint32_t    sig_len)
{
  return TrustAccept(pm_metal_trust_verify_mods, data, data_len, sig, sig_len);
}

int pm_metal_trust_accept_kernel(const void *data,
                                 uint32_t    data_len,
                                 const void *sig,
                                 uint32_t    sig_len)
{
  return TrustAccept(pm_metal_trust_verify_kernel, data, data_len, sig, sig_len);
}

static pm_metal_trust_boot_t mBootStatus = PM_METAL_TRUST_BOOT_OFF;

int pm_metal_trust_strict(void)
{
#if PM_METAL_TRUST_STRICT
  return 1;
#else
  return 0;
#endif
}

pm_metal_trust_boot_t pm_metal_trust_boot_status(void)
{
  return mBootStatus;
}

const char *pm_metal_trust_boot_status_str(void)
{
  switch (mBootStatus) {
  case PM_METAL_TRUST_BOOT_OK:
    return "ok";
  case PM_METAL_TRUST_BOOT_WARN:
    return "WARN";
  case PM_METAL_TRUST_BOOT_FAIL:
    return "FAIL";
  case PM_METAL_TRUST_BOOT_OFF:
  default:
    return "off";
  }
}

/**
 * Enforce failures are intrinsically fatal (METAL-005). Soft continues
 * unless STRICT. ENFORCE_CONTINUE is a diagnostic escape only.
 */
static int32_t TrustBootReturnFail(void)
{
  int32_t mode;

  mBootStatus = PM_METAL_TRUST_BOOT_FAIL;
  mode        = pm_metal_trust_mode();
  if (mode == PM_METAL_TRUST_MODE_ENFORCE) {
#if PM_METAL_TRUST_ENFORCE_CONTINUE
    return 0;
#else
    return -1;
#endif
  }

  return pm_metal_trust_strict() ? -1 : 0;
}

static int32_t TrustBootReturnWarn(void)
{
  int32_t mode;

  mBootStatus = PM_METAL_TRUST_BOOT_WARN;
  mode        = pm_metal_trust_mode();
  if (mode == PM_METAL_TRUST_MODE_ENFORCE) {
#if PM_METAL_TRUST_ENFORCE_CONTINUE
    return 0;
#else
    return -1;
#endif
  }

  return pm_metal_trust_strict() ? -1 : 0;
}

static void TrustSigPath(const char *img_path, char *sig_path, uintptr_t sig_cap)
{
  uintptr_t n;

  n = 0;
  if (img_path != NULL) {
    while (img_path[n] != '\0' && n + 5 < sig_cap) {
      sig_path[n] = img_path[n];
      n++;
    }
  }

  if (n + 4 < sig_cap) {
    sig_path[n++] = '.';
    sig_path[n++] = 's';
    sig_path[n++] = 'i';
    sig_path[n++] = 'g';
  }

  sig_path[n] = '\0';
}

/**
 * Verify ESP bytes at the LoadedImage path (+ matching .sig).
 * Path identity comes from EFI_LOADED_IMAGE_PROTOCOL — never another
 * candidate filename (METAL-006). Returns 0 ok, 1 missing, -1 bad sig.
 */
static int32_t TrustBootVerifyLoaded(const char *img_path)
{
  char     sig_path[160];
  uint8_t *img;
  uint32_t img_len;
  uint8_t *sig;
  uint32_t sig_len;
  int32_t  rc;

  if (img_path == NULL || img_path[0] == '\0') {
    return 1;
  }

  TrustSigPath(img_path, sig_path, sizeof(sig_path));
  img = NULL;
  sig = NULL;
  if (pm_metal_esp_file_size(img_path, &img_len) != 0 ||
      pm_metal_esp_file_size(sig_path, &sig_len) != 0) {
    return 1;
  }

  if (pm_metal_esp_read_file(img_path, &img, &img_len) != 0 ||
      pm_metal_esp_read_file(sig_path, &sig, &sig_len) != 0) {
    if (img != NULL) {
      pm_metal_mem_free(img);
    }

    if (sig != NULL) {
      pm_metal_mem_free(sig);
    }

    return 1;
  }

  rc = pm_metal_trust_accept_kernel(img, img_len, sig, sig_len);
  pm_metal_mem_free(img);
  pm_metal_mem_free(sig);
  return (rc == 0) ? 0 : -1;
}

int pm_metal_trust_boot_check(void)
{
  const char *loaded;
  int32_t     tr;

  if (pm_metal_trust_mode() == PM_METAL_TRUST_MODE_OFF) {
    mBootStatus = PM_METAL_TRUST_BOOT_OFF;
    return 0;
  }

  if (!pm_metal_trust_baked()) {
    if (pm_metal_trust_mode() == PM_METAL_TRUST_MODE_ENFORCE) {
      return TrustBootReturnFail();
    }

    mBootStatus = PM_METAL_TRUST_BOOT_OFF;
    return 0;
  }

  if (!pm_metal_trust_ready()) {
    /* Pubs baked but parse failed — real problem in any checking mode. */
    return TrustBootReturnWarn();
  }

  if (!pm_metal_esp_ready()) {
    if (pm_metal_trust_mode() == PM_METAL_TRUST_MODE_ENFORCE) {
      return TrustBootReturnFail();
    }

    /* soft: no ESP to check → unsigned OK */
    mBootStatus = PM_METAL_TRUST_BOOT_OK;
    return 0;
  }

  /*
   * METAL-006: only the executing artifact. Never succeed because some
   * other candidate on the ESP still has a valid signature.
   */
  loaded = pm_metal_esp_loaded_path();
  if (loaded == NULL) {
    if (pm_metal_trust_mode() == PM_METAL_TRUST_MODE_ENFORCE) {
      return TrustBootReturnFail();
    }

    mBootStatus = PM_METAL_TRUST_BOOT_OK;
    return 0;
  }

  tr = TrustBootVerifyLoaded(loaded);
  if (tr == 0) {
    mBootStatus = PM_METAL_TRUST_BOOT_OK;
    return 0;
  }

  if (tr < 0) {
    return TrustBootReturnFail();
  }

  /* Missing loaded image+.sig pair. */
  if (pm_metal_trust_mode() == PM_METAL_TRUST_MODE_ENFORCE) {
    return TrustBootReturnFail();
  }

  mBootStatus = PM_METAL_TRUST_BOOT_OK;
  return 0;
}

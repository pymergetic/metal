/** @file
  Multi-CA trust store: baked Root / Kernel-CA / Mods-CA pubs + ECDSA verify.
  Wasm/kernel sigs accepted if ANY matching signer CA verifies (multi-team).
**/
#include <pymergetic/metal/trust/trust.h>
#include <pymergetic/metal/dev/net/mbedtls_metal_config.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/mem/mem.h>

#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

/*
 * Prefer baked pubs from -I build/trust (metal_trust_bake.inc.c).
 * Fall back to co-located metal_trust.inc.c so clangd / no-PKI builds
 * never see a missing include (and stay soft-disabled).
 */
#if defined(__has_include)
#  if __has_include("metal_trust_bake.inc.c")
#    include "metal_trust_bake.inc.c"
#  else
#    include "metal_trust.inc.c"
#  endif
#else
#  include "metal_trust.inc.c"
#endif

#ifndef PM_METAL_TRUST_BAKED
#define PM_METAL_TRUST_BAKED 0
#endif

#ifndef PM_METAL_TRUST_MODE
#define PM_METAL_TRUST_MODE  PM_METAL_TRUST_MODE_OFF
#endif

#ifndef PM_METAL_TRUST_MAX_CAS
#define PM_METAL_TRUST_MAX_CAS  16u
#endif

#if PM_METAL_TRUST_BAKED

STATIC mbedtls_x509_crt  mRoots[PM_METAL_TRUST_MAX_CAS];
STATIC UINT32            mRootN;
STATIC mbedtls_x509_crt  mKernelCas[PM_METAL_TRUST_MAX_CAS];
STATIC UINT32            mKernelN;
STATIC mbedtls_x509_crt  mModsCas[PM_METAL_TRUST_MAX_CAS];
STATIC UINT32            mModsN;
STATIC INT32             mReady;
STATIC INT32             mInitTried;

STATIC
INT32
TrustParseList (
  mbedtls_x509_crt           *slots,
  UINT32                      slot_cap,
  UINT32                     *out_n,
  CONST pm_metal_trust_der_t *list,
  UINT32                      list_n,
  CONST CHAR8                *kind
  )
{
  UINT32  i;
  UINT32  n;

  (VOID)kind; /* reserved for quiet diagnostics */
  n = 0;
  for (i = 0; i < list_n && n < slot_cap; i++) {
    INT32  e;

    mbedtls_x509_crt_init (&slots[n]);
    e = mbedtls_x509_crt_parse_der (slots + n, list[i].der, list[i].len);
    if (e != 0) {
      /* Keep quiet during boot tree; BOOT_TRUST line shows overall status. */
      mbedtls_x509_crt_free (&slots[n]);
      continue;
    }

    n++;
  }

  *out_n = n;
  return (n > 0u) ? 0 : -1;
}

STATIC
INT32
TrustEnsure (
  VOID
  )
{
  if (mInitTried) {
    return mReady;
  }

  mInitTried = 1;
  mRootN     = 0;
  mKernelN   = 0;
  mModsN     = 0;

  /* PLATFORM_MEMORY defaults to a null calloc until this hook runs. */
  pm_metal_mbedtls_runtime_init ();

  if (TrustParseList (
        mRoots,
        PM_METAL_TRUST_MAX_CAS,
        &mRootN,
        g_pm_metal_trust_roots,
        g_pm_metal_trust_root_count,
        "root"
        ) != 0)
  {
    return 0;
  }

  if (TrustParseList (
        mKernelCas,
        PM_METAL_TRUST_MAX_CAS,
        &mKernelN,
        g_pm_metal_trust_kernel_cas,
        g_pm_metal_trust_kernel_ca_count,
        "kernel"
        ) != 0)
  {
    return 0;
  }

  if (TrustParseList (
        mModsCas,
        PM_METAL_TRUST_MAX_CAS,
        &mModsN,
        g_pm_metal_trust_mods_cas,
        g_pm_metal_trust_mods_ca_count,
        "mods"
        ) != 0)
  {
    return 0;
  }

  mReady = 1;
  return 1;
}

STATIC
INT32
TrustVerifyAny (
  mbedtls_x509_crt  *signers,
  UINT32             n,
  CONST VOID        *data,
  UINT32             data_len,
  CONST VOID        *sig,
  UINT32             sig_len
  )
{
  UINT8   hash[32];
  UINT32  i;
  INT32   e;

  if (data == NULL || sig == NULL || data_len == 0 || sig_len == 0 || n == 0) {
    return -1;
  }

  if (!TrustEnsure ()) {
    return -1;
  }

  e = mbedtls_sha256 ((CONST UINT8 *)data, data_len, hash, 0);
  if (e != 0) {
    return -1;
  }

  for (i = 0; i < n; i++) {
    e = mbedtls_pk_verify (
          &signers[i].pk,
          MBEDTLS_MD_SHA256,
          hash,
          sizeof (hash),
          (CONST UINT8 *)sig,
          sig_len
          );
    if (e == 0) {
      return 0;
    }
  }

  return -1;
}
#endif /* PM_METAL_TRUST_BAKED */

int
pm_metal_trust_baked (
  VOID
  )
{
#if PM_METAL_TRUST_BAKED
  return 1;
#else
  return 0;
#endif
}

int
pm_metal_trust_mode (
  VOID
  )
{
  return (int)PM_METAL_TRUST_MODE;
}

CONST CHAR8 *
pm_metal_trust_mode_str (
  VOID
  )
{
  switch (pm_metal_trust_mode ()) {
    case PM_METAL_TRUST_MODE_SOFT:
      return "soft";
    case PM_METAL_TRUST_MODE_ENFORCE:
      return "enforce";
    case PM_METAL_TRUST_MODE_OFF:
    default:
      return "off";
  }
}

int
pm_metal_trust_ready (
  VOID
  )
{
#if PM_METAL_TRUST_BAKED
  if (pm_metal_trust_mode () == PM_METAL_TRUST_MODE_OFF) {
    return 0;
  }

  return TrustEnsure ();
#else
  return 0;
#endif
}

STATIC
INT32
TrustAccept (
  INT32 (
    *verify_fn
    )(
      CONST VOID  *data,
      UINT32       data_len,
      CONST VOID  *sig,
      UINT32       sig_len
      ),
  CONST VOID  *data,
  UINT32       data_len,
  CONST VOID  *sig,
  UINT32       sig_len
  )
{
  INT32  mode;

  mode = pm_metal_trust_mode ();
  if (mode == PM_METAL_TRUST_MODE_OFF) {
    return 0;
  }

  if (sig == NULL || sig_len == 0) {
    return (mode == PM_METAL_TRUST_MODE_ENFORCE) ? -1 : 0;
  }

  if (!pm_metal_trust_ready ()) {
    return -1;
  }

  return verify_fn (data, data_len, sig, sig_len);
}

int
pm_metal_trust_verify_mods (
  CONST VOID  *data,
  UINT32       data_len,
  CONST VOID  *sig,
  UINT32       sig_len
  )
{
#if PM_METAL_TRUST_BAKED
  return TrustVerifyAny (mModsCas, mModsN, data, data_len, sig, sig_len);
#else
  (VOID)data;
  (VOID)data_len;
  (VOID)sig;
  (VOID)sig_len;
  return (pm_metal_trust_mode () == PM_METAL_TRUST_MODE_ENFORCE) ? -1 : 0;
#endif
}

int
pm_metal_trust_verify_kernel (
  CONST VOID  *data,
  UINT32       data_len,
  CONST VOID  *sig,
  UINT32       sig_len
  )
{
#if PM_METAL_TRUST_BAKED
  return TrustVerifyAny (mKernelCas, mKernelN, data, data_len, sig, sig_len);
#else
  (VOID)data;
  (VOID)data_len;
  (VOID)sig;
  (VOID)sig_len;
  return (pm_metal_trust_mode () == PM_METAL_TRUST_MODE_ENFORCE) ? -1 : 0;
#endif
}

int
pm_metal_trust_accept_mods (
  CONST VOID  *data,
  UINT32       data_len,
  CONST VOID  *sig,
  UINT32       sig_len
  )
{
  return TrustAccept (
           pm_metal_trust_verify_mods,
           data,
           data_len,
           sig,
           sig_len
           );
}

int
pm_metal_trust_accept_kernel (
  CONST VOID  *data,
  UINT32       data_len,
  CONST VOID  *sig,
  UINT32       sig_len
  )
{
  return TrustAccept (
           pm_metal_trust_verify_kernel,
           data,
           data_len,
           sig,
           sig_len
           );
}

STATIC pm_metal_trust_boot_t  mBootStatus = PM_METAL_TRUST_BOOT_OFF;

int
pm_metal_trust_strict (
  VOID
  )
{
#if PM_METAL_TRUST_STRICT
  return 1;
#else
  return 0;
#endif
}

pm_metal_trust_boot_t
pm_metal_trust_boot_status (
  VOID
  )
{
  return mBootStatus;
}

CONST CHAR8 *
pm_metal_trust_boot_status_str (
  VOID
  )
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
STATIC
INT32
TrustBootReturnFail (
  VOID
  )
{
  INT32  mode;

  mBootStatus = PM_METAL_TRUST_BOOT_FAIL;
  mode        = pm_metal_trust_mode ();
  if (mode == PM_METAL_TRUST_MODE_ENFORCE) {
#if PM_METAL_TRUST_ENFORCE_CONTINUE
    return 0;
#else
    return -1;
#endif
  }

  return pm_metal_trust_strict () ? -1 : 0;
}

STATIC
INT32
TrustBootReturnWarn (
  VOID
  )
{
  INT32  mode;

  mBootStatus = PM_METAL_TRUST_BOOT_WARN;
  mode        = pm_metal_trust_mode ();
  if (mode == PM_METAL_TRUST_MODE_ENFORCE) {
#if PM_METAL_TRUST_ENFORCE_CONTINUE
    return 0;
#else
    return -1;
#endif
  }

  return pm_metal_trust_strict () ? -1 : 0;
}

STATIC
VOID
TrustSigPath (
  CONST CHAR8  *img_path,
  CHAR8        *sig_path,
  UINTN         sig_cap
  )
{
  UINTN  n;

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
STATIC
INT32
TrustBootVerifyLoaded (
  CONST CHAR8  *img_path
  )
{
  CHAR8   sig_path[160];
  UINT8  *img;
  UINT32  img_len;
  UINT8  *sig;
  UINT32  sig_len;
  INT32   rc;

  if (img_path == NULL || img_path[0] == '\0') {
    return 1;
  }

  TrustSigPath (img_path, sig_path, sizeof (sig_path));
  img = NULL;
  sig = NULL;
  if (pm_metal_esp_file_size (img_path, &img_len) != 0
      || pm_metal_esp_file_size (sig_path, &sig_len) != 0)
  {
    return 1;
  }

  if (pm_metal_esp_read_file (img_path, &img, &img_len) != 0
      || pm_metal_esp_read_file (sig_path, &sig, &sig_len) != 0)
  {
    if (img != NULL) {
      pm_metal_mem_free (img);
    }

    if (sig != NULL) {
      pm_metal_mem_free (sig);
    }

    return 1;
  }

  rc = pm_metal_trust_accept_kernel (img, img_len, sig, sig_len);
  pm_metal_mem_free (img);
  pm_metal_mem_free (sig);
  return (rc == 0) ? 0 : -1;
}

int
pm_metal_trust_boot_check (
  VOID
  )
{
  CONST CHAR8  *loaded;
  INT32         tr;

  if (pm_metal_trust_mode () == PM_METAL_TRUST_MODE_OFF) {
    mBootStatus = PM_METAL_TRUST_BOOT_OFF;
    return 0;
  }

  if (!pm_metal_trust_baked ()) {
    if (pm_metal_trust_mode () == PM_METAL_TRUST_MODE_ENFORCE) {
      return TrustBootReturnFail ();
    }

    mBootStatus = PM_METAL_TRUST_BOOT_OFF;
    return 0;
  }

  if (!pm_metal_trust_ready ()) {
    /* Pubs baked but parse failed — real problem in any checking mode. */
    return TrustBootReturnWarn ();
  }

  if (!pm_metal_esp_ready ()) {
    if (pm_metal_trust_mode () == PM_METAL_TRUST_MODE_ENFORCE) {
      return TrustBootReturnFail ();
    }

    /* soft: no ESP to check → unsigned OK */
    mBootStatus = PM_METAL_TRUST_BOOT_OK;
    return 0;
  }

  /*
   * METAL-006: only the executing artifact. Never succeed because some
   * other candidate on the ESP still has a valid signature.
   */
  loaded = pm_metal_esp_loaded_path ();
  if (loaded == NULL) {
    if (pm_metal_trust_mode () == PM_METAL_TRUST_MODE_ENFORCE) {
      return TrustBootReturnFail ();
    }

    mBootStatus = PM_METAL_TRUST_BOOT_OK;
    return 0;
  }

  tr = TrustBootVerifyLoaded (loaded);
  if (tr == 0) {
    mBootStatus = PM_METAL_TRUST_BOOT_OK;
    return 0;
  }

  if (tr < 0) {
    return TrustBootReturnFail ();
  }

  /* Missing loaded image+.sig pair. */
  if (pm_metal_trust_mode () == PM_METAL_TRUST_MODE_ENFORCE) {
    return TrustBootReturnFail ();
  }

  mBootStatus = PM_METAL_TRUST_BOOT_OK;
  return 0;
}

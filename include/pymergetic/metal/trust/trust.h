/*
 * Code trust — multi-CA baked publics + detached ECDSA-SHA256 sigs.
 *
 * PKI (private keys) lives outside the tree (METAL_PKI_DIR).
 * Build bakes Root / Kernel-CA / Mods-CA DER lists into
 * build/trust/metal_trust_bake.inc.c (pymergetic + team realms / extra/).
 * Verify accepts a signature from any matching CA in the baked list.
 *
 * Policy mode (bake + loader agree via PM_METAL_TRUST_MODE):
 *   off     — never verify (dev)
 *   soft    — verify when .sig present; unsigned OK (asymmetric)
 *   enforce — signatures required for external artifacts (prod)
 *
 * impl: common — src/pymergetic/metal/trust/trust.c
 */
#ifndef PYMERGETIC_METAL_TRUST_TRUST_H_
#define PYMERGETIC_METAL_TRUST_TRUST_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/** Build with -DMETAL_TRUST_STRICT=1 → boot hard-fails on bad/missing kernel sig. */
#ifndef PM_METAL_TRUST_STRICT
#define PM_METAL_TRUST_STRICT 0
#endif

#ifndef PM_METAL_TRUST_MODE_OFF
#define PM_METAL_TRUST_MODE_OFF     0
#define PM_METAL_TRUST_MODE_SOFT    1
#define PM_METAL_TRUST_MODE_ENFORCE 2
#endif

typedef enum {
	PM_METAL_TRUST_BOOT_OFF = 0, /* mode off or CA publics not baked */
	PM_METAL_TRUST_BOOT_OK,
	PM_METAL_TRUST_BOOT_WARN, /* missing image/sig or soft failure */
	PM_METAL_TRUST_BOOT_FAIL  /* crypto verify failed / enforce missing */
} pm_metal_trust_boot_t;

/**
 * 1 if CA pubs were baked into the image (compile-time). Does not parse.
 * Safe for floor-tree printing before mbedtls heap hooks are live.
 */
int pm_metal_trust_baked(void);

/** Compile-time policy: OFF / SOFT / ENFORCE. */
int pm_metal_trust_mode(void);

/** "off" / "soft" / "enforce". */
const char *pm_metal_trust_mode_str(void);

/** 1 if baked CAs parsed and crypto verify is usable. */
int pm_metal_trust_ready(void);

/** 1 if METAL_TRUST_STRICT / PM_METAL_TRUST_STRICT. */
int pm_metal_trust_strict(void);

/**
 * Verify detached signature with any baked Mods CA (SHA-256 + ECDSA-P256).
 * Pure crypto — prefer pm_metal_trust_accept_mods for policy.
 * Returns 0 on success.
 */
int pm_metal_trust_verify_mods(const void *data, uint32_t data_len,
			       const void *sig, uint32_t sig_len);

/** Verify with any baked Kernel CA. */
int pm_metal_trust_verify_kernel(const void *data, uint32_t data_len,
				 const void *sig, uint32_t sig_len);

/**
 * Policy gate for mods: off → ok; soft → ok if no sig, else verify;
 * enforce → sig required + verify. sig may be NULL / sig_len 0.
 */
int pm_metal_trust_accept_mods(const void *data, uint32_t data_len,
			       const void *sig, uint32_t sig_len);

/** Same policy for kernel image + detached sig. */
int pm_metal_trust_accept_kernel(const void *data, uint32_t data_len,
				 const void *sig, uint32_t sig_len);

/**
 * Early boot: load kernel image + .sig from ESP (tried paths) and verify.
 * Updates last boot status. Returns 0 if OK or soft-warn; -1 if FAIL and strict.
 */
int pm_metal_trust_boot_check(void);

/** Result of the last pm_metal_trust_boot_check (or OFF if never run). */
pm_metal_trust_boot_t pm_metal_trust_boot_status(void);

/** Short status word for boot tree: "ok" / "WARN" / "FAIL" / "off". */
const char *pm_metal_trust_boot_status_str(void);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_TRUST_TRUST_H_ */

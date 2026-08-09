/*
 * Code trust — Monocypher EdDSA verify + Kconfig policy.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/trust/__init__.h>
#include <pymergetic/metal/log.h>

#include "monocypher.h"

#if defined(CONFIG_PM_METAL_TRUST_MODE_ENFORCE)
#define PM_METAL_TRUST_MODE_VAL PM_METAL_TRUST_MODE_ENFORCE
#elif defined(CONFIG_PM_METAL_TRUST_MODE_OFF)
#define PM_METAL_TRUST_MODE_VAL PM_METAL_TRUST_MODE_OFF
#else
#define PM_METAL_TRUST_MODE_VAL PM_METAL_TRUST_MODE_SOFT
#endif

static uint8_t g_mods_pk[PM_METAL_TRUST_PUBKEY_LEN];
static int32_t g_mods_pk_set;

int32_t pm_metal_trust_mode(void)
{
  return (int32_t)PM_METAL_TRUST_MODE_VAL;
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

int32_t pm_metal_trust_ready(void)
{
  return g_mods_pk_set;
}

int32_t pm_metal_trust_mods_pubkey_set(const uint8_t *pk, uint32_t pk_len)
{
  if (pk == NULL || pk_len == 0u) {
    memset(g_mods_pk, 0, sizeof(g_mods_pk));
    g_mods_pk_set = 0;
    return 0;
  }
  if (pk_len != PM_METAL_TRUST_PUBKEY_LEN) {
    return -1;
  }
  memcpy(g_mods_pk, pk, PM_METAL_TRUST_PUBKEY_LEN);
  g_mods_pk_set = 1;
  return 0;
}

int32_t pm_metal_trust_verify_mods(const void *data,
                                   uint32_t data_len,
                                   const void *sig,
                                   uint32_t sig_len)
{
  if (data == NULL || data_len == 0u || sig == NULL || sig_len != PM_METAL_TRUST_SIG_LEN) {
    return -1;
  }
  if (!g_mods_pk_set) {
    return -1;
  }
  if (crypto_eddsa_check((const uint8_t *)sig, g_mods_pk, (const uint8_t *)data, data_len) != 0) {
    return -1;
  }
  return 0;
}

int32_t pm_metal_trust_accept_mods(const void *data,
                                   uint32_t data_len,
                                   const void *sig,
                                   uint32_t sig_len)
{
  int32_t mode;

  mode = pm_metal_trust_mode();
  if (mode == PM_METAL_TRUST_MODE_OFF) {
    return 0;
  }
  if (sig == NULL || sig_len == 0u) {
    if (mode == PM_METAL_TRUST_MODE_SOFT) {
      return 0;
    }
    return -1;
  }
  return pm_metal_trust_verify_mods(data, data_len, sig, sig_len);
}

int32_t pm_metal_trust_proof(void)
{
  /* Fixed test vector (seed 01..20); public + sig only — no private key in image. */
  static const uint8_t pk[PM_METAL_TRUST_PUBKEY_LEN] = {
      0xd4, 0xf8, 0xe6, 0xf2, 0x67, 0x27, 0x11, 0x77, 0xc1, 0x1d, 0x17, 0xd3,
      0x98, 0x10, 0xd7, 0x47, 0x16, 0x65, 0x72, 0xa1, 0xb6, 0xdb, 0x8e, 0x35,
      0x23, 0x63, 0xd9, 0x78, 0x6e, 0xb0, 0x79, 0x83};
  static const uint8_t sig[PM_METAL_TRUST_SIG_LEN] = {
      0xda, 0x94, 0xeb, 0x11, 0xdf, 0xfd, 0xfd, 0x81, 0xd5, 0x86, 0xd4, 0x75,
      0x05, 0xc3, 0xa1, 0x1e, 0x61, 0x19, 0x84, 0x27, 0x7a, 0x1b, 0x3e, 0xe1,
      0x5a, 0x1c, 0xd6, 0x17, 0x20, 0xb9, 0xb9, 0x58, 0xb6, 0x06, 0xf0, 0x5c,
      0x9b, 0x31, 0x66, 0x8e, 0x94, 0x32, 0x97, 0x33, 0x8b, 0xf6, 0x8e, 0x79,
      0xc9, 0x6d, 0x19, 0xad, 0xdc, 0x78, 0x28, 0xb2, 0x58, 0x34, 0x2d, 0x19,
      0xb9, 0x02, 0xc4, 0x05};
  static const uint8_t msg[] = "metal trust proof";
  uint8_t bad[PM_METAL_TRUST_SIG_LEN];

  if (pm_metal_trust_mods_pubkey_set(pk, sizeof(pk)) != 0) {
    return -1;
  }
  if (pm_metal_trust_verify_mods(msg, (uint32_t)(sizeof(msg) - 1u), sig, sizeof(sig)) != 0) {
    return -1;
  }
  memcpy(bad, sig, sizeof(bad));
  bad[0] ^= 1u;
  if (pm_metal_trust_verify_mods(msg, (uint32_t)(sizeof(msg) - 1u), bad, sizeof(bad)) == 0) {
    return -1;
  }
  /* Soft/off: unsigned body accepted. Enforce builds skip this path. */
  if (pm_metal_trust_mode() != PM_METAL_TRUST_MODE_ENFORCE) {
    if (pm_metal_trust_accept_mods(msg, (uint32_t)(sizeof(msg) - 1u), NULL, 0u) != 0) {
      return -1;
    }
  } else if (pm_metal_trust_accept_mods(msg, (uint32_t)(sizeof(msg) - 1u), NULL, 0u) == 0) {
    return -1;
  }
  if (pm_metal_trust_accept_mods(msg, (uint32_t)(sizeof(msg) - 1u), sig, sizeof(sig)) != 0) {
    return -1;
  }
  pm_metal_log((const uint8_t *)"trust verify ok");
  return 0;
}

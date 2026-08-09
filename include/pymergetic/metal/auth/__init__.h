/*
 * Shared auth — Argon2id passwords + SSH public keys (httpd / sshd).
 * Bcrypt ($2a$/$2b$) omitted until external/crypt_blowfish is vendored.
 */
#ifndef PYMERGETIC_METAL_AUTH_H_
#define PYMERGETIC_METAL_AUTH_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_AUTH_USER_MAX        32u
#define PM_METAL_AUTH_HASH_MAX        256u
#define PM_METAL_AUTH_USERS_MAX       8u
#define PM_METAL_AUTH_ALGO_MAX        32u
#define PM_METAL_AUTH_PUBKEY_BLOB_MAX 512u
#define PM_METAL_AUTH_PUBKEYS_MAX     16u
#define PM_METAL_AUTH_PUBKEY_PATH     "/etc/ssh/authorized_keys"

typedef struct {
  int32_t used;
  char    name[PM_METAL_AUTH_USER_MAX];
  char    hash[PM_METAL_AUTH_HASH_MAX];
} pm_metal_auth_user_t;

typedef struct {
  int32_t  used;
  char     user[PM_METAL_AUTH_USER_MAX];
  char     algo[PM_METAL_AUTH_ALGO_MAX];
  uint8_t  blob[PM_METAL_AUTH_PUBKEY_BLOB_MAX];
  uint32_t blob_len;
} pm_metal_auth_pubkey_t;

/** Clear and install users (replaces table). n may be 0. */
void pm_metal_auth_users_set(const pm_metal_auth_user_t *users, uint32_t n);

/** 1 match, 0 no. */
int32_t pm_metal_auth_user_check(const char *user, const char *pass);

/**
 * Verify password against an encoded hash string.
 * Supports $argon2id$... (PHC-ish, std base64). Returns 1 match, 0 no.
 */
int32_t pm_metal_auth_hash_verify(const char *encoded, const char *pass);

/**
 * Decode HTTP Basic Authorization value (after "Basic ") into user/pass.
 * Returns 0 ok, -1 bad.
 */
int32_t pm_metal_auth_basic_decode(
  const char *b64, char *user, uint32_t user_cap, char *pass, uint32_t pass_cap);

/** Clear authorized public keys. */
void pm_metal_auth_pubkeys_clear(void);

/**
 * Append one authorized key (raw SSH wire blob + algo name).
 * Returns 0 ok, -1 bad/full.
 */
int32_t pm_metal_auth_pubkey_add(const char *user, const char *algo, const uint8_t *blob,
                                 uint32_t blob_len);

/**
 * Parse one OpenSSH authorized_keys line for user (ssh-ed25519 AAAA... [comment]).
 * Returns 0 ok, -1 skip/bad.
 */
int32_t pm_metal_auth_pubkey_add_line(const char *user, const char *line);

/**
 * Load OpenSSH authorized_keys text for user (newline-separated lines).
 * Returns number of keys added.
 */
int32_t pm_metal_auth_pubkey_load_text(const char *user, const char *text, uint32_t text_len);

/**
 * Check whether key_blob is authorized for user.
 * algo may be NULL (match any) or e.g. "ssh-ed25519".
 * Returns 1 match, 0 no.
 */
int32_t pm_metal_auth_pubkey_check(const char *user, const char *algo, const uint8_t *key_blob,
                                   uint32_t key_len);

/** Load this module's RegMod (idempotent). */
int32_t pm_metal_auth_reg_load(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_AUTH_H_ */

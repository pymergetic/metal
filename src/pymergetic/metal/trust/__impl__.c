/* pymergetic.metal.trust — SHA-256 of caller bytes (mbedtls already on every seat). */
#include "pymergetic/metal/trust/__exports__.h"

#include "mbedtls/sha256.h"

#include <string.h>

static pm_util_mem_arena_t *s_arena;

int32_t pm_metal_trust_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    return 0;
}

void pm_metal_trust_deinit(void) {
    s_arena = NULL;
}

int32_t pm_metal_trust_sha256(const uint8_t *data, uint32_t n, uint8_t *out) {
    if (s_arena == NULL || (n != 0 && data == NULL) || out == NULL) {
        return -1;
    }
    if (mbedtls_sha256(data, n, out, 0) != 0) {
        return -1;
    }
    return 0;
}

int32_t pm_metal_trust_eq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint32_t i;
    uint8_t d = 0;
    if (a == NULL || b == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        d = (uint8_t)(d | (a[i] ^ b[i]));
    }
    return d == 0 ? 1 : 0;
}

/*----------------------------------------------------------------------
 * pymergetic.metal.trust.Digest — the card's domain type, defined in
 * the impl language (same posture as PM_MOD_EXPORT_C; no .pmdef).
 * The 32 SHA-256 bytes pack as 4 u64 cells (zero-copy C view:
 *   pm_trust_digest_view_t — w0..w3 at 0/8/16/24, created at 32),
 * `created` carries a boot clock stamp. Fields sorted by name_hash
 * (created 0x0A3D < w0 0x79CC < w1 0x79CD < w2 0x79CE < w3 0x79CF
 * — the DEFINE contract; the prove asserts order at runtime).
 *--------------------------------------------------------------------*/
static const pm_type_field_t s_trust_digest_fields[] = {
    { 0x0A3D, 0, 32, &PM_TYPE_I64_DESC, "created" },
    { 0x79CC, 0, 0, &PM_TYPE_U64_DESC, "w0" },
    { 0x79CD, 0, 8, &PM_TYPE_U64_DESC, "w1" },
    { 0x79CE, 0, 16, &PM_TYPE_U64_DESC, "w2" },
    { 0x79CF, 0, 24, &PM_TYPE_U64_DESC, "w3" },
};
PM_TYPE_DEFINE_C(s_trust_digest_desc, "pymergetic.metal.trust.Digest",
    PM_TYPE_DESC_STRUCT, 40, NULL, s_trust_digest_fields, 5);

/* Pack a 32-byte hash into a Digest value (arena-backed, refcount 1). */
pm_type_value_t pm_metal_trust_digest_new(pm_util_mem_arena_t *arena,
    const uint8_t *hash, int64_t created) {
    const pm_type_descriptor_t *d;
    if (s_arena == NULL || arena == NULL || hash == NULL) {
        return pm_types_nil();
    }
    d = pm_types_registry_find("pymergetic.metal.trust.Digest");
    if (d == NULL) {
        return pm_types_nil();
    }
    uint64_t w[4];
    memcpy(w, hash, sizeof(w));
    return pm_types_struct_new(arena, d,
        0x0A3Du, pm_types_i64(created),
        0x79CCu, pm_types_u64(w[0]),
        0x79CDu, pm_types_u64(w[1]),
        0x79CEu, pm_types_u64(w[2]),
        0x79CFu, pm_types_u64(w[3]),
        PM_TYPE_FIELD_END);
}

/* Constant-time Digest compare via the eq primitive (never early-out). */
int32_t pm_metal_trust_digest_eq(pm_type_value_t a, pm_type_value_t b) {
    const uint64_t *wa;
    const uint64_t *wb;
    if (pm_types_kind(a) != PM_TYPE_KIND_OBJ || pm_types_kind(b) != PM_TYPE_KIND_OBJ) {
        return 0;
    }
    wa = (const uint64_t *)pm_types_obj_data(a);
    wb = (const uint64_t *)pm_types_obj_data(b);
    if (wa == NULL || wb == NULL) {
        return 0;
    }
    uint8_t da[32];
    uint8_t db[32];
    memcpy(da, wa, 32);
    memcpy(db, wb, 32);
    /* w* carry the 32 hash bytes; `created` (offset 32) is metadata,
     * not identity — eq compares the hash only. */
    return pm_metal_trust_eq(da, db, 32);
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_init, pm_metal_trust_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_deinit, pm_metal_trust_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_sha256, pm_metal_trust_sha256, int32_t(const uint8_t *, uint32_t, uint8_t *));
PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_eq, pm_metal_trust_eq, int32_t(const uint8_t *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_digest_new, pm_metal_trust_digest_new, pm_type_value_t(pm_util_mem_arena_t *, const uint8_t *, int64_t));
PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_digest_eq, pm_metal_trust_digest_eq, int32_t(pm_type_value_t, pm_type_value_t));

PM_MOD_BOOT_C(pymergetic.metal.trust, pm_metal_trust_init, pm_metal_trust_deinit);

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

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_init, pm_metal_trust_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_deinit, pm_metal_trust_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_sha256, pm_metal_trust_sha256, int32_t(const uint8_t *, uint32_t, uint8_t *));
PM_MOD_EXPORT_C(pymergetic.metal.trust, pm_metal_trust_eq, pm_metal_trust_eq, int32_t(const uint8_t *, const uint8_t *, uint32_t));

PM_MOD_BOOT_C(pymergetic.metal.trust, pm_metal_trust_init, pm_metal_trust_deinit);

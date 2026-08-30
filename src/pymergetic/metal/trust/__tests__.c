/* pymergetic.metal.trust — SHA-256("abc"), constant-time eq, and the
 * trust.Digest domain type: registry round-trip + view layout. */
#include "pymergetic/metal/trust.h"
#include "pymergetic/metal/trust/__view__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.trust test: %s\n", why);
    return 1;
}

int32_t pm_metal_trust_tests(void) {
    static const uint8_t abc[] = { 'a', 'b', 'c' };
    static const uint8_t want[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22,
        0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00,
        0x15, 0xad,
    };
    uint8_t got[32];
    if (pm_metal_trust_sha256(abc, 3, got) != 0) {
        return fail("sha256");
    }
    if (pm_metal_trust_eq(got, want, 32) != 1) {
        return fail("digest");
    }
    if (pm_metal_trust_eq(got, want, 31) != 1) {
        return fail("prefix");
    }
    got[0] ^= 1u;
    if (pm_metal_trust_eq(got, want, 32) != 0) {
        return fail("neq");
    }
    /* Digest domain type: pack via digest_new, read back through the
     * generated zero-copy view, prove the registry field order, then
     * compare through the constant-time digest_eq. */
    {
        void *tb = malloc(1u << 16);
        pm_util_mem_arena_t *arena;
        pm_type_value_t d0, d1;
        const pm_type_descriptor_t *desc;
        const pm_trust_digest_view_t *view;
        uint16_t prev = 0;
        int i;
        if (tb == NULL) {
            return fail("digest test backing");
        }
        arena = pm_util_mem_arena_create(tb, 1u << 16);
        if (arena == NULL) {
            free(tb);
            return fail("digest test arena");
        }
        if (pm_metal_trust_init(arena) != 0) {
            free(tb);
            return fail("digest re-init");
        }
        desc = pm_types_registry_find("pymergetic.metal.trust.Digest");
        if (desc == NULL) {
            free(tb);
            return fail("digest descriptor missing");
        }
        /* Fields sorted by name_hash ascending — the DEFINE contract. */
        for (i = 0; i < desc->field_count; i++) {
            if (i > 0 && desc->fields[i].name_hash <= prev) {
                free(tb);
                return fail("digest field order");
            }
            prev = desc->fields[i].name_hash;
        }
        d0 = pm_metal_trust_digest_new(arena, want, 42);
        d1 = pm_metal_trust_digest_new(arena, want, 43);
        if (pm_types_kind(d0) != PM_TYPE_KIND_OBJ) {
            free(tb);
            return fail("digest new kind");
        }
        /* Zero-copy view over the packed instance data. */
        view = (const pm_trust_digest_view_t *)pm_types_obj_data(d0);
        if (view == NULL) {
            free(tb);
            return fail("digest data");
        }
        if (memcmp(&view->w0, want, 32) != 0) {
            free(tb);
            return fail("digest view bytes");
        }
        if (view->created != 42) {
            free(tb);
            return fail("digest view created");
        }
        /* eq compares the hash only, not `created`. */
        if (pm_metal_trust_digest_eq(d0, d1) != 1) {
            free(tb);
            return fail("digest eq");
        }
        d1 = pm_metal_trust_digest_new(arena, got, 43);
        if (pm_metal_trust_digest_eq(d0, d1) != 0) {
            free(tb);
            return fail("digest neq");
        }
        free(tb);
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.trust, tests, pm_metal_trust_tests);

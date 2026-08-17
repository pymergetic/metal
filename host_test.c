/* Host prove for metal. Cases register with PM_MOD_TEST_C; this binary
 * only boots and walks the registry — not a card list. */
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/registry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void teardown(pm_util_mem_arena_t *arena, void *backing) {
    pm_mod_boot_unwind();
    pm_util_mem_arena_destroy(arena);
    free(backing);
}

static int32_t late_boot_init(pm_util_mem_arena_t *a) {
    (void)a;
    return 0;
}

static void late_boot_deinit(void) {}

static int fqn_is_metal(const uint8_t *buf, uint32_t len) {
    static const char pfx[] = "pymergetic.metal";
    uint32_t n = (uint32_t)(sizeof(pfx) - 1u);
    if (len < n) {
        return 0;
    }
    if (memcmp(buf, pfx, n) != 0) {
        return 0;
    }
    return len == n || buf[n] == '.';
}

static int32_t run_registered_tests(void) {
    uint32_t n = pm_wasmmod_registry_module_count();
    uint32_t i;
    uint32_t ran = 0;
    int32_t fails = 0;
    for (i = 0; i < n; i++) {
        uint8_t buf[256];
        uint32_t len = sizeof(buf);
        uint32_t tc;
        if (pm_wasmmod_registry_module_at(i, buf, &len) == 0 || len == 0) {
            continue;
        }
        if (!fqn_is_metal(buf, len)) {
            continue;
        }
        tc = pm_wasmmod_registry_test_count(buf, len);
        if (tc == 0u) {
            continue;
        }
        ran += tc;
        fails += pm_wasmmod_registry_test_run_all(buf, len);
    }
    if (ran == 0u) {
        fprintf(stderr, "metal.host: no registered tests\n");
        return 1;
    }
    if (fails != 0) {
        fprintf(stderr, "metal.host: %d case(s) failed (%u ran)\n", (int)fails, (unsigned)ran);
        return 1;
    }
    printf("metal host tests ok (%u cases)\n", (unsigned)ran);
    return 0;
}

int main(void) {
    enum { SPAN = 8u * 1024u * 1024u };
    void *backing = malloc(SPAN);
    pm_util_mem_arena_t *arena;
    int32_t st;
    if (backing == NULL) {
        fprintf(stderr, "metal.host: malloc\n");
        return 1;
    }
    arena = pm_util_mem_arena_create(backing, SPAN);
    if (arena == NULL) {
        fprintf(stderr, "metal.host: arena_create\n");
        free(backing);
        return 1;
    }
    if (pm_mod_boot_run(arena) != 0) {
        fprintf(stderr, "metal.host: boot\n");
        teardown(arena, backing);
        return 1;
    }
    {
        static const pm_mod_boot_t late = {
            "pymergetic.test.lateboot", late_boot_init, late_boot_deinit, NULL
        };
        if (pm_mod_boot_add(&late) != 0) {
            fprintf(stderr, "metal.host: late boot add\n");
            teardown(arena, backing);
            return 1;
        }
    }
    st = run_registered_tests();
    teardown(arena, backing);
    return st != 0 ? 1 : 0;
}

/* pymergetic.metal.boot.externals — walk PM_METAL_EXTERNAL_C records. */
#include "pymergetic/metal/boot/externals/__exports__.h"

#include "pymergetic/metal/boot/externals/__types__.h"
#include "pymergetic/util/mem/__types__.h"

#include <stdint.h>
#include <string.h>

extern const pm_metal_external_t __start_pm_metal_externals[] __attribute__((weak));
extern const pm_metal_external_t __stop_pm_metal_externals[] __attribute__((weak));

static const pm_metal_external_t *s_extra[PM_METAL_EXTERNAL_MAX];
static uint32_t s_nextra;

static int name_eq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

static int ver_ok(const char *ver) {
    if (ver == NULL || ver[0] == 0) {
        return 0;
    }
    /* "ok" is a status token in the tree, not a version. */
    return !(ver[0] == 'o' && ver[1] == 'k' && ver[2] == 0);
}

static void sort_by_name(const pm_metal_external_t **recs, uint32_t n) {
    uint32_t i;
    for (i = 1; i < n; i++) {
        const pm_metal_external_t *x = recs[i];
        uint32_t j = i;
        while (j > 0 && strcmp(recs[j - 1u]->name, x->name) > 0) {
            recs[j] = recs[j - 1u];
            j--;
        }
        recs[j] = x;
    }
}

static uint32_t collect(const pm_metal_external_t **out, uint32_t cap) {
    uint32_t w = 0;
    uint32_t i;
#if !defined(__wasm__)
    const pm_metal_external_t *recs = __start_pm_metal_externals;
    if ((uintptr_t)(const void *)recs != 0
        && (uintptr_t)(const void *)__stop_pm_metal_externals != 0) {
        uint32_t n = (uint32_t)(__stop_pm_metal_externals - __start_pm_metal_externals);
        uint32_t k;
        for (k = 0; k < n && w < cap; k++) {
            uint32_t j;
            int seen = 0;
            if (recs[k].name == NULL || recs[k].name[0] == 0 || !ver_ok(recs[k].version)) {
                continue;
            }
            for (j = 0; j < w; j++) {
                if (out[j] == &recs[k] || name_eq(out[j]->name, recs[k].name)) {
                    seen = 1;
                    break;
                }
            }
            if (!seen) {
                out[w++] = &recs[k];
            }
        }
    }
#endif
    for (i = 0; i < s_nextra && w < cap; i++) {
        uint32_t j;
        int seen = 0;
        for (j = 0; j < w; j++) {
            if (out[j] == s_extra[i] || name_eq(out[j]->name, s_extra[i]->name)) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            out[w++] = s_extra[i];
        }
    }
    sort_by_name(out, w);
    return w;
}

int32_t pm_metal_external_add(const pm_metal_external_t *rec) {
    uint32_t i;
    if (rec == NULL || rec->name == NULL || rec->name[0] == 0 || !ver_ok(rec->version)) {
        return -1;
    }
    for (i = 0; i < s_nextra; i++) {
        if (name_eq(s_extra[i]->name, rec->name)) {
            if (name_eq(s_extra[i]->version, rec->version)) {
                return 0;
            }
            return -1;
        }
    }
    if (s_nextra >= PM_METAL_EXTERNAL_MAX) {
        return -1;
    }
    s_extra[s_nextra++] = rec;
    return 0;
}

uint32_t pm_metal_external_count(void) {
    const pm_metal_external_t *recs[PM_METAL_EXTERNAL_MAX];
    return collect(recs, PM_METAL_EXTERNAL_MAX);
}

const char *pm_metal_external_name(uint32_t i) {
    const pm_metal_external_t *recs[PM_METAL_EXTERNAL_MAX];
    uint32_t n = collect(recs, PM_METAL_EXTERNAL_MAX);
    if (i >= n) {
        return NULL;
    }
    return recs[i]->name;
}

const char *pm_metal_external_version(uint32_t i) {
    const pm_metal_external_t *recs[PM_METAL_EXTERNAL_MAX];
    uint32_t n = collect(recs, PM_METAL_EXTERNAL_MAX);
    if (i >= n) {
        return NULL;
    }
    return recs[i]->version;
}

static int32_t pm_metal_boot_externals_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    return 0;
}

static void pm_metal_boot_externals_deinit(void) {}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.boot.externals, pm_metal_external_count, pm_metal_external_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.boot.externals, pm_metal_external_name, pm_metal_external_name, const char *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.boot.externals, pm_metal_external_version, pm_metal_external_version, const char *(uint32_t));

PM_MOD_BOOT_C(pymergetic.metal.boot.externals, pm_metal_boot_externals_init, pm_metal_boot_externals_deinit);

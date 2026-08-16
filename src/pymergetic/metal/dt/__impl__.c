/* pymergetic.metal.dt — live inventory. Same compatible may appear N times. */
#include "pymergetic/metal/dt/__exports__.h"

#include <string.h>

#define PM_METAL_DT_MAX 128u
#define PM_METAL_DT_COMPAT 32u

struct pm_metal_dt_node {
    uint32_t used;
    int32_t class;
    int32_t bus;
    uint32_t loc[4];
    uint32_t unit;
    char compat[PM_METAL_DT_COMPAT];
};

static pm_util_mem_arena_t *s_arena;
static struct pm_metal_dt_node s_node[PM_METAL_DT_MAX];
static uint32_t s_class_n[16];

static int loc_eq(const uint32_t *a, uint32_t b0, uint32_t b1, uint32_t b2, uint32_t b3) {
    return a[0] == b0 && a[1] == b1 && a[2] == b2 && a[3] == b3;
}

int32_t pm_metal_dt_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_node, 0, sizeof(s_node));
    memset(s_class_n, 0, sizeof(s_class_n));
    return 0;
}

void pm_metal_dt_deinit(void) {
    memset(s_node, 0, sizeof(s_node));
    memset(s_class_n, 0, sizeof(s_class_n));
    s_arena = NULL;
}

int32_t pm_metal_dt_add(int32_t class, const char *compat, int32_t bus, uint32_t loc0,
    uint32_t loc1, uint32_t loc2, uint32_t loc3) {
    uint32_t i;
    uint32_t n;
    if (s_arena == NULL || class <= 0 || class >= 16 || compat == NULL || compat[0] == 0) {
        return -1;
    }
    for (i = 0; i < PM_METAL_DT_MAX; i++) {
        if (s_node[i].used && s_node[i].class == class && s_node[i].bus == bus
            && loc_eq(s_node[i].loc, loc0, loc1, loc2, loc3)
            && strncmp(s_node[i].compat, compat, PM_METAL_DT_COMPAT) == 0) {
            return (int32_t)i;
        }
    }
    for (i = 0; i < PM_METAL_DT_MAX; i++) {
        if (!s_node[i].used) {
            memset(&s_node[i], 0, sizeof(s_node[i]));
            s_node[i].used = 1;
            s_node[i].class = class;
            s_node[i].bus = bus;
            s_node[i].loc[0] = loc0;
            s_node[i].loc[1] = loc1;
            s_node[i].loc[2] = loc2;
            s_node[i].loc[3] = loc3;
            n = 0;
            while (compat[n] != 0 && n + 1u < PM_METAL_DT_COMPAT) {
                s_node[i].compat[n] = compat[n];
                n++;
            }
            s_node[i].unit = s_class_n[class];
            s_class_n[class]++;
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t pm_metal_dt_unbind(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_DT_MAX || !s_node[id].used) {
        return -1;
    }
    if (s_class_n[s_node[id].class] != 0) {
        s_class_n[s_node[id].class]--;
    }
    memset(&s_node[id], 0, sizeof(s_node[id]));
    return 0;
}

int32_t pm_metal_dt_count(void) {
    uint32_t i;
    int32_t n = 0;
    for (i = 0; i < PM_METAL_DT_MAX; i++) {
        if (s_node[i].used) {
            n++;
        }
    }
    return n;
}

int32_t pm_metal_dt_count_class(int32_t class) {
    if (class <= 0 || class >= 16) {
        return -1;
    }
    return (int32_t)s_class_n[class];
}

int32_t pm_metal_dt_by_class(int32_t class, int32_t index) {
    uint32_t i;
    int32_t seen = 0;
    if (class <= 0 || index < 0) {
        return -1;
    }
    for (i = 0; i < PM_METAL_DT_MAX; i++) {
        if (!s_node[i].used || s_node[i].class != class) {
            continue;
        }
        if (seen == index) {
            return (int32_t)i;
        }
        seen++;
    }
    return -1;
}

int32_t pm_metal_dt_class(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_DT_MAX || !s_node[id].used) {
        return -1;
    }
    return s_node[id].class;
}

const char *pm_metal_dt_compat(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_DT_MAX || !s_node[id].used) {
        return NULL;
    }
    return s_node[id].compat;
}

int32_t pm_metal_dt_unit(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_DT_MAX || !s_node[id].used) {
        return -1;
    }
    return (int32_t)s_node[id].unit;
}

int32_t pm_metal_dt_bus(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_DT_MAX || !s_node[id].used) {
        return -1;
    }
    return s_node[id].bus;
}

int32_t pm_metal_dt_loc(int32_t id, uint32_t idx, uint32_t *out) {
    if (id < 0 || (uint32_t)id >= PM_METAL_DT_MAX || !s_node[id].used || idx > 3u || out == NULL) {
        return -1;
    }
    *out = s_node[id].loc[idx];
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_init, pm_metal_dt_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_deinit, pm_metal_dt_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_add, pm_metal_dt_add, int32_t(int32_t, const char *, int32_t, uint32_t, uint32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_unbind, pm_metal_dt_unbind, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_count, pm_metal_dt_count, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_count_class, pm_metal_dt_count_class, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_by_class, pm_metal_dt_by_class, int32_t(int32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_class, pm_metal_dt_class, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_compat, pm_metal_dt_compat, const char *(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_unit, pm_metal_dt_unit, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_bus, pm_metal_dt_bus, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.dt, pm_metal_dt_loc, pm_metal_dt_loc, int32_t(int32_t, uint32_t, uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.dt, pm_metal_dt_init, pm_metal_dt_deinit);

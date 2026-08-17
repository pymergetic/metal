/* pymergetic.metal.fs — in-memory files. Firmware cannot fopen. */
#include "pymergetic/metal/fs/__exports__.h"

#include <string.h>

#define FILE_MAX 8
#define NAME_MAX 64
#define FILE_BYTES 512

struct file {
    uint32_t used;
    char name[NAME_MAX];
    uint8_t data[FILE_BYTES];
    uint32_t len;
};

static pm_util_mem_arena_t *s_arena;
static struct file s_file[FILE_MAX];

static uint32_t name_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != 0 && b[i] != 0 && i < NAME_MAX; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return a[i] == 0 && b[i] == 0;
}

static int32_t find(const char *path) {
    uint32_t i;
    if (path == NULL || path[0] == 0) {
        return -1;
    }
    for (i = 0; i < FILE_MAX; i++) {
        if (s_file[i].used && name_eq(s_file[i].name, path)) {
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t pm_metal_fs_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_file, 0, sizeof(s_file));
    return 0;
}

void pm_metal_fs_deinit(void) {
    memset(s_file, 0, sizeof(s_file));
    s_arena = NULL;
}

int32_t pm_metal_fs_add(const char *path, const uint8_t *data, uint32_t len) {
    uint32_t i;
    uint32_t n;
    if (s_arena == NULL || path == NULL || path[0] == 0 || data == NULL || len == 0
        || len > FILE_BYTES) {
        return -1;
    }
    if (find(path) >= 0) {
        return -1;
    }
    for (i = 0; i < FILE_MAX; i++) {
        if (s_file[i].used) {
            continue;
        }
        n = 0;
        while (path[n] != 0 && n + 1u < NAME_MAX) {
            s_file[i].name[n] = path[n];
            n++;
        }
        if (path[n] != 0) {
            return -1;
        }
        s_file[i].name[n] = 0;
        memcpy(s_file[i].data, data, len);
        s_file[i].len = len;
        s_file[i].used = 1;
        return (int32_t)i;
    }
    return -1;
}

int32_t pm_metal_fs_stat(const char *path, uint32_t *len) {
    int32_t id = find(path);
    if (id < 0) {
        return -1;
    }
    if (len != NULL) {
        *len = s_file[id].len;
    }
    return 0;
}

int32_t pm_metal_fs_read(const char *path, uint8_t *out, uint32_t *len) {
    int32_t id = find(path);
    uint32_t n;
    if (id < 0 || out == NULL || len == NULL) {
        return -1;
    }
    n = s_file[id].len;
    if (*len < n) {
        n = *len;
    }
    memcpy(out, s_file[id].data, n);
    *len = n;
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_init, pm_metal_fs_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_deinit, pm_metal_fs_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_add, pm_metal_fs_add, int32_t(const char *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_stat, pm_metal_fs_stat, int32_t(const char *, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_read, pm_metal_fs_read, int32_t(const char *, uint8_t *, uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.fs, pm_metal_fs_init, pm_metal_fs_deinit);

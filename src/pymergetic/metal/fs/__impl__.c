/* pymergetic.metal.fs — arena files (embed first; no POSIX fopen). */
#include "pymergetic/metal/fs/__exports__.h"

#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/util/mem.h"

#include <string.h>

struct file {
    struct file *next;
    char *name;
    uint8_t *data;
    uint32_t len;
    uint32_t id;
    uint32_t used;
};

static pm_util_mem_arena_t *s_arena;
static struct file *s_head;
static uint32_t s_next_id;

int32_t pm_metal_fs_import_blk(int32_t);
int32_t pm_metal_fs_reserve(const char *, uint32_t, uint8_t **);
int32_t pm_metal_fs_drop(const char *);
void pm_metal_fs_fat_bind(pm_util_mem_arena_t *);
void pm_metal_fs_fat_reset(void);
int32_t pm_metal_fs_fat_stat(const char *, uint32_t *);
int32_t pm_metal_fs_fat_read(const char *, uint8_t *, uint32_t *);

static uint32_t name_eq(const char *a, const char *b) {
    uint32_t i;
    if (a == NULL || b == NULL) {
        return 0;
    }
    for (i = 0; a[i] != 0 && b[i] != 0; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return a[i] == 0 && b[i] == 0;
}

static uint32_t name_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n;
}

static struct file *find_file(const char *path) {
    struct file *f;
    if (path == NULL || path[0] == 0) {
        return NULL;
    }
    for (f = s_head; f != NULL; f = f->next) {
        if (f->used && name_eq(f->name, path)) {
            return f;
        }
    }
    return NULL;
}

int32_t pm_metal_fs_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_head = NULL;
    s_next_id = 0;
    pm_metal_fs_fat_bind(arena);
    return 0;
}

void pm_metal_fs_deinit(void) {
    s_head = NULL;
    s_next_id = 0;
    pm_metal_fs_fat_reset();
    s_arena = NULL;
}

int32_t pm_metal_fs_reserve(const char *path, uint32_t len, uint8_t **out) {
    struct file *f;
    uint32_t n;
    char *name;
    uint8_t *buf;
    if (s_arena == NULL || path == NULL || path[0] == 0 || out == NULL || len == 0) {
        return -1;
    }
    if (find_file(path) != NULL) {
        return -1;
    }
    n = name_len(path);
    f = (struct file *)pm_util_mem_alloc(s_arena, sizeof(*f));
    name = (char *)pm_util_mem_alloc(s_arena, n + 1u);
    buf = (uint8_t *)pm_util_mem_alloc(s_arena, len);
    if (f == NULL || name == NULL || buf == NULL) {
        return -1;
    }
    memcpy(name, path, n + 1u);
    f->next = s_head;
    f->name = name;
    f->data = buf;
    f->len = len;
    f->id = s_next_id++;
    f->used = 1;
    s_head = f;
    *out = buf;
    return (int32_t)f->id;
}

int32_t pm_metal_fs_drop(const char *path) {
    struct file *f = find_file(path);
    if (f == NULL) {
        return -1;
    }
    f->used = 0;
    return 0;
}

int32_t pm_metal_fs_add(const char *path, const uint8_t *data, uint32_t len) {
    uint8_t *buf;
    int32_t id;
    if (data == NULL) {
        return -1;
    }
    id = pm_metal_fs_reserve(path, len, &buf);
    if (id < 0) {
        return -1;
    }
    memcpy(buf, data, len);
    return id;
}

int32_t pm_metal_fs_stat(const char *path, uint32_t *len) {
    struct file *f = find_file(path);
    if (f != NULL) {
        if (len != NULL) {
            *len = f->len;
        }
        return 0;
    }
    return pm_metal_fs_fat_stat(path, len);
}

int32_t pm_metal_fs_read(const char *path, uint8_t *out, uint32_t *len) {
    struct file *f = find_file(path);
    uint32_t n;
    if (out == NULL || len == NULL) {
        return -1;
    }
    if (f != NULL) {
        n = f->len;
        if (*len < n) {
            n = *len;
        }
        memcpy(out, f->data, n);
        *len = n;
        return 0;
    }
    return pm_metal_fs_fat_read(path, out, len);
}

int32_t pm_metal_fs_up(void) {
    static const uint8_t hello[] = "metal fs\n";
    uint32_t len = 0;
    int32_t i;
    if (s_arena == NULL) {
        return -1;
    }
    if (pm_metal_fs_stat("/metal/hello.txt", &len) != 0 || len == 0u) {
        if (pm_metal_fs_add("/metal/hello.txt", hello, (uint32_t)(sizeof(hello) - 1u)) < 0) {
            return -1;
        }
    }
    for (i = 0; i < 8; i++) {
        if (pm_metal_drivers_blk_ready(i)) {
            (void)pm_metal_fs_import_blk(i);
        }
    }
    {
        static const char *const live[] = {
            "/esp/HI.TXT",
            "/esp/SUB/X.TXT",
            "/esp/hello.txt",
            "/esp/EFI/BOOT/BOOTX64.EFI",
        };
        uint8_t buf[8];
        uint32_t n;
        uint32_t k;
        for (k = 0; k < sizeof(live) / sizeof(live[0]); k++) {
            n = 0;
            if (pm_metal_fs_stat(live[k], &n) != 0 || n == 0u) {
                continue;
            }
            n = sizeof(buf);
            if (pm_metal_fs_read(live[k], buf, &n) != 0 || n == 0u) {
                return -1;
            }
            break;
        }
    }
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_init, pm_metal_fs_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_deinit, pm_metal_fs_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_add, pm_metal_fs_add, int32_t(const char *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_stat, pm_metal_fs_stat, int32_t(const char *, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_read, pm_metal_fs_read, int32_t(const char *, uint8_t *, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_import_blk, pm_metal_fs_import_blk, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.fs, pm_metal_fs_up, pm_metal_fs_up, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.fs, pm_metal_fs_init, pm_metal_fs_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.fs, pymergetic.metal.drivers.blk);

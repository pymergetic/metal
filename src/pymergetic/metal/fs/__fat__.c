/* pymergetic.metal.fs — live FAT12/16/32 on a blk (LFN + dirs; bodies stay on disk). */
#include "pymergetic/metal/fs/__exports__.h"

#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#define FAT_SEC 512u
#define FAT_PATH_MAX 256u

struct fat_vol {
    struct fat_vol *next;
    int32_t blk_h;
    uint32_t reserved;
    uint32_t fat_bits;
    uint32_t first_data;
    uint32_t spc;
    uint32_t tot;
};

struct fat_ent {
    struct fat_ent *next;
    struct fat_vol *vol;
    char *path;
    uint32_t clus;
    uint32_t size;
    uint32_t is_dir;
};

static pm_util_mem_arena_t *s_arena;
static struct fat_vol *s_vol;
static struct fat_ent *s_ent;

void pm_metal_fs_fat_bind(pm_util_mem_arena_t *arena) {
    s_arena = arena;
}

void pm_metal_fs_fat_reset(void) {
    s_vol = NULL;
    s_ent = NULL;
    s_arena = NULL;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

static int32_t read_sec(int32_t h, uint32_t lba, uint8_t *buf) {
    return pm_metal_drivers_blk_read(h, (uint64_t)lba, buf, 1);
}

static uint32_t fat_entry(int32_t h, uint32_t reserved, uint32_t fat_bits, uint32_t clus) {
    uint8_t sec[FAT_SEC];
    uint32_t off;
    uint32_t lba;
    uint32_t o;
    if (fat_bits == 12u) {
        off = clus + (clus / 2u);
    } else if (fat_bits == 16u) {
        off = clus * 2u;
    } else {
        off = clus * 4u;
    }
    lba = reserved + (off / FAT_SEC);
    o = off % FAT_SEC;
    if (read_sec(h, lba, sec) != 0) {
        return 0;
    }
    if (fat_bits == 12u) {
        uint32_t raw;
        if (o + 1u >= FAT_SEC) {
            uint8_t nxt[FAT_SEC];
            if (read_sec(h, lba + 1u, nxt) != 0) {
                return 0;
            }
            raw = (uint32_t)sec[o] | ((uint32_t)nxt[0] << 8);
        } else {
            raw = (uint32_t)sec[o] | ((uint32_t)sec[o + 1u] << 8);
        }
        return (clus & 1u) != 0u ? (raw >> 4) & 0xfffu : raw & 0xfffu;
    }
    if (fat_bits == 16u) {
        if (o + 1u >= FAT_SEC) {
            return 0;
        }
        return le16(sec + o);
    }
    if (o + 3u >= FAT_SEC) {
        return 0;
    }
    return le32(sec + o) & 0x0fffffffu;
}

static uint32_t eoc(uint32_t fat_bits) {
    if (fat_bits == 12u) {
        return 0xff8u;
    }
    if (fat_bits == 16u) {
        return 0xfff8u;
    }
    return 0x0ffffff8u;
}

static uint32_t clus_lba(uint32_t first_data, uint32_t spc, uint32_t clus) {
    return first_data + (clus - 2u) * spc;
}

static int32_t read_file(struct fat_vol *v, uint32_t start, uint32_t size, uint8_t *out,
    uint32_t cap, uint32_t *got_out) {
    uint32_t clus = start;
    uint32_t got = 0;
    uint32_t n;
    uint32_t i;
    uint8_t sec[FAT_SEC];
    uint32_t clus_bytes;
    uint32_t need;
    uint32_t want = size;
    if (v == NULL || start < 2u || v->spc == 0u || out == NULL) {
        return -1;
    }
    if (want > cap) {
        want = cap;
    }
    if (want == 0u) {
        if (got_out != NULL) {
            *got_out = 0;
        }
        return 0;
    }
    clus_bytes = v->spc * FAT_SEC;
    need = (size + clus_bytes - 1u) / clus_bytes;
    if (need == 0u) {
        need = 1u;
    }
    for (n = 0; n < need && clus >= 2u && clus < eoc(v->fat_bits) && got < want; n++) {
        uint32_t lba = clus_lba(v->first_data, v->spc, clus);
        for (i = 0; i < v->spc && got < want; i++) {
            uint32_t chunk;
            if (read_sec(v->blk_h, lba + i, sec) != 0) {
                return -1;
            }
            chunk = want - got;
            if (chunk > FAT_SEC) {
                chunk = FAT_SEC;
            }
            memcpy(out + got, sec, chunk);
            got += chunk;
        }
        clus = fat_entry(v->blk_h, v->reserved, v->fat_bits, clus);
        if (clus == 0u) {
            return -1;
        }
    }
    if (got_out != NULL) {
        *got_out = got;
    }
    return got > 0u ? 0 : -1;
}

static void put_utf16(char *dst, uint32_t *n, uint32_t cap, uint16_t u) {
    if (u == 0 || u == 0xffffu || *n + 1u >= cap) {
        return;
    }
    if (u < 0x80u) {
        dst[(*n)++] = (char)u;
        return;
    }
    if (u < 0x800u && *n + 2u < cap) {
        dst[(*n)++] = (char)(0xc0u | (u >> 6));
        dst[(*n)++] = (char)(0x80u | (u & 0x3fu));
        return;
    }
    if (*n + 3u < cap) {
        dst[(*n)++] = (char)(0xe0u | (u >> 12));
        dst[(*n)++] = (char)(0x80u | ((u >> 6) & 0x3fu));
        dst[(*n)++] = (char)(0x80u | (u & 0x3fu));
    }
}

static void lfn_slot(uint16_t *ucs, uint32_t cap, const uint8_t *e) {
    static const uint8_t pos[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    uint32_t seq = (uint32_t)(e[0] & 0x1fu);
    uint32_t base;
    uint32_t i;
    if (seq == 0u || seq > 20u) {
        return;
    }
    base = (seq - 1u) * 13u;
    for (i = 0; i < 13u && base + i < cap; i++) {
        ucs[base + i] = le16(e + pos[i]);
    }
}

static void lfn_utf8(char *dst, uint32_t cap, const uint16_t *ucs, uint32_t ucap) {
    uint32_t n = 0;
    uint32_t i;
    dst[0] = 0;
    for (i = 0; i < ucap && ucs[i] != 0 && ucs[i] != 0xffffu; i++) {
        put_utf16(dst, &n, cap, ucs[i]);
    }
    dst[n] = 0;
}

static void name_83(const uint8_t *e, char *name, uint32_t cap) {
    uint32_t n = 0;
    uint32_t i;
    if (cap < 13u) {
        name[0] = 0;
        return;
    }
    for (i = 0; i < 8u && e[i] != ' ' && n + 1u < cap; i++) {
        name[n++] = (char)e[i];
    }
    if (e[8] != ' ' && n + 2u < cap) {
        name[n++] = '.';
        for (i = 8; i < 11u && e[i] != ' ' && n + 1u < cap; i++) {
            name[n++] = (char)e[i];
        }
    }
    name[n] = 0;
}

static int32_t join_path(char *dst, uint32_t cap, const char *dir, const char *leaf) {
    uint32_t n = 0;
    uint32_t i;
    if (dir == NULL || leaf == NULL || leaf[0] == 0) {
        return -1;
    }
    while (dir[n] != 0 && n + 1u < cap) {
        dst[n] = dir[n];
        n++;
    }
    if (n == 0 || dst[n - 1u] != '/') {
        if (n + 1u >= cap) {
            return -1;
        }
        dst[n++] = '/';
    }
    for (i = 0; leaf[i] != 0 && n + 1u < cap; i++) {
        dst[n++] = leaf[i];
    }
    if (leaf[i] != 0) {
        return -1;
    }
    dst[n] = 0;
    return 0;
}

static struct fat_ent *find_ent(const char *path) {
    struct fat_ent *e;
    if (path == NULL || path[0] == 0) {
        return NULL;
    }
    for (e = s_ent; e != NULL; e = e->next) {
        if (name_eq(e->path, path)) {
            return e;
        }
    }
    return NULL;
}

static int32_t add_ent(struct fat_vol *v, const char *path, uint32_t clus, uint32_t size,
    uint32_t is_dir) {
    struct fat_ent *e;
    uint32_t n;
    char *name;
    if (s_arena == NULL || v == NULL || path == NULL || path[0] == 0 || find_ent(path) != NULL) {
        return 0;
    }
    n = name_len(path);
    e = (struct fat_ent *)pm_util_mem_alloc(s_arena, sizeof(*e));
    name = (char *)pm_util_mem_alloc(s_arena, n + 1u);
    if (e == NULL || name == NULL) {
        return 0;
    }
    memcpy(name, path, n + 1u);
    e->next = s_ent;
    e->vol = v;
    e->path = name;
    e->clus = clus;
    e->size = size;
    e->is_dir = is_dir;
    s_ent = e;
    return 1;
}

static int32_t walk_dir(struct fat_vol *v, uint32_t start, uint32_t root_secs, const char *prefix,
    int32_t *added);

static int32_t take_ent(struct fat_vol *v, const uint8_t *e, uint16_t *ucs, uint32_t *have_lfn,
    const char *prefix, int32_t *added) {
    char leaf[FAT_PATH_MAX];
    char path[FAT_PATH_MAX];
    uint32_t size;
    uint32_t clus;
    uint8_t attr;
    if (e[0] == 0 || e[0] == 0xe5) {
        *have_lfn = 0;
        return e[0] == 0 ? -2 : 0;
    }
    attr = e[11];
    if (attr == 0x0fu) {
        if ((e[0] & 0x40u) != 0) {
            memset(ucs, 0, 260u * sizeof(uint16_t));
        }
        lfn_slot(ucs, 260u, e);
        *have_lfn = 1;
        return 0;
    }
    if ((attr & 0x08u) != 0) {
        *have_lfn = 0;
        return 0;
    }
    size = le32(e + 28);
    clus = (uint32_t)le16(e + 26);
    if (v->fat_bits == 32u) {
        clus |= ((uint32_t)le16(e + 20) << 16);
    }
    if (*have_lfn) {
        lfn_utf8(leaf, sizeof(leaf), ucs, 260u);
    } else {
        name_83(e, leaf, sizeof(leaf));
    }
    *have_lfn = 0;
    if (leaf[0] == 0 || (leaf[0] == '.' && (leaf[1] == 0 || (leaf[1] == '.' && leaf[2] == 0)))) {
        return 0;
    }
    if (join_path(path, sizeof(path), prefix, leaf) != 0) {
        return 0;
    }
    if ((attr & 0x10u) != 0) {
        if (clus < 2u) {
            return 0;
        }
        if (add_ent(v, path, clus, 0, 1)) {
            (*added)++;
        }
        return walk_dir(v, clus, 0, path, added);
    }
    if (clus < 2u && size != 0u) {
        return 0;
    }
    if (add_ent(v, path, clus, size, 0)) {
        (*added)++;
    }
    return 0;
}

static int32_t walk_dir_sec(struct fat_vol *v, const uint8_t *sec, uint16_t *ucs, uint32_t *have_lfn,
    const char *prefix, int32_t *added) {
    uint32_t i;
    for (i = 0; i < FAT_SEC; i += 32u) {
        int32_t st = take_ent(v, sec + i, ucs, have_lfn, prefix, added);
        if (st == -2) {
            return 1;
        }
    }
    return 0;
}

static int32_t walk_dir(struct fat_vol *v, uint32_t start, uint32_t root_secs, const char *prefix,
    int32_t *added) {
    uint8_t sec[FAT_SEC];
    uint16_t ucs[260];
    uint32_t have_lfn = 0;
    uint32_t i;
    memset(ucs, 0, sizeof(ucs));
    if (v == NULL || prefix == NULL) {
        return -1;
    }
    if (root_secs != 0u) {
        uint32_t lba0 = v->first_data - root_secs;
        for (i = 0; i < root_secs; i++) {
            if (read_sec(v->blk_h, lba0 + i, sec) != 0) {
                return -1;
            }
            if (walk_dir_sec(v, sec, ucs, &have_lfn, prefix, added)) {
                return 0;
            }
        }
        return 0;
    }
    {
        uint32_t clus = start;
        uint32_t n;
        uint32_t max_clus = (v->tot - v->first_data) / v->spc;
        if (clus < 2u || max_clus == 0u) {
            return -1;
        }
        for (n = 0; n < max_clus && clus >= 2u && clus < eoc(v->fat_bits); n++) {
            uint32_t lba = clus_lba(v->first_data, v->spc, clus);
            uint32_t s;
            for (s = 0; s < v->spc; s++) {
                if (read_sec(v->blk_h, lba + s, sec) != 0) {
                    return -1;
                }
                if (walk_dir_sec(v, sec, ucs, &have_lfn, prefix, added)) {
                    return 0;
                }
            }
            clus = fat_entry(v->blk_h, v->reserved, v->fat_bits, clus);
            if (clus == 0u) {
                break;
            }
        }
    }
    return 0;
}

int32_t pm_metal_fs_fat_stat(const char *path, uint32_t *len) {
    struct fat_ent *e = find_ent(path);
    if (e == NULL || e->is_dir) {
        return -1;
    }
    if (len != NULL) {
        *len = e->size;
    }
    return 0;
}

int32_t pm_metal_fs_fat_read(const char *path, uint8_t *out, uint32_t *len) {
    struct fat_ent *e = find_ent(path);
    uint32_t got = 0;
    if (e == NULL || e->is_dir || out == NULL || len == NULL) {
        return -1;
    }
    if (e->size == 0u) {
        *len = 0;
        return 0;
    }
    if (read_file(e->vol, e->clus, e->size, out, *len, &got) != 0) {
        return -1;
    }
    *len = got;
    return 0;
}

int32_t pm_metal_fs_import_blk(int32_t blk_h) {
    uint8_t sec[FAT_SEC];
    uint32_t bps;
    uint32_t spc;
    uint32_t reserved;
    uint32_t nfats;
    uint32_t root_ents;
    uint32_t tot16;
    uint32_t fat16;
    uint32_t tot;
    uint32_t fat_sz;
    uint32_t root_secs;
    uint32_t first_data;
    uint32_t fat_bits;
    struct fat_vol *v;
    int32_t added = 0;
    if (s_arena == NULL || blk_h < 0 || !pm_metal_drivers_blk_ready(blk_h)) {
        return -1;
    }
    if (read_sec(blk_h, 0, sec) != 0 || sec[510] != 0x55u || sec[511] != 0xaau) {
        return -1;
    }
    bps = le16(sec + 11);
    spc = sec[13];
    reserved = le16(sec + 14);
    nfats = sec[16];
    root_ents = le16(sec + 17);
    tot16 = le16(sec + 19);
    fat16 = le16(sec + 22);
    tot = tot16 != 0u ? tot16 : le32(sec + 32);
    if (bps != FAT_SEC || spc == 0u || (spc & (spc - 1u)) != 0u || spc > 64u || reserved == 0u
        || nfats == 0u || nfats > 2u || tot < 4u) {
        return -1;
    }
    if (fat16 != 0u) {
        fat_sz = fat16;
        root_secs = ((root_ents * 32u) + (FAT_SEC - 1u)) / FAT_SEC;
        fat_bits = 16u;
    } else {
        fat_sz = le32(sec + 36);
        root_secs = 0;
        fat_bits = 32u;
    }
    if (fat_sz == 0u) {
        return -1;
    }
    first_data = reserved + nfats * fat_sz + root_secs;
    if (first_data >= tot) {
        return -1;
    }
    if (fat_bits == 16u) {
        uint32_t nclus = (tot - first_data) / spc;
        if (nclus < 4085u) {
            fat_bits = 12u;
        }
    }
    v = (struct fat_vol *)pm_util_mem_alloc(s_arena, sizeof(*v));
    if (v == NULL) {
        return -1;
    }
    v->next = s_vol;
    v->blk_h = blk_h;
    v->reserved = reserved;
    v->fat_bits = fat_bits;
    v->first_data = first_data;
    v->spc = spc;
    v->tot = tot;
    s_vol = v;
    if (fat_bits != 32u) {
        if (walk_dir(v, 0, root_secs, "/esp", &added) != 0) {
            return added > 0 ? added : -1;
        }
        return added;
    }
    {
        uint32_t clus = le32(sec + 44);
        if (walk_dir(v, clus, 0, "/esp", &added) != 0) {
            return added > 0 ? added : -1;
        }
    }
    return added;
}

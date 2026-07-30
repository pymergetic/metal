/*
 * littlefs volume glue — in-memory block device + format/seed/mount.
 * Metal C dialect (stdint); lfs_* lives in vendor/lfs.c.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "vendor/lfs.h"

#define PM_LFS_MAX_VOL 8u
#define PM_LFS_BLOCK 4096u
#define PM_LFS_CACHE 256u
#define PM_LFS_LOOK 16u
#define PM_LFS_READ 16u
#define PM_LFS_PROG 16u

typedef struct {
  uint8_t *buf;
  size_t   len;
} pm_lfs_bd_t;

typedef struct {
  int              used;
  pm_lfs_bd_t      bd;
  struct lfs_config cfg;
  lfs_t            lfs;
  uint8_t          read_buf[PM_LFS_CACHE];
  uint8_t          prog_buf[PM_LFS_CACHE];
  uint8_t          look_buf[PM_LFS_LOOK];
} pm_lfs_vol_t;

static pm_lfs_vol_t g_vols[PM_LFS_MAX_VOL];

static int bd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                   void *buffer, lfs_size_t size) {
  pm_lfs_bd_t *bd = (pm_lfs_bd_t *)c->context;
  size_t       addr;

  if (bd == NULL || bd->buf == NULL) {
    return LFS_ERR_IO;
  }
  addr = (size_t)block * (size_t)c->block_size + (size_t)off;
  if (addr + (size_t)size > bd->len) {
    return LFS_ERR_IO;
  }
  memcpy(buffer, bd->buf + addr, (size_t)size);
  return 0;
}

static int bd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                   const void *buffer, lfs_size_t size) {
  pm_lfs_bd_t *bd = (pm_lfs_bd_t *)c->context;
  size_t       addr;

  if (bd == NULL || bd->buf == NULL) {
    return LFS_ERR_IO;
  }
  addr = (size_t)block * (size_t)c->block_size + (size_t)off;
  if (addr + (size_t)size > bd->len) {
    return LFS_ERR_IO;
  }
  memcpy(bd->buf + addr, buffer, (size_t)size);
  return 0;
}

static int bd_erase(const struct lfs_config *c, lfs_block_t block) {
  pm_lfs_bd_t *bd = (pm_lfs_bd_t *)c->context;
  size_t       addr;

  if (bd == NULL || bd->buf == NULL) {
    return LFS_ERR_IO;
  }
  addr = (size_t)block * (size_t)c->block_size;
  if (addr + (size_t)c->block_size > bd->len) {
    return LFS_ERR_IO;
  }
  memset(bd->buf + addr, 0xff, (size_t)c->block_size);
  return 0;
}

static int bd_sync(const struct lfs_config *c) {
  (void)c;
  return 0;
}

static int fill_cfg(pm_lfs_vol_t *v, uint8_t *buf, size_t len) {
  size_t blocks;

  if (buf == NULL || len < (size_t)PM_LFS_BLOCK * 2u) {
    return -1;
  }
  blocks = len / (size_t)PM_LFS_BLOCK;
  if (blocks < 2u) {
    return -1;
  }
  memset(&v->cfg, 0, sizeof(v->cfg));
  v->bd.buf = buf;
  v->bd.len = len;
  v->cfg.context = &v->bd;
  v->cfg.read = bd_read;
  v->cfg.prog = bd_prog;
  v->cfg.erase = bd_erase;
  v->cfg.sync = bd_sync;
  v->cfg.read_size = PM_LFS_READ;
  v->cfg.prog_size = PM_LFS_PROG;
  v->cfg.block_size = PM_LFS_BLOCK;
  v->cfg.block_count = (lfs_size_t)blocks;
  v->cfg.block_cycles = 100;
  v->cfg.cache_size = PM_LFS_CACHE;
  v->cfg.lookahead_size = PM_LFS_LOOK;
  v->cfg.read_buffer = v->read_buf;
  v->cfg.prog_buffer = v->prog_buf;
  v->cfg.lookahead_buffer = v->look_buf;
  return 0;
}

static int32_t alloc_vol(void) {
  uint32_t i;

  for (i = 1u; i < PM_LFS_MAX_VOL; i++) {
    if (!g_vols[i].used) {
      memset(&g_vols[i], 0, sizeof(g_vols[i]));
      g_vols[i].used = 1;
      return (int32_t)i;
    }
  }
  return -1;
}

static void free_vol(uint32_t id) {
  if (id == 0u || id >= PM_LFS_MAX_VOL) {
    return;
  }
  memset(&g_vols[id], 0, sizeof(g_vols[id]));
}

int32_t pm_metal_fs_littlefs_format_buf(uint8_t *buf, size_t len) {
  pm_lfs_vol_t tmp;
  int          rc;

  memset(&tmp, 0, sizeof(tmp));
  if (fill_cfg(&tmp, buf, len) != 0) {
    return -1;
  }
  memset(buf, 0xff, len);
  rc = lfs_format(&tmp.lfs, &tmp.cfg);
  return (rc == 0) ? 0 : -1;
}

static int ensure_parent(lfs_t *lfs, const char *path) {
  char   tmp[256];
  size_t n;
  size_t i;

  if (path == NULL) {
    return -1;
  }
  n = strlen(path);
  if (n == 0u || n >= sizeof(tmp)) {
    return -1;
  }
  memcpy(tmp, path, n + 1u);
  for (i = 1u; i < n; i++) {
    if (tmp[i] == '/') {
      tmp[i] = '\0';
      if (tmp[0] != '\0') {
        (void)lfs_mkdir(lfs, tmp);
      }
      tmp[i] = '/';
    }
  }
  return 0;
}

int32_t pm_metal_fs_littlefs_seed_simple(uint8_t *buf, size_t len, const uint8_t *const *names,
                                         const uint8_t *const *datas, const uint32_t *lens,
                                         uint32_t count) {
  pm_lfs_vol_t tmp;
  uint32_t     i;
  int          rc;

  if (buf == NULL || (count > 0u && (names == NULL || datas == NULL || lens == NULL))) {
    return -1;
  }
  memset(&tmp, 0, sizeof(tmp));
  if (fill_cfg(&tmp, buf, len) != 0) {
    return -1;
  }
  rc = lfs_mount(&tmp.lfs, &tmp.cfg);
  if (rc != 0) {
    memset(buf, 0xff, len);
    if (lfs_format(&tmp.lfs, &tmp.cfg) != 0) {
      return -1;
    }
    if (lfs_mount(&tmp.lfs, &tmp.cfg) != 0) {
      return -1;
    }
  }
  for (i = 0u; i < count; i++) {
    const char *path;
    lfs_file_t  file;
    lfs_ssize_t wr;

    if (names[i] == NULL || datas[i] == NULL) {
      (void)lfs_unmount(&tmp.lfs);
      return -1;
    }
    path = (const char *)names[i];
    while (*path == '/') {
      path++;
    }
    if (ensure_parent(&tmp.lfs, path) != 0) {
      (void)lfs_unmount(&tmp.lfs);
      return -1;
    }
    rc = lfs_file_open(&tmp.lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (rc != 0) {
      (void)lfs_unmount(&tmp.lfs);
      return -1;
    }
    if (lens[i] > 0u) {
      wr = lfs_file_write(&tmp.lfs, &file, datas[i], (lfs_size_t)lens[i]);
      if (wr < 0 || (uint32_t)wr != lens[i]) {
        (void)lfs_file_close(&tmp.lfs, &file);
        (void)lfs_unmount(&tmp.lfs);
        return -1;
      }
    }
    if (lfs_file_close(&tmp.lfs, &file) != 0) {
      (void)lfs_unmount(&tmp.lfs);
      return -1;
    }
  }
  if (lfs_unmount(&tmp.lfs) != 0) {
    return -1;
  }
  return 0;
}

/* Open volume; returns nonzero vol id or 0. */
uint32_t pm_metal_fs_littlefs_open_buf(uint8_t *buf, size_t len) {
  int32_t id;
  int     rc;

  id = alloc_vol();
  if (id < 0) {
    return 0u;
  }
  if (fill_cfg(&g_vols[(uint32_t)id], buf, len) != 0) {
    free_vol((uint32_t)id);
    return 0u;
  }
  rc = lfs_mount(&g_vols[(uint32_t)id].lfs, &g_vols[(uint32_t)id].cfg);
  if (rc != 0) {
    free_vol((uint32_t)id);
    return 0u;
  }
  return (uint32_t)id;
}

int32_t pm_metal_fs_littlefs_close_vol(uint32_t vol) {
  if (vol == 0u || vol >= PM_LFS_MAX_VOL || !g_vols[vol].used) {
    return -1;
  }
  (void)lfs_unmount(&g_vols[vol].lfs);
  free_vol(vol);
  return 0;
}

/* Backing buffer size for mounted volume; 0 if bad id. */
size_t pm_metal_fs_littlefs_vol_bytes(uint32_t vol) {
  if (vol == 0u || vol >= PM_LFS_MAX_VOL || !g_vols[vol].used) {
    return 0u;
  }
  return g_vols[vol].bd.len;
}

lfs_t *pm_metal_fs_littlefs_lfs(uint32_t vol) {
  if (vol == 0u || vol >= PM_LFS_MAX_VOL || !g_vols[vol].used) {
    return NULL;
  }
  return &g_vols[vol].lfs;
}

/* ---- open handles for Rust ops -------------------------------------- */

#define PM_LFS_MAX_OPEN 32u
#define PM_LFS_O_RDONLY 1u
#define PM_LFS_O_WRONLY 2u
#define PM_LFS_O_RDWR 3u
#define PM_LFS_O_CREAT 4u
#define PM_LFS_O_TRUNC 8u
#define PM_LFS_O_APPEND 16u
#define PM_LFS_O_DIRECTORY 32u
#define PM_LFS_INVALID 0xffffffffu

typedef struct {
  int        used;
  int        is_dir;
  uint32_t   vol;
  lfs_file_t file;
  lfs_dir_t  dir;
} pm_lfs_open_t;

static pm_lfs_open_t g_open[PM_LFS_MAX_OPEN];

static uint32_t map_flags(uint32_t flags) {
  uint32_t o = 0u;
  uint32_t mode = flags & 3u;

  if (mode == PM_LFS_O_WRONLY) {
    o = LFS_O_WRONLY;
  } else if (mode == PM_LFS_O_RDWR) {
    o = LFS_O_RDWR;
  } else {
    o = LFS_O_RDONLY;
  }
  if ((flags & PM_LFS_O_CREAT) != 0u) {
    o |= LFS_O_CREAT;
  }
  if ((flags & PM_LFS_O_TRUNC) != 0u) {
    o |= LFS_O_TRUNC;
  }
  if ((flags & PM_LFS_O_APPEND) != 0u) {
    o |= LFS_O_APPEND;
  }
  return o;
}

static int32_t alloc_open(void) {
  uint32_t i;

  for (i = 0u; i < PM_LFS_MAX_OPEN; i++) {
    if (!g_open[i].used) {
      memset(&g_open[i], 0, sizeof(g_open[i]));
      g_open[i].used = 1;
      return (int32_t)i;
    }
  }
  return -1;
}

uint32_t pm_metal_fs_littlefs_op_open(uint32_t vol, const uint8_t *path, uint32_t flags) {
  lfs_t     *lfs;
  const char *p;
  int32_t    slot;
  int        rc;

  lfs = pm_metal_fs_littlefs_lfs(vol);
  if (lfs == NULL || path == NULL) {
    return PM_LFS_INVALID;
  }
  p = (const char *)path;
  while (*p == '/') {
    p++;
  }
  slot = alloc_open();
  if (slot < 0) {
    return PM_LFS_INVALID;
  }
  g_open[(uint32_t)slot].vol = vol;
  if ((flags & PM_LFS_O_DIRECTORY) != 0u || p[0] == '\0') {
    g_open[(uint32_t)slot].is_dir = 1;
    rc = lfs_dir_open(lfs, &g_open[(uint32_t)slot].dir, (p[0] == '\0') ? "/" : p);
    if (rc != 0) {
      g_open[(uint32_t)slot].used = 0;
      return PM_LFS_INVALID;
    }
    return (uint32_t)slot;
  }
  g_open[(uint32_t)slot].is_dir = 0;
  rc = lfs_file_open(lfs, &g_open[(uint32_t)slot].file, p, (int)map_flags(flags));
  if (rc != 0) {
    g_open[(uint32_t)slot].used = 0;
    return PM_LFS_INVALID;
  }
  return (uint32_t)slot;
}

uint32_t pm_metal_fs_littlefs_op_close(uint32_t h) {
  lfs_t *lfs;

  if (h >= PM_LFS_MAX_OPEN || !g_open[h].used) {
    return PM_LFS_INVALID;
  }
  lfs = pm_metal_fs_littlefs_lfs(g_open[h].vol);
  if (lfs != NULL) {
    if (g_open[h].is_dir) {
      (void)lfs_dir_close(lfs, &g_open[h].dir);
    } else {
      (void)lfs_file_close(lfs, &g_open[h].file);
    }
  }
  g_open[h].used = 0;
  return 0u;
}

uint32_t pm_metal_fs_littlefs_op_fread(uint32_t h, uint8_t *dest, uint32_t len) {
  lfs_t      *lfs;
  lfs_ssize_t n;

  if (h >= PM_LFS_MAX_OPEN || !g_open[h].used || g_open[h].is_dir || dest == NULL) {
    return 0u;
  }
  lfs = pm_metal_fs_littlefs_lfs(g_open[h].vol);
  if (lfs == NULL) {
    return 0u;
  }
  n = lfs_file_read(lfs, &g_open[h].file, dest, (lfs_size_t)len);
  return (n < 0) ? 0u : (uint32_t)n;
}

uint32_t pm_metal_fs_littlefs_op_fwrite(uint32_t h, const uint8_t *src, uint32_t len) {
  lfs_t      *lfs;
  lfs_ssize_t n;

  if (h >= PM_LFS_MAX_OPEN || !g_open[h].used || g_open[h].is_dir || src == NULL) {
    return 0u;
  }
  lfs = pm_metal_fs_littlefs_lfs(g_open[h].vol);
  if (lfs == NULL) {
    return 0u;
  }
  n = lfs_file_write(lfs, &g_open[h].file, src, (lfs_size_t)len);
  return (n < 0) ? 0u : (uint32_t)n;
}

uint32_t pm_metal_fs_littlefs_op_fpread(uint32_t h, uint32_t off, uint8_t *dest, uint32_t len) {
  lfs_t *lfs;

  if (h >= PM_LFS_MAX_OPEN || !g_open[h].used || g_open[h].is_dir) {
    return 0u;
  }
  lfs = pm_metal_fs_littlefs_lfs(g_open[h].vol);
  if (lfs == NULL) {
    return 0u;
  }
  if (lfs_file_seek(lfs, &g_open[h].file, (lfs_soff_t)off, LFS_SEEK_SET) < 0) {
    return 0u;
  }
  return pm_metal_fs_littlefs_op_fread(h, dest, len);
}

uint32_t pm_metal_fs_littlefs_op_fpwrite(uint32_t h, uint32_t off, const uint8_t *src, uint32_t len) {
  lfs_t *lfs;

  if (h >= PM_LFS_MAX_OPEN || !g_open[h].used || g_open[h].is_dir) {
    return 0u;
  }
  lfs = pm_metal_fs_littlefs_lfs(g_open[h].vol);
  if (lfs == NULL) {
    return 0u;
  }
  if (lfs_file_seek(lfs, &g_open[h].file, (lfs_soff_t)off, LFS_SEEK_SET) < 0) {
    return 0u;
  }
  return pm_metal_fs_littlefs_op_fwrite(h, src, len);
}

int32_t pm_metal_fs_littlefs_op_lseek(uint32_t h, int32_t off, uint32_t whence) {
  lfs_t     *lfs;
  lfs_soff_t n;
  int        w;

  if (h >= PM_LFS_MAX_OPEN || !g_open[h].used || g_open[h].is_dir) {
    return -1;
  }
  lfs = pm_metal_fs_littlefs_lfs(g_open[h].vol);
  if (lfs == NULL) {
    return -1;
  }
  if (whence == 1u) {
    w = LFS_SEEK_CUR;
  } else if (whence == 2u) {
    w = LFS_SEEK_END;
  } else {
    w = LFS_SEEK_SET;
  }
  n = lfs_file_seek(lfs, &g_open[h].file, (lfs_soff_t)off, w);
  return (n < 0) ? -1 : (int32_t)n;
}

uint32_t pm_metal_fs_littlefs_op_stat(uint32_t vol, const uint8_t *path, uint8_t *st_out) {
  lfs_t          *lfs;
  const char     *p;
  struct lfs_info info;
  int             rc;

  lfs = pm_metal_fs_littlefs_lfs(vol);
  if (lfs == NULL || path == NULL) {
    return PM_LFS_INVALID;
  }
  p = (const char *)path;
  while (*p == '/') {
    p++;
  }
  if (p[0] == '\0') {
    if (st_out != NULL) {
      /* size + type_ as pm_metal_fs_stat_t */
      ((uint32_t *)st_out)[0] = 0u;
      ((uint32_t *)st_out)[1] = 2u; /* DIR */
    }
    return 0u;
  }
  rc = lfs_stat(lfs, p, &info);
  if (rc != 0) {
    return PM_LFS_INVALID;
  }
  if (st_out != NULL) {
    ((uint32_t *)st_out)[0] = (uint32_t)info.size;
    ((uint32_t *)st_out)[1] = (info.type == LFS_TYPE_DIR) ? 2u : 1u;
  }
  return 0u;
}

uint32_t pm_metal_fs_littlefs_op_readdir(uint32_t h, uint8_t *name_out, uint32_t name_cap) {
  lfs_t          *lfs;
  struct lfs_info info;
  int             rc;
  size_t          n;
  size_t          i;

  if (h >= PM_LFS_MAX_OPEN || !g_open[h].used || !g_open[h].is_dir || name_out == NULL ||
      name_cap == 0u) {
    return 0u;
  }
  lfs = pm_metal_fs_littlefs_lfs(g_open[h].vol);
  if (lfs == NULL) {
    return 0u;
  }
  for (;;) {
    rc = lfs_dir_read(lfs, &g_open[h].dir, &info);
    if (rc <= 0) {
      return 0u;
    }
    if (info.name[0] == '.' && (info.name[1] == '\0' || (info.name[1] == '.' && info.name[2] == '\0'))) {
      continue;
    }
    n = strlen(info.name);
    if (n + 1u > (size_t)name_cap) {
      n = (size_t)name_cap - 1u;
    }
    for (i = 0u; i < n; i++) {
      name_out[i] = (uint8_t)info.name[i];
    }
    name_out[n] = 0u;
    return 1u;
  }
}

uint32_t pm_metal_fs_littlefs_op_mkdir(uint32_t vol, const uint8_t *path) {
  lfs_t      *lfs;
  const char *p;
  int         rc;

  lfs = pm_metal_fs_littlefs_lfs(vol);
  if (lfs == NULL || path == NULL) {
    return PM_LFS_INVALID;
  }
  p = (const char *)path;
  while (*p == '/') {
    p++;
  }
  if (ensure_parent(lfs, p) != 0) {
    return PM_LFS_INVALID;
  }
  rc = lfs_mkdir(lfs, p);
  return (rc == 0 || rc == LFS_ERR_EXIST) ? 0u : PM_LFS_INVALID;
}

uint32_t pm_metal_fs_littlefs_op_unlink(uint32_t vol, const uint8_t *path) {
  lfs_t      *lfs;
  const char *p;
  int         rc;

  lfs = pm_metal_fs_littlefs_lfs(vol);
  if (lfs == NULL || path == NULL) {
    return PM_LFS_INVALID;
  }
  p = (const char *)path;
  while (*p == '/') {
    p++;
  }
  rc = lfs_remove(lfs, p);
  return (rc == 0) ? 0u : PM_LFS_INVALID;
}

uint32_t pm_metal_fs_littlefs_op_fsync(uint32_t h) {
  lfs_t *lfs;

  if (h >= PM_LFS_MAX_OPEN || !g_open[h].used || g_open[h].is_dir) {
    return PM_LFS_INVALID;
  }
  lfs = pm_metal_fs_littlefs_lfs(g_open[h].vol);
  if (lfs == NULL) {
    return PM_LFS_INVALID;
  }
  return (lfs_file_sync(lfs, &g_open[h].file) == 0) ? 0u : PM_LFS_INVALID;
}

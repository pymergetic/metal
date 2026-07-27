/** @file
  ESP RAM cache + API (shared). Live volume I/O in esp_port.
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/runtime/mem/mem.h>

/* Port: bios|efi fs/esp/esp_port.c */
int pm_metal_esp_init_port(void *image_handle);
int pm_metal_esp_read_file_port(const char *path, uint8_t **out, uint32_t *len);
int pm_metal_esp_write_file_port(const char *path, const uint8_t *data, uint32_t len);
int pm_metal_esp_file_size_port(const char *path, uint32_t *len);
int pm_metal_esp_stat_port(const char *path, uint32_t *size, uint32_t *type);
int pm_metal_esp_mkdir_port(const char *path);
int pm_metal_esp_unlink_port(const char *path);
int pm_metal_esp_rename_port(const char *old_path, const char *new_path);
int pm_metal_esp_fsync_port(const char *path);
int pm_metal_esp_readdir_port(const char *path, uint32_t index, char *name, uint32_t name_cap);

/* mods/apps + mods/py (now incl. stdlib_src/'s ~20 Easy-pack files, see
 * docs/MICROPYTHON.md) + the 2 individually preloaded mods/tests fixtures
 * already run ~40 entries before counting any future growth — 128 gives
 * real headroom instead of silently dropping preload/write slots again. */
#define PM_METAL_ESP_CACHE_MAX   128u
#define PM_METAL_ESP_DIR_MAX     16u
#define PM_METAL_ESP_PATH_MAX    128u
#define PM_METAL_ESP_READDIR_MAX 32u

typedef struct {
  int32_t  used;
  char     path[PM_METAL_ESP_PATH_MAX];
  uint8_t *data;
  uint32_t len;
  int32_t  dirty;
} metal_esp_cache_t;

static metal_esp_cache_t mCache[PM_METAL_ESP_CACHE_MAX];
static char              mDirs[PM_METAL_ESP_DIR_MAX][PM_METAL_ESP_PATH_MAX];
static int32_t           mReady;
static char              mLoadedPath[PM_METAL_ESP_PATH_MAX];
static const void       *mLoadedImageBase;
static uint32_t          mLoadedImageSize;

static int32_t MetalEspPathEq(const char *a, const char *b)
{
  uintptr_t i;

  if (a == NULL || b == NULL) {
    return 0;
  }

  for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }

  return (a[i] == b[i]) ? 1 : 0;
}

static int32_t MetalEspIsPrefix(const char *dir, const char *path)
{
  uintptr_t dlen;
  uintptr_t i;

  if (dir == NULL || path == NULL) {
    return 0;
  }

  dlen = strlen(dir);
  if (strlen(path) <= dlen) {
    return 0;
  }

  for (i = 0; i < dlen; i++) {
    if (dir[i] != path[i]) {
      return 0;
    }
  }

  return (dir[dlen] == '\0' && path[dlen] == '/') ? 1 : 0;
}

static metal_esp_cache_t *MetalEspCacheFind(const char *path)
{
  uintptr_t i;

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (mCache[i].used && MetalEspPathEq(mCache[i].path, path)) {
      return &mCache[i];
    }
  }

  return NULL;
}

static metal_esp_cache_t *MetalEspCacheSlot(const char *path)
{
  metal_esp_cache_t *ent;
  uintptr_t          i;

  ent = MetalEspCacheFind(path);
  if (ent != NULL) {
    return ent;
  }

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (!mCache[i].used) {
      snprintf(
        mCache[i].path, sizeof(mCache[i].path), "%.*s", (int)(sizeof(mCache[i].path) - 1), path);
      mCache[i].used  = 1;
      mCache[i].data  = NULL;
      mCache[i].len   = 0;
      mCache[i].dirty = 0;
      return &mCache[i];
    }
  }

  /* Cache full: reuse a clean slot so hostkey/config writes are not stuck. */
  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (mCache[i].used && !mCache[i].dirty) {
      if (mCache[i].data != NULL) {
        pm_metal_mem_free(mCache[i].data);
      }
      snprintf(
        mCache[i].path, sizeof(mCache[i].path), "%.*s", (int)(sizeof(mCache[i].path) - 1), path);
      mCache[i].data  = NULL;
      mCache[i].len   = 0;
      mCache[i].dirty = 0;
      return &mCache[i];
    }
  }

  return NULL;
}

static int32_t MetalEspDirFind(const char *path)
{
  uintptr_t i;

  for (i = 0; i < PM_METAL_ESP_DIR_MAX; i++) {
    if (mDirs[i][0] != '\0' && MetalEspPathEq(mDirs[i], path)) {
      return 1;
    }
  }

  return 0;
}

static int32_t MetalEspDirAdd(const char *path)
{
  uintptr_t i;

  if (path == NULL || path[0] == '\0') {
    return -1;
  }

  if (MetalEspDirFind(path)) {
    return 0;
  }

  for (i = 0; i < PM_METAL_ESP_DIR_MAX; i++) {
    if (mDirs[i][0] == '\0') {
      snprintf(mDirs[i], sizeof(mDirs[i]), "%.*s", (int)(sizeof(mDirs[i]) - 1), path);
      return 0;
    }
  }

  return -1;
}

static void MetalEspDirRemovePrefix(const char *path)
{
  uintptr_t i;

  for (i = 0; i < PM_METAL_ESP_DIR_MAX; i++) {
    if (mDirs[i][0] != '\0' &&
        (MetalEspPathEq(mDirs[i], path) || MetalEspIsPrefix(path, mDirs[i]))) {
      mDirs[i][0] = '\0';
    }
  }
}

static int32_t MetalEspCacheStore(const char    *path,
                                  const uint8_t *data,
                                  uint32_t       len,
                                  int32_t        dirty)
{
  metal_esp_cache_t *ent;
  uint8_t           *copy;

  ent = MetalEspCacheSlot(path);
  if (ent == NULL) {
    return -1;
  }

  copy = NULL;
  if (len > 0) {
    if (data == NULL) {
      return -1;
    }

    copy = (uint8_t *)pm_metal_mem_alloc(len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (copy == NULL) {
      return -1;
    }

    memcpy(copy, data, len);
  }

  if (ent->data != NULL) {
    pm_metal_mem_free(ent->data);
  }

  ent->data  = copy;
  ent->len   = len;
  ent->dirty = dirty;
  return 0;
}

static int32_t MetalEspCacheEnsure(const char *path)
{
  metal_esp_cache_t *ent;
  uint8_t           *buf;
  uint32_t           len;

  ent = MetalEspCacheFind(path);
  if (ent != NULL) {
    return 0;
  }

  buf = NULL;
  len = 0;
  if (pm_metal_esp_read_file_port(path, &buf, &len) != 0) {
    return -1;
  }

  if (MetalEspCacheStore(path, buf, len, 0) != 0) {
    if (buf != NULL) {
      pm_metal_mem_free(buf);
    }

    return -1;
  }

  if (buf != NULL) {
    pm_metal_mem_free(buf);
  }

  return 0;
}

static int32_t MetalEspNameInList(char        names[][PM_METAL_ESP_PATH_MAX],
                                  uintptr_t   count,
                                  const char *name)
{
  uintptr_t j;

  for (j = 0; j < count; j++) {
    if (MetalEspPathEq(names[j], name)) {
      return 1;
    }
  }

  return 0;
}

static int32_t MetalEspReaddirCollect(const char *dir,
                                      char        names[][PM_METAL_ESP_PATH_MAX],
                                      uint32_t   *count)
{
  uintptr_t i;
  uintptr_t o;
  uintptr_t dlen;
  uint32_t  pi;

  if (dir == NULL || names == NULL || count == NULL) {
    return -1;
  }

  *count = 0;
  o      = 0;
  dlen   = strlen(dir);

  for (i = 0; i < PM_METAL_ESP_DIR_MAX && o < PM_METAL_ESP_READDIR_MAX; i++) {
    const char *sub;

    if (mDirs[i][0] == '\0' || !MetalEspIsPrefix(dir, mDirs[i])) {
      continue;
    }

    sub = mDirs[i] + dlen + 1;
    if (sub[0] == '\0' || strstr(sub, "/") != NULL) {
      continue;
    }

    if (MetalEspNameInList(names, o, sub)) {
      continue;
    }

    snprintf(names[o], PM_METAL_ESP_PATH_MAX, "%.*s", (int)(PM_METAL_ESP_PATH_MAX - 1), sub);
    o++;
  }

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX && o < PM_METAL_ESP_READDIR_MAX; i++) {
    const char *sub;

    if (!mCache[i].used || !MetalEspIsPrefix(dir, mCache[i].path)) {
      continue;
    }

    sub = mCache[i].path + dlen + 1;
    if (sub[0] == '\0' || strstr(sub, "/") != NULL) {
      continue;
    }

    if (MetalEspNameInList(names, o, sub)) {
      continue;
    }

    snprintf(names[o], PM_METAL_ESP_PATH_MAX, "%.*s", (int)(PM_METAL_ESP_PATH_MAX - 1), sub);
    o++;
  }

  for (pi = 0; o < PM_METAL_ESP_READDIR_MAX; pi++) {
    char    port_name[PM_METAL_ESP_PATH_MAX];
    int32_t rc;

    rc = pm_metal_esp_readdir_port(dir, pi, port_name, sizeof(port_name));
    if (rc <= 0) {
      break;
    }

    if (MetalEspNameInList(names, o, port_name)) {
      continue;
    }

    snprintf(names[o], PM_METAL_ESP_PATH_MAX, "%.*s", (int)(PM_METAL_ESP_PATH_MAX - 1), port_name);
    o++;
  }

  *count = (uint32_t)o;
  return 0;
}

int pm_metal_esp_init(void *image_handle)
{
  if (mReady) {
    return 0;
  }

  memset(mCache, 0, sizeof(mCache));
  memset(mDirs, 0, sizeof(mDirs));
  /*
   * EFI: volume bind required when image_handle is set.
   * BIOS: hw is a no-op; cache-only is fine.
   */
  if (pm_metal_esp_init_port(image_handle) != 0 && image_handle != NULL) {
    return -1;
  }

  mReady = 1;
  return 0;
}

int pm_metal_esp_ready(void)
{
  return mReady ? 1 : 0;
}

void pm_metal_esp_set_loaded_identity(const char *path, const void *base, uint32_t size)
{
  uintptr_t i;

  mLoadedPath[0]   = '\0';
  mLoadedImageBase = base;
  mLoadedImageSize = size;
  if (path == NULL || path[0] == '\0') {
    return;
  }

  for (i = 0; i + 1 < PM_METAL_ESP_PATH_MAX && path[i] != '\0'; i++) {
    char c;

    c = path[i];
    if (c == '\\') {
      c = '/';
    }

    mLoadedPath[i] = c;
  }

  mLoadedPath[i] = '\0';
}

const char *pm_metal_esp_loaded_path(void)
{
  return (mLoadedPath[0] != '\0') ? mLoadedPath : NULL;
}

int pm_metal_esp_loaded_image(const void **base, uint32_t *size)
{
  if (mLoadedImageBase == NULL || mLoadedImageSize == 0) {
    return -1;
  }

  if (base != NULL) {
    *base = mLoadedImageBase;
  }

  if (size != NULL) {
    *size = mLoadedImageSize;
  }

  return 0;
}

int pm_metal_esp_cache_put(const char *path, const uint8_t *data, uint32_t len)
{
  if (!mReady || path == NULL) {
    return -1;
  }

  return MetalEspCacheStore(path, data, len, 0);
}

int pm_metal_esp_preload(const char *path)
{
  uint8_t *buf;
  uint32_t len;

  if (!mReady || path == NULL) {
    return -1;
  }

  buf = NULL;
  len = 0;
  if (pm_metal_esp_read_file_port(path, &buf, &len) != 0) {
    return -1;
  }

  if (MetalEspCacheStore(path, buf, len, 0) != 0) {
    if (buf != NULL) {
      pm_metal_mem_free(buf);
    }

    return -1;
  }

  if (buf != NULL) {
    pm_metal_mem_free(buf);
  }

  return 0;
}

int pm_metal_esp_preload_tree(const char *dir)
{
  uint32_t idx;
  uint32_t n;
  uint32_t ty;
  uint32_t sz;
  char     name[PM_METAL_ESP_PATH_MAX];
  char     child[PM_METAL_ESP_PATH_MAX];

  if (!mReady || dir == NULL || dir[0] == '\0') {
    return -1;
  }

  if (pm_metal_esp_stat(dir, &sz, &ty) != 0 || ty != PM_METAL_ESP_TYPE_DIR) {
    return -1;
  }

  n = 0;
  for (idx = 0;; idx++) {
    int32_t rc;

    rc = pm_metal_esp_readdir(dir, idx, name, sizeof(name));
    if (rc <= 0) {
      break;
    }

    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
      continue;
    }

    if (snprintf(child, sizeof(child), "%s/%s", dir, name) >= sizeof(child)) {
      continue;
    }

    if (pm_metal_esp_stat(child, &sz, &ty) != 0) {
      continue;
    }

    if (ty == PM_METAL_ESP_TYPE_DIR) {
      if (pm_metal_esp_preload_tree(child) == 0) {
        n++;
      }
    } else if (ty == PM_METAL_ESP_TYPE_FILE) {
      if (pm_metal_esp_preload(child) == 0) {
        n++;
      }
    }
  }

  return (n > 0u) ? 0 : -1;
}

int pm_metal_esp_file_size(const char *path, uint32_t *len)
{
  metal_esp_cache_t *ent;

  if (len == NULL || path == NULL) {
    return -1;
  }

  *len = 0;
  ent  = MetalEspCacheFind(path);
  if (ent != NULL) {
    *len = ent->len;
    return 0;
  }

  return pm_metal_esp_file_size_port(path, len);
}

int pm_metal_esp_read_file(const char *path, uint8_t **out, uint32_t *len)
{
  metal_esp_cache_t *ent;
  uint8_t           *Buf;

  if (out == NULL || len == NULL || path == NULL) {
    return -1;
  }

  *out = NULL;
  *len = 0;

  ent = MetalEspCacheFind(path);
  if (ent != NULL) {
    if (ent->len == 0) {
      return 0;
    }

    Buf = (uint8_t *)pm_metal_mem_alloc(ent->len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (Buf == NULL) {
      return -1;
    }

    memcpy(Buf, ent->data, ent->len);
    *out = Buf;
    *len = ent->len;
    return 0;
  }

  return pm_metal_esp_read_file_port(path, out, len);
}

int pm_metal_esp_write_file(const char *path, const uint8_t *data, uint32_t len)
{
  if (path == NULL) {
    return -1;
  }

  if (len > 0 && data == NULL) {
    return -1;
  }

  if (MetalEspCacheStore(path, data, len, 1) != 0) {
    return -1;
  }

  (void)pm_metal_esp_write_file_port(path, data, len);
  if (MetalEspCacheFind(path) != NULL) {
    MetalEspCacheFind(path)->dirty = 0;
  }

  return 0;
}

static int32_t MetalEspHasChildren(const char *dir)
{
  uintptr_t i;

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (mCache[i].used && MetalEspIsPrefix(dir, mCache[i].path)) {
      return 1;
    }
  }

  for (i = 0; i < PM_METAL_ESP_DIR_MAX; i++) {
    if (mDirs[i][0] != '\0' && MetalEspIsPrefix(dir, mDirs[i])) {
      return 1;
    }
  }

  return 0;
}

int pm_metal_esp_stat(const char *path, uint32_t *size, uint32_t *type)
{
  metal_esp_cache_t *ent;

  if (path == NULL || size == NULL || type == NULL) {
    return -1;
  }

  *size = 0;
  *type = PM_METAL_ESP_TYPE_FILE;

  if (MetalEspDirFind(path)) {
    *type = PM_METAL_ESP_TYPE_DIR;
    return 0;
  }

  if (MetalEspHasChildren(path)) {
    *type = PM_METAL_ESP_TYPE_DIR;
    return 0;
  }

  ent = MetalEspCacheFind(path);
  if (ent != NULL) {
    *size = ent->len;
    *type = PM_METAL_ESP_TYPE_FILE;
    return 0;
  }

  return pm_metal_esp_stat_port(path, size, type);
}

int pm_metal_esp_read_at(
  const char *path, uint32_t off, uint8_t *buf, uint32_t len, uint32_t *nread)
{
  metal_esp_cache_t *ent;
  uint32_t           avail;

  if (path == NULL || buf == NULL || nread == NULL) {
    return -1;
  }

  *nread = 0;
  if (len == 0) {
    return 0;
  }

  ent = MetalEspCacheFind(path);
  if (ent == NULL) {
    if (MetalEspCacheEnsure(path) != 0) {
      return -1;
    }

    ent = MetalEspCacheFind(path);
  }

  if (ent == NULL) {
    return -1;
  }

  if (off >= ent->len) {
    return 0;
  }

  avail = ent->len - off;
  if (len > avail) {
    len = avail;
  }

  memcpy(buf, ent->data + off, len);
  *nread = len;
  return 0;
}

int pm_metal_esp_write_at(
  const char *path, uint32_t off, const uint8_t *data, uint32_t len, int32_t truncate)
{
  metal_esp_cache_t *ent;
  uint32_t           new_len;
  uint8_t           *copy;

  if (path == NULL || (len > 0 && data == NULL)) {
    return -1;
  }

  ent = MetalEspCacheFind(path);
  if (ent == NULL) {
    ent = MetalEspCacheSlot(path);
  }

  if (ent == NULL) {
    return -1;
  }

  if (truncate) {
    if (MetalEspCacheStore(path, NULL, 0, 1) != 0) {
      return -1;
    }

    ent = MetalEspCacheFind(path);
    if (ent == NULL) {
      return -1;
    }
  }

  new_len = off + len;
  if (new_len < ent->len) {
    new_len = ent->len;
  }

  /* pm_metal_mem_alloc(0, ...) always returns NULL (mem.c) — a genuinely
   * empty result (new file, or truncate-to-0) is not an allocation
   * failure, so it can't go through the copy == NULL error check below. */
  if (new_len == 0) {
    if (ent->data != NULL) {
      pm_metal_mem_free(ent->data);
    }

    ent->data  = NULL;
    ent->len   = 0;
    ent->dirty = 1;
    return 0;
  }

  copy = (uint8_t *)pm_metal_mem_alloc(new_len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (copy == NULL) {
    return -1;
  }

  if (ent->len > 0 && ent->data != NULL) {
    memcpy(copy, ent->data, ent->len);
  }

  if (len > 0) {
    memcpy(copy + off, data, len);
  }

  if (ent->data != NULL) {
    pm_metal_mem_free(ent->data);
  }

  ent->data  = copy;
  ent->len   = new_len;
  ent->dirty = 1;
  return 0;
}

int pm_metal_esp_fsync(const char *path)
{
  metal_esp_cache_t *ent;

  if (path == NULL) {
    return -1;
  }

  ent = MetalEspCacheFind(path);
  if (ent == NULL || !ent->dirty) {
    return pm_metal_esp_fsync_port(path);
  }

  if (pm_metal_esp_write_file_port(path, ent->data, ent->len) != 0) {
    return -1;
  }

  ent->dirty = 0;
  return pm_metal_esp_fsync_port(path);
}

int pm_metal_esp_mkdir(const char *path)
{
  if (path == NULL) {
    return -1;
  }

  if (MetalEspDirAdd(path) != 0) {
    return -1;
  }

  (void)pm_metal_esp_mkdir_port(path);
  return 0;
}

int pm_metal_esp_unlink(const char *path)
{
  metal_esp_cache_t *ent;
  uintptr_t          i;

  if (path == NULL) {
    return -1;
  }

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (mCache[i].used && MetalEspPathEq(mCache[i].path, path)) {
      ent = &mCache[i];
      if (ent->data != NULL) {
        pm_metal_mem_free(ent->data);
      }

      memset(ent, 0, sizeof(*ent));
      break;
    }
  }

  MetalEspDirRemovePrefix(path);
  (void)pm_metal_esp_unlink_port(path);
  return 0;
}

int pm_metal_esp_rename(const char *old_path, const char *new_path)
{
  metal_esp_cache_t *ent;
  uintptr_t          i;

  if (old_path == NULL || new_path == NULL) {
    return -1;
  }

  ent = MetalEspCacheFind(old_path);
  if (ent != NULL) {
    metal_esp_cache_t *dup;

    dup = MetalEspCacheSlot(new_path);
    if (dup == NULL) {
      return -1;
    }

    if (dup->data != NULL) {
      pm_metal_mem_free(dup->data);
    }

    dup->data  = ent->data;
    dup->len   = ent->len;
    dup->dirty = ent->dirty;
    ent->data  = NULL;
    ent->len   = 0;
    ent->used  = 0;
    ent->dirty = 0;
  }

  for (i = 0; i < PM_METAL_ESP_DIR_MAX; i++) {
    if (mDirs[i][0] != '\0' && MetalEspPathEq(mDirs[i], old_path)) {
      snprintf(mDirs[i], sizeof(mDirs[i]), "%.*s", (int)(sizeof(mDirs[i]) - 1), new_path);
    }
  }

  (void)pm_metal_esp_rename_port(old_path, new_path);
  return 0;
}

int pm_metal_esp_readdir(const char *path, uint32_t index, char *name, uint32_t name_cap)
{
  char     names[PM_METAL_ESP_READDIR_MAX][PM_METAL_ESP_PATH_MAX];
  uint32_t count;

  if (path == NULL || name == NULL || name_cap == 0) {
    return -1;
  }

  if (MetalEspReaddirCollect(path, names, &count) != 0) {
    return -1;
  }

  if (index >= count) {
    return 0;
  }

  snprintf(name, name_cap, "%.*s", (int)(name_cap - 1), names[index]);
  return 1;
}

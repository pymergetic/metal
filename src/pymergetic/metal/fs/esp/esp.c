/** @file
  ESP RAM cache + API (shared). Live volume I/O in esp_port.
**/
#include <pymergetic/metal/fs/esp/esp.h>
#include <runtime/mem/mem.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

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
int pm_metal_esp_readdir_port(const char *path, uint32_t index, char *name,
			      uint32_t name_cap);

#define PM_METAL_ESP_CACHE_MAX  32u
#define PM_METAL_ESP_DIR_MAX    16u
#define PM_METAL_ESP_PATH_MAX   128u
#define PM_METAL_ESP_READDIR_MAX 32u

typedef struct {
  INT32   used;
  CHAR8   path[PM_METAL_ESP_PATH_MAX];
  UINT8  *data;
  UINT32  len;
  INT32   dirty;
} metal_esp_cache_t;

STATIC metal_esp_cache_t  mCache[PM_METAL_ESP_CACHE_MAX];
STATIC CHAR8              mDirs[PM_METAL_ESP_DIR_MAX][PM_METAL_ESP_PATH_MAX];
STATIC INT32              mReady;
STATIC CHAR8              mLoadedPath[PM_METAL_ESP_PATH_MAX];
STATIC CONST VOID        *mLoadedImageBase;
STATIC UINT32             mLoadedImageSize;

STATIC
INT32
MetalEspPathEq (
  CONST CHAR8  *a,
  CONST CHAR8  *b
  )
{
  UINTN  i;

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

STATIC
INT32
MetalEspIsPrefix (
  CONST CHAR8  *dir,
  CONST CHAR8  *path
  )
{
  UINTN  dlen;
  UINTN  i;

  if (dir == NULL || path == NULL) {
    return 0;
  }

  dlen = AsciiStrLen (dir);
  if (AsciiStrLen (path) <= dlen) {
    return 0;
  }

  for (i = 0; i < dlen; i++) {
    if (dir[i] != path[i]) {
      return 0;
    }
  }

  return (dir[dlen] == '\0' && path[dlen] == '/') ? 1 : 0;
}

STATIC
metal_esp_cache_t *
MetalEspCacheFind (
  CONST CHAR8  *path
  )
{
  UINTN  i;

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (mCache[i].used && MetalEspPathEq (mCache[i].path, path)) {
      return &mCache[i];
    }
  }

  return NULL;
}

STATIC
metal_esp_cache_t *
MetalEspCacheSlot (
  CONST CHAR8  *path
  )
{
  metal_esp_cache_t  *ent;
  UINTN               i;

  ent = MetalEspCacheFind (path);
  if (ent != NULL) {
    return ent;
  }

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (!mCache[i].used) {
      AsciiStrnCpyS (
        mCache[i].path,
        sizeof (mCache[i].path),
        path,
        sizeof (mCache[i].path) - 1
        );
      mCache[i].used  = 1;
      mCache[i].data  = NULL;
      mCache[i].len   = 0;
      mCache[i].dirty = 0;
      return &mCache[i];
    }
  }

  return NULL;
}

STATIC
INT32
MetalEspDirFind (
  CONST CHAR8  *path
  )
{
  UINTN  i;

  for (i = 0; i < PM_METAL_ESP_DIR_MAX; i++) {
    if (mDirs[i][0] != '\0' && MetalEspPathEq (mDirs[i], path)) {
      return 1;
    }
  }

  return 0;
}

STATIC
INT32
MetalEspDirAdd (
  CONST CHAR8  *path
  )
{
  UINTN  i;

  if (path == NULL || path[0] == '\0') {
    return -1;
  }

  if (MetalEspDirFind (path)) {
    return 0;
  }

  for (i = 0; i < PM_METAL_ESP_DIR_MAX; i++) {
    if (mDirs[i][0] == '\0') {
      AsciiStrnCpyS (
        mDirs[i],
        sizeof (mDirs[i]),
        path,
        sizeof (mDirs[i]) - 1
        );
      return 0;
    }
  }

  return -1;
}

STATIC
VOID
MetalEspDirRemovePrefix (
  CONST CHAR8  *path
  )
{
  UINTN  i;

  for (i = 0; i < PM_METAL_ESP_DIR_MAX; i++) {
    if (mDirs[i][0] != '\0'
        && (MetalEspPathEq (mDirs[i], path)
            || MetalEspIsPrefix (path, mDirs[i])))
    {
      mDirs[i][0] = '\0';
    }
  }
}

STATIC
INT32
MetalEspCacheStore (
  CONST CHAR8   *path,
  CONST UINT8   *data,
  UINT32         len,
  INT32          dirty
  )
{
  metal_esp_cache_t  *ent;
  UINT8              *copy;

  ent = MetalEspCacheSlot (path);
  if (ent == NULL) {
    return -1;
  }

  copy = NULL;
  if (len > 0) {
    if (data == NULL) {
      return -1;
    }

    copy = (UINT8 *)pm_metal_mem_alloc (
                      len,
                      PM_METAL_MEM_HEAP,
                      PM_METAL_MEM_ID_NONE
                      );
    if (copy == NULL) {
      return -1;
    }

    CopyMem (copy, data, len);
  }

  if (ent->data != NULL) {
    pm_metal_mem_free (ent->data);
  }

  ent->data  = copy;
  ent->len   = len;
  ent->dirty = dirty;
  return 0;
}

STATIC
INT32
MetalEspCacheEnsure (
  CONST CHAR8  *path
  )
{
  metal_esp_cache_t  *ent;
  UINT8              *buf;
  UINT32              len;

  ent = MetalEspCacheFind (path);
  if (ent != NULL) {
    return 0;
  }

  buf = NULL;
  len = 0;
  if (pm_metal_esp_read_file_port (path, &buf, &len) != 0) {
    return -1;
  }

  if (MetalEspCacheStore (path, buf, len, 0) != 0) {
    if (buf != NULL) {
      pm_metal_mem_free (buf);
    }

    return -1;
  }

  if (buf != NULL) {
    pm_metal_mem_free (buf);
  }

  return 0;
}

STATIC
INT32
MetalEspNameInList (
  CHAR8         names[][PM_METAL_ESP_PATH_MAX],
  UINTN         count,
  CONST CHAR8  *name
  )
{
  UINTN  j;

  for (j = 0; j < count; j++) {
    if (MetalEspPathEq (names[j], name)) {
      return 1;
    }
  }

  return 0;
}

STATIC
INT32
MetalEspReaddirCollect (
  CONST CHAR8  *dir,
  CHAR8         names[][PM_METAL_ESP_PATH_MAX],
  UINT32       *count
  )
{
  UINTN   i;
  UINTN   o;
  UINTN   dlen;
  UINT32  pi;

  if (dir == NULL || names == NULL || count == NULL) {
    return -1;
  }

  *count = 0;
  o      = 0;
  dlen   = AsciiStrLen (dir);

  for (i = 0; i < PM_METAL_ESP_DIR_MAX && o < PM_METAL_ESP_READDIR_MAX; i++) {
    CONST CHAR8  *sub;

    if (mDirs[i][0] == '\0' || !MetalEspIsPrefix (dir, mDirs[i])) {
      continue;
    }

    sub = mDirs[i] + dlen + 1;
    if (sub[0] == '\0' || AsciiStrStr (sub, "/") != NULL) {
      continue;
    }

    if (MetalEspNameInList (names, o, sub)) {
      continue;
    }

    AsciiStrnCpyS (names[o], PM_METAL_ESP_PATH_MAX, sub, PM_METAL_ESP_PATH_MAX - 1);
    o++;
  }

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX && o < PM_METAL_ESP_READDIR_MAX; i++) {
    CONST CHAR8  *sub;

    if (!mCache[i].used || !MetalEspIsPrefix (dir, mCache[i].path)) {
      continue;
    }

    sub = mCache[i].path + dlen + 1;
    if (sub[0] == '\0' || AsciiStrStr (sub, "/") != NULL) {
      continue;
    }

    if (MetalEspNameInList (names, o, sub)) {
      continue;
    }

    AsciiStrnCpyS (names[o], PM_METAL_ESP_PATH_MAX, sub, PM_METAL_ESP_PATH_MAX - 1);
    o++;
  }

  for (pi = 0; o < PM_METAL_ESP_READDIR_MAX; pi++) {
    CHAR8  port_name[PM_METAL_ESP_PATH_MAX];
    INT32  rc;

    rc = pm_metal_esp_readdir_port (
           dir,
           pi,
           port_name,
           sizeof (port_name)
           );
    if (rc <= 0) {
      break;
    }

    if (MetalEspNameInList (names, o, port_name)) {
      continue;
    }

    AsciiStrnCpyS (names[o], PM_METAL_ESP_PATH_MAX, port_name, PM_METAL_ESP_PATH_MAX - 1);
    o++;
  }

  *count = (UINT32)o;
  return 0;
}

int
pm_metal_esp_init (
  VOID  *image_handle
  )
{
  if (mReady) {
    return 0;
  }

  ZeroMem (mCache, sizeof (mCache));
  ZeroMem (mDirs, sizeof (mDirs));
  /*
   * EFI: volume bind required when image_handle is set.
   * BIOS: hw is a no-op; cache-only is fine.
   */
  if (pm_metal_esp_init_port (image_handle) != 0 && image_handle != NULL) {
    return -1;
  }

  mReady = 1;
  return 0;
}

int
pm_metal_esp_ready (
  VOID
  )
{
  return mReady ? 1 : 0;
}

void
pm_metal_esp_set_loaded_identity (
  CONST CHAR8   *path,
  CONST VOID    *base,
  UINT32         size
  )
{
  UINTN  i;

  mLoadedPath[0]   = '\0';
  mLoadedImageBase = base;
  mLoadedImageSize = size;
  if (path == NULL || path[0] == '\0') {
    return;
  }

  for (i = 0; i + 1 < PM_METAL_ESP_PATH_MAX && path[i] != '\0'; i++) {
    CHAR8  c;

    c = path[i];
    if (c == '\\') {
      c = '/';
    }

    mLoadedPath[i] = c;
  }

  mLoadedPath[i] = '\0';
}

CONST CHAR8 *
pm_metal_esp_loaded_path (
  VOID
  )
{
  return (mLoadedPath[0] != '\0') ? mLoadedPath : NULL;
}

int
pm_metal_esp_loaded_image (
  CONST VOID  **base,
  UINT32       *size
  )
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

int
pm_metal_esp_cache_put (
  CONST CHAR8   *path,
  CONST UINT8   *data,
  UINT32         len
  )
{
  if (!mReady || path == NULL) {
    return -1;
  }

  return MetalEspCacheStore (path, data, len, 0);
}

int
pm_metal_esp_preload (
  CONST CHAR8  *path
  )
{
  UINT8   *buf;
  UINT32   len;

  if (!mReady || path == NULL) {
    return -1;
  }

  buf = NULL;
  len = 0;
  if (pm_metal_esp_read_file_port (path, &buf, &len) != 0) {
    return -1;
  }

  if (MetalEspCacheStore (path, buf, len, 0) != 0) {
    if (buf != NULL) {
      pm_metal_mem_free (buf);
    }

    return -1;
  }

  if (buf != NULL) {
    pm_metal_mem_free (buf);
  }

  return 0;
}

int
pm_metal_esp_preload_tree (
  CONST CHAR8  *dir
  )
{
  UINT32  idx;
  UINT32  n;
  UINT32  ty;
  UINT32  sz;
  CHAR8   name[PM_METAL_ESP_PATH_MAX];
  CHAR8   child[PM_METAL_ESP_PATH_MAX];

  if (!mReady || dir == NULL || dir[0] == '\0') {
    return -1;
  }

  if (pm_metal_esp_stat (dir, &sz, &ty) != 0
      || ty != PM_METAL_ESP_TYPE_DIR)
  {
    return -1;
  }

  n = 0;
  for (idx = 0; ; idx++) {
    INT32  rc;

    rc = pm_metal_esp_readdir (dir, idx, name, sizeof (name));
    if (rc <= 0) {
      break;
    }

    if (name[0] == '.'
        && (name[1] == '\0'
            || (name[1] == '.' && name[2] == '\0')))
    {
      continue;
    }

    if (AsciiSPrint (
          child,
          sizeof (child),
          "%a/%a",
          dir,
          name
          ) >= sizeof (child))
    {
      continue;
    }

    if (pm_metal_esp_stat (child, &sz, &ty) != 0) {
      continue;
    }

    if (ty == PM_METAL_ESP_TYPE_DIR) {
      if (pm_metal_esp_preload_tree (child) == 0) {
        n++;
      }
    } else if (ty == PM_METAL_ESP_TYPE_FILE) {
      if (pm_metal_esp_preload (child) == 0) {
        n++;
      }
    }
  }

  return (n > 0u) ? 0 : -1;
}

int
pm_metal_esp_file_size (
  CONST CHAR8  *path,
  UINT32       *len
  )
{
  metal_esp_cache_t  *ent;

  if (len == NULL || path == NULL) {
    return -1;
  }

  *len = 0;
  ent  = MetalEspCacheFind (path);
  if (ent != NULL) {
    *len = ent->len;
    return 0;
  }

  return pm_metal_esp_file_size_port (path, len);
}

int
pm_metal_esp_read_file (
  CONST CHAR8   *path,
  UINT8        **out,
  UINT32        *len
  )
{
  metal_esp_cache_t  *ent;
  UINT8              *Buf;

  if (out == NULL || len == NULL || path == NULL) {
    return -1;
  }

  *out = NULL;
  *len = 0;

  ent = MetalEspCacheFind (path);
  if (ent != NULL) {
    if (ent->len == 0) {
      return 0;
    }

    Buf = (UINT8 *)pm_metal_mem_alloc (
                     ent->len,
                     PM_METAL_MEM_HEAP,
                     PM_METAL_MEM_ID_NONE
                     );
    if (Buf == NULL) {
      return -1;
    }

    CopyMem (Buf, ent->data, ent->len);
    *out = Buf;
    *len = ent->len;
    return 0;
  }

  return pm_metal_esp_read_file_port (path, out, len);
}

int
pm_metal_esp_write_file (
  CONST CHAR8   *path,
  CONST UINT8   *data,
  UINT32         len
  )
{
  if (path == NULL) {
    return -1;
  }

  if (len > 0 && data == NULL) {
    return -1;
  }

  if (MetalEspCacheStore (path, data, len, 1) != 0) {
    return -1;
  }

  (VOID)pm_metal_esp_write_file_port (path, data, len);
  if (MetalEspCacheFind (path) != NULL) {
    MetalEspCacheFind (path)->dirty = 0;
  }

  return 0;
}

STATIC
INT32
MetalEspHasChildren (
  CONST CHAR8  *dir
  )
{
  UINTN  i;

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (mCache[i].used && MetalEspIsPrefix (dir, mCache[i].path)) {
      return 1;
    }
  }

  for (i = 0; i < PM_METAL_ESP_DIR_MAX; i++) {
    if (mDirs[i][0] != '\0' && MetalEspIsPrefix (dir, mDirs[i])) {
      return 1;
    }
  }

  return 0;
}

int
pm_metal_esp_stat (
  CONST CHAR8  *path,
  UINT32       *size,
  UINT32       *type
  )
{
  metal_esp_cache_t  *ent;

  if (path == NULL || size == NULL || type == NULL) {
    return -1;
  }

  *size = 0;
  *type = PM_METAL_ESP_TYPE_FILE;

  if (MetalEspDirFind (path)) {
    *type = PM_METAL_ESP_TYPE_DIR;
    return 0;
  }

  if (MetalEspHasChildren (path)) {
    *type = PM_METAL_ESP_TYPE_DIR;
    return 0;
  }

  ent = MetalEspCacheFind (path);
  if (ent != NULL) {
    *size = ent->len;
    *type = PM_METAL_ESP_TYPE_FILE;
    return 0;
  }

  return pm_metal_esp_stat_port (path, size, type);
}

int
pm_metal_esp_read_at (
  CONST CHAR8  *path,
  UINT32        off,
  UINT8        *buf,
  UINT32        len,
  UINT32       *nread
  )
{
  metal_esp_cache_t  *ent;
  UINT32              avail;

  if (path == NULL || buf == NULL || nread == NULL) {
    return -1;
  }

  *nread = 0;
  if (len == 0) {
    return 0;
  }

  ent = MetalEspCacheFind (path);
  if (ent == NULL) {
    if (MetalEspCacheEnsure (path) != 0) {
      return -1;
    }

    ent = MetalEspCacheFind (path);
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

  CopyMem (buf, ent->data + off, len);
  *nread = len;
  return 0;
}

int
pm_metal_esp_write_at (
  CONST CHAR8   *path,
  UINT32         off,
  CONST UINT8   *data,
  UINT32         len,
  INT32          truncate
  )
{
  metal_esp_cache_t  *ent;
  UINT32              new_len;
  UINT8              *copy;

  if (path == NULL || (len > 0 && data == NULL)) {
    return -1;
  }

  ent = MetalEspCacheFind (path);
  if (ent == NULL) {
    ent = MetalEspCacheSlot (path);
  }

  if (ent == NULL) {
    return -1;
  }

  if (truncate) {
    if (MetalEspCacheStore (path, NULL, 0, 1) != 0) {
      return -1;
    }

    ent = MetalEspCacheFind (path);
    if (ent == NULL) {
      return -1;
    }
  }

  new_len = off + len;
  if (new_len < ent->len) {
    new_len = ent->len;
  }

  copy = (UINT8 *)pm_metal_mem_alloc (
                     new_len,
                     PM_METAL_MEM_HEAP,
                     PM_METAL_MEM_ID_NONE
                     );
  if (copy == NULL) {
    return -1;
  }

  if (ent->len > 0 && ent->data != NULL) {
    CopyMem (copy, ent->data, ent->len);
  }

  if (len > 0) {
    CopyMem (copy + off, data, len);
  }

  if (ent->data != NULL) {
    pm_metal_mem_free (ent->data);
  }

  ent->data  = copy;
  ent->len   = new_len;
  ent->dirty = 1;
  return 0;
}

int
pm_metal_esp_fsync (
  CONST CHAR8  *path
  )
{
  metal_esp_cache_t  *ent;

  if (path == NULL) {
    return -1;
  }

  ent = MetalEspCacheFind (path);
  if (ent == NULL || !ent->dirty) {
    return pm_metal_esp_fsync_port (path);
  }

  if (pm_metal_esp_write_file_port (path, ent->data, ent->len) != 0) {
    return -1;
  }

  ent->dirty = 0;
  return pm_metal_esp_fsync_port (path);
}

int
pm_metal_esp_mkdir (
  CONST CHAR8  *path
  )
{
  if (path == NULL) {
    return -1;
  }

  if (MetalEspDirAdd (path) != 0) {
    return -1;
  }

  (VOID)pm_metal_esp_mkdir_port (path);
  return 0;
}

int
pm_metal_esp_unlink (
  CONST CHAR8  *path
  )
{
  metal_esp_cache_t  *ent;
  UINTN               i;

  if (path == NULL) {
    return -1;
  }

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (mCache[i].used && MetalEspPathEq (mCache[i].path, path)) {
      ent = &mCache[i];
      if (ent->data != NULL) {
        pm_metal_mem_free (ent->data);
      }

      ZeroMem (ent, sizeof (*ent));
      break;
    }
  }

  MetalEspDirRemovePrefix (path);
  (VOID)pm_metal_esp_unlink_port (path);
  return 0;
}

int
pm_metal_esp_rename (
  CONST CHAR8  *old_path,
  CONST CHAR8  *new_path
  )
{
  metal_esp_cache_t  *ent;
  UINTN               i;

  if (old_path == NULL || new_path == NULL) {
    return -1;
  }

  ent = MetalEspCacheFind (old_path);
  if (ent != NULL) {
    metal_esp_cache_t  *dup;

    dup = MetalEspCacheSlot (new_path);
    if (dup == NULL) {
      return -1;
    }

    if (dup->data != NULL) {
      pm_metal_mem_free (dup->data);
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
    if (mDirs[i][0] != '\0' && MetalEspPathEq (mDirs[i], old_path)) {
      AsciiStrnCpyS (
        mDirs[i],
        sizeof (mDirs[i]),
        new_path,
        sizeof (mDirs[i]) - 1
        );
    }
  }

  (VOID)pm_metal_esp_rename_port (old_path, new_path);
  return 0;
}

int
pm_metal_esp_readdir (
  CONST CHAR8  *path,
  UINT32        index,
  CHAR8        *name,
  UINT32        name_cap
  )
{
  CHAR8   names[PM_METAL_ESP_READDIR_MAX][PM_METAL_ESP_PATH_MAX];
  UINT32  count;

  if (path == NULL || name == NULL || name_cap == 0) {
    return -1;
  }

  if (MetalEspReaddirCollect (path, names, &count) != 0) {
    return -1;
  }

  if (index >= count) {
    return 0;
  }

  AsciiStrnCpyS (name, name_cap, names[index], name_cap - 1);
  return 1;
}

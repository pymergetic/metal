/**
 * @file Minimal read-only STORED-zip member accessor for stdlib.zip imports.
 * py_port_stubs.c's mp_import_stat/mp_lexer_new_from_file hand any sys.path
 * lookup that crosses a ".zip/" boundary to PyZipStatPath/PyZipReadPath
 * instead of a plain ESP file lookup — the archive is read in place, never
 * extracted to loose files. Deliberately supports compression method 0
 * (stored) only: mods/py/build_stdlib_zip.sh always packs with `zip -X0`
 * for exactly this reason, and a compressed entry is treated as a hard
 * "unsupported" error rather than silently misreading garbage.
 */
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include "py_zip_read.h"

#define ZIP_EOCD_SIG        0x06054b50u
#define ZIP_CDIR_SIG        0x02014b50u
#define ZIP_EOCD_BYTES      22u
#define ZIP_CDIR_HDR_BYTES  46u
#define ZIP_LOCAL_HDR_BYTES 30u
#define ZIP_CD_MAX_BYTES    (4u * 1024u * 1024u)

typedef struct {
  uint32_t cd_off;
  uint32_t cd_size;
} zip_eocd_t;

typedef struct {
  uint16_t method;
  uint32_t comp_size;
  uint32_t local_off;
} zip_entry_t;

static uint32_t RdU32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t RdU16(const uint8_t *p)
{
  return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static int32_t ZipSplitPath(const char  *path,
                            char        *zip_out,
                            size_t       zip_cap,
                            const char **member_out)
{
  const char *p;
  size_t      zlen;

  if (path == NULL) {
    return -1;
  }

  /* MicroPython-side paths (sys.path entries + qstr filenames) always carry
   * a leading '/', but the ESP cache indexes everything without one (see
   * fs.c's MetalFsCleanPath, which every other pm_metal_fs_* caller goes
   * through) — strip it here so pm_metal_esp_file_size/_read_at below hit
   * the same cache keys the preload/fetch path wrote under. */
  while (*path == '/') {
    path++;
  }

  p = strstr(path, ".zip/");
  if (p == NULL) {
    return -1;
  }

  zlen = (size_t)(p - path) + 4u; /* keep the ".zip" itself */
  if (zlen == 0 || zlen >= zip_cap) {
    return -1;
  }

  memcpy(zip_out, path, zlen);
  zip_out[zlen] = '\0';
  *member_out   = p + 5u; /* skip ".zip/" */
  return 0;
}

static int32_t ZipReadEocd(const char *zip_path, zip_eocd_t *out)
{
  uint32_t sz;
  uint8_t  buf[ZIP_EOCD_BYTES];
  uint32_t nread;

  sz = 0;
  if (pm_metal_esp_file_size(zip_path, &sz) != 0 || sz < ZIP_EOCD_BYTES) {
    return -1;
  }
  if (pm_metal_esp_read_at(zip_path, sz - ZIP_EOCD_BYTES, buf, ZIP_EOCD_BYTES, &nread) != 0 ||
      nread != ZIP_EOCD_BYTES) {
    return -1;
  }
  if (RdU32(&buf[0]) != ZIP_EOCD_SIG) {
    return -1; /* zip comment present (we never write one) or not a zip we made */
  }

  out->cd_size = RdU32(&buf[12]);
  out->cd_off  = RdU32(&buf[16]);
  return 0;
}

/* -1 error, 0 not found, 1 exact file, 2 dir prefix seen (only meaningful
 * when the exact-match search itself misses). *out is filled on rc==1. */
static int32_t ZipFindEntry(const char *zip_path, const char *member, zip_entry_t *out)
{
  zip_eocd_t eocd;
  uint8_t   *cd;
  uint32_t   nread;
  uint32_t   off;
  size_t     member_len;
  int32_t    found_dir;

  if (ZipReadEocd(zip_path, &eocd) != 0) {
    return -1;
  }
  if (eocd.cd_size == 0 || eocd.cd_size > ZIP_CD_MAX_BYTES) {
    return -1;
  }

  cd = (uint8_t *)pm_metal_mem_alloc(eocd.cd_size, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (cd == NULL) {
    return -1;
  }
  if (pm_metal_esp_read_at(zip_path, eocd.cd_off, cd, eocd.cd_size, &nread) != 0 ||
      nread != eocd.cd_size) {
    pm_metal_mem_free(cd);
    return -1;
  }

  member_len = strlen(member);
  found_dir  = 0;
  off        = 0;
  while (off + ZIP_CDIR_HDR_BYTES <= eocd.cd_size) {
    uint16_t    name_len;
    uint16_t    extra_len;
    uint16_t    comment_len;
    const char *name;

    if (RdU32(&cd[off]) != ZIP_CDIR_SIG) {
      break;
    }

    name_len    = RdU16(&cd[off + 28]);
    extra_len   = RdU16(&cd[off + 30]);
    comment_len = RdU16(&cd[off + 32]);
    name        = (const char *)&cd[off + ZIP_CDIR_HDR_BYTES];

    if ((uint32_t)(ZIP_CDIR_HDR_BYTES + name_len) > eocd.cd_size - off) {
      break;
    }

    if ((size_t)name_len == member_len && memcmp(name, member, member_len) == 0) {
      out->method    = RdU16(&cd[off + 10]);
      out->comp_size = RdU32(&cd[off + 20]);
      out->local_off = RdU32(&cd[off + 42]);
      pm_metal_mem_free(cd);
      return 1;
    }
    if ((size_t)name_len > member_len && name[member_len] == '/' &&
        memcmp(name, member, member_len) == 0) {
      found_dir = 1;
    }

    off += (uint32_t)ZIP_CDIR_HDR_BYTES + name_len + extra_len + comment_len;
  }

  pm_metal_mem_free(cd);
  return found_dir ? 2 : 0;
}

int32_t PyZipStatPath(const char *path, uint32_t *out_size)
{
  char        zip_path[192];
  const char *member;
  zip_entry_t entry;
  int32_t     rc;

  if (ZipSplitPath(path, zip_path, sizeof(zip_path), &member) != 0) {
    return -1;
  }

  rc = ZipFindEntry(zip_path, member, &entry);
  if (rc < 0) {
    return PY_ZIP_STAT_ERROR;
  }
  if (rc == 1) {
    if (entry.method != 0) {
      return PY_ZIP_STAT_ERROR; /* compressed entry: unsupported, not "missing" */
    }
    if (out_size != NULL) {
      *out_size = entry.comp_size;
    }
    return PY_ZIP_STAT_FILE;
  }

  return (rc == 2) ? PY_ZIP_STAT_DIR : PY_ZIP_STAT_MISSING;
}

uint32_t PyZipReadPath(const char *path, void *dest, uint32_t dest_len)
{
  char        zip_path[192];
  const char *member;
  zip_entry_t entry;
  uint8_t     local_hdr[ZIP_LOCAL_HDR_BYTES];
  uint32_t    nread;
  uint32_t    data_off;
  uint32_t    n;

  if (dest == NULL || dest_len == 0) {
    return 0;
  }
  if (ZipSplitPath(path, zip_path, sizeof(zip_path), &member) != 0) {
    return 0;
  }
  if (ZipFindEntry(zip_path, member, &entry) != 1 || entry.method != 0) {
    return 0;
  }

  if (pm_metal_esp_read_at(zip_path, entry.local_off, local_hdr, ZIP_LOCAL_HDR_BYTES, &nread) !=
        0 ||
      nread != ZIP_LOCAL_HDR_BYTES) {
    return 0;
  }

  data_off = entry.local_off + ZIP_LOCAL_HDR_BYTES + RdU16(&local_hdr[26]) + RdU16(&local_hdr[28]);
  n        = (entry.comp_size < dest_len) ? entry.comp_size : dest_len;
  if (n == 0) {
    return 0;
  }
  if (pm_metal_esp_read_at(zip_path, data_off, (uint8_t *)dest, n, &nread) != 0) {
    return 0;
  }

  return nread;
}

/** @file
  pm_metal_iface_esp_install() — scan mods/apps/<app>/iface.list and
  register each listed lz4(ustar) blob. Soft-fail only; external apps
  (e.g. metal-doom) stage packs beside their wasm with zero Metal
  app-specific bake-in. See docs/IFACE.md.
**/
#include <pymergetic/metal/util/iface.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#define IFACE_ESP_LINE_MAX 256u
#define IFACE_ESP_NAME_MAX 64u
#define IFACE_ESP_VER_MAX  64u
#define IFACE_ESP_BLOB_MAX 96u
#define IFACE_ESP_PATH_MAX 160u

static int32_t KindParse(const char *s, pm_metal_iface_pkg_kind_t *out)
{
  if (s == NULL || out == NULL) {
    return -1;
  }
  if (strcmp(s, "h") == 0) {
    *out = PM_METAL_IFACE_PKG_H;
    return 0;
  }
  if (strcmp(s, "sysroot") == 0) {
    *out = PM_METAL_IFACE_PKG_SYSROOT;
    return 0;
  }
  if (strcmp(s, "c") == 0) {
    *out = PM_METAL_IFACE_PKG_C;
    return 0;
  }
  if (strcmp(s, "meta") == 0) {
    *out = PM_METAL_IFACE_PKG_META;
    return 0;
  }
  if (strcmp(s, "pyi") == 0) {
    *out = PM_METAL_IFACE_PKG_PYI;
    return 0;
  }
  if (strcmp(s, "py") == 0) {
    *out = PM_METAL_IFACE_PKG_PY;
    return 0;
  }
  return -1;
}

/* Reject path separators / ".." so blob names stay under the app dir. */
static int32_t BlobNameOk(const char *blob)
{
  if (blob == NULL || blob[0] == '\0') {
    return 0;
  }
  if (strchr(blob, '/') != NULL || strchr(blob, '\\') != NULL) {
    return 0;
  }
  if (strstr(blob, "..") != NULL) {
    return 0;
  }
  return 1;
}

/*
 * Parse one iface.list line:
 *   name kind version uncompressed_len blob_filename
 * Mutates line in place (NUL-splits fields). Returns 0 on success.
 */
static int32_t ParseLine(char                      *line,
                         char                      *name,
                         uint32_t                   name_cap,
                         pm_metal_iface_pkg_kind_t *kind,
                         char                      *version,
                         uint32_t                   version_cap,
                         uint32_t                  *uncompressed_len,
                         char                      *blob,
                         uint32_t                   blob_cap)
{
  char    *fields[5];
  char    *p;
  uint32_t n;

  n = 0u;
  p = line;
  while (*p != '\0' && n < 5u) {
    while (*p == ' ' || *p == '\t') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    fields[n++] = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
      p++;
    }
    if (*p != '\0') {
      *p = '\0';
      p++;
    }
  }
  if (n != 5u) {
    return -1;
  }

  if (KindParse(fields[1], kind) != 0) {
    return -1;
  }
  *uncompressed_len = (uint32_t)strtoul(fields[3], NULL, 10);
  if (*uncompressed_len == 0u || !BlobNameOk(fields[4])) {
    return -1;
  }

  snprintf(name, name_cap, "%s", fields[0]);
  snprintf(version, version_cap, "%s", fields[2]);
  snprintf(blob, blob_cap, "%s", fields[4]);
  if (name[0] == '\0' || blob[0] == '\0') {
    return -1;
  }
  return 0;
}

static void InstallListForApp(const char *app)
{
  char     list_path[IFACE_ESP_PATH_MAX];
  char     blob_path[IFACE_ESP_PATH_MAX];
  uint8_t *list_buf = NULL;
  uint32_t list_len = 0u;
  char    *p;
  char     msg[192];

  snprintf(list_path, sizeof(list_path), "mods/apps/%s/iface.list", app);
  if (pm_metal_esp_read_file(list_path, &list_buf, &list_len) != 0) {
    return;
  }
  if (list_len == 0u || list_buf == NULL) {
    pm_metal_mem_free(list_buf);
    return;
  }

  /* Ensure NUL terminator — esp_read_file may return exact file length. */
  {
    uint8_t *grown =
      (uint8_t *)pm_metal_mem_alloc(list_len + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (grown == NULL) {
      pm_metal_mem_free(list_buf);
      return;
    }
    memcpy(grown, list_buf, list_len);
    grown[list_len] = '\0';
    pm_metal_mem_free(list_buf);
    list_buf = grown;
  }

  p = (char *)list_buf;
  while (*p != '\0') {
    char                      line[IFACE_ESP_LINE_MAX];
    char                      name[IFACE_ESP_NAME_MAX];
    char                      version[IFACE_ESP_VER_MAX];
    char                      blob[IFACE_ESP_BLOB_MAX];
    pm_metal_iface_pkg_kind_t kind;
    uint32_t                  uncompressed_len;
    uint8_t                  *blob_data = NULL;
    uint32_t                  blob_len  = 0u;
    char                     *nl;
    size_t                    linelen;

    nl = strchr(p, '\n');
    if (nl != NULL) {
      linelen = (size_t)(nl - p);
    } else {
      linelen = strlen(p);
    }
    if (linelen >= sizeof(line)) {
      linelen = sizeof(line) - 1u;
    }
    memcpy(line, p, linelen);
    line[linelen] = '\0';
    if (nl != NULL) {
      p = nl + 1;
    } else {
      p += strlen(p);
    }
    if (linelen > 0u && line[linelen - 1u] == '\r') {
      line[linelen - 1u] = '\0';
    }

    {
      char *s = line;
      while (*s == ' ' || *s == '\t') {
        s++;
      }
      if (s[0] == '\0' || s[0] == '#') {
        continue;
      }
      if (ParseLine(s,
                    name,
                    sizeof(name),
                    &kind,
                    version,
                    sizeof(version),
                    &uncompressed_len,
                    blob,
                    sizeof(blob)) != 0) {
        snprintf(msg, sizeof(msg), "iface-esp: skip bad line in %s", list_path);
        pm_metal_log(msg);
        continue;
      }
    }

    /* Idempotent: EFI main installs pre-EBS; wasm.c may call again. */
    {
      uint32_t                  i;
      uint32_t                  npkg;
      pm_metal_iface_pkg_info_t info;
      int32_t                   already = 0;

      npkg = (uint32_t)pm_metal_iface_pkg_count();
      for (i = 0u; i < npkg; i++) {
        if (pm_metal_iface_pkg_at(i, &info) == 0 && info.name != NULL &&
            strcmp(info.name, name) == 0) {
          already = 1;
          break;
        }
      }
      if (already) {
        continue;
      }
    }

    snprintf(blob_path, sizeof(blob_path), "mods/apps/%s/%s", app, blob);
    if (pm_metal_esp_read_file(blob_path, &blob_data, &blob_len) != 0 || blob_data == NULL ||
        blob_len == 0u) {
      snprintf(msg, sizeof(msg), "iface-esp: missing blob %s", blob_path);
      pm_metal_log(msg);
      continue;
    }

    if (pm_metal_iface_pkg_register(
          name, kind, version, "", blob_data, blob_len, uncompressed_len) != 0) {
      snprintf(msg, sizeof(msg), "iface-esp: register failed %s", name);
      pm_metal_log(msg);
    } else {
      snprintf(msg, sizeof(msg), "iface-esp: registered %s", name);
      pm_metal_log(msg);
    }
    pm_metal_mem_free(blob_data);
  }

  pm_metal_mem_free(list_buf);
}

void pm_metal_iface_esp_install(void)
{
  char     name[IFACE_ESP_NAME_MAX];
  uint32_t idx;

  if (!pm_metal_esp_ready()) {
    return;
  }

  for (idx = 0u;; idx++) {
    uint32_t size = 0u;
    uint32_t type = 0u;
    char     app_path[IFACE_ESP_PATH_MAX];
    int32_t  rc;

    rc = (int32_t)pm_metal_esp_readdir("mods/apps", idx, name, sizeof(name));
    if (rc <= 0) {
      break;
    }
    if (name[0] == '\0' || name[0] == '.') {
      continue;
    }
    snprintf(app_path, sizeof(app_path), "mods/apps/%s", name);
    if (pm_metal_esp_stat(app_path, &size, &type) != 0 || type != PM_METAL_ESP_TYPE_DIR) {
      continue;
    }
    InstallListForApp(name);
  }
}

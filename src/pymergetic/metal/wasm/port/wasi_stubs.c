/*
 * Minimal wasi_snapshot_preview1 stubs for guests still linked against
 * wasi-libc. Not a product WASI surface - stdout/err go to Metal log;
 * path/fd ops return BADF/NOENT so guests use Metal FS.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/shell/__init__.h>

#include "wasm_export.h"

#define WASI_ESUCCESS 0
#define WASI_EBADF 8
#define WASI_EAGAIN 6
#define WASI_EINVAL 28
#define WASI_ENOENT 44
#define WASI_ENOTCAPABLE 76

#define WASI_FILETYPE_CHARACTER_DEVICE 2

typedef struct {
  uint8_t filetype;
  uint8_t pad0;
  uint16_t flags;
  uint64_t rights_base;
  uint64_t rights_inheriting;
} wasi_fdstat_t;

typedef struct {
  uint32_t buf;
  uint32_t buf_len;
} wasi_ciovec_guest_t;

static int is_stdio(int32_t fd)
{
  return fd == 0 || fd == 1 || fd == 2;
}

static int32_t wasi_fd_close(wasm_exec_env_t exec_env, int32_t fd)
{
  (void)exec_env;
  (void)fd;
  return WASI_ESUCCESS;
}

static int32_t wasi_fd_fdstat_get(wasm_exec_env_t exec_env, int32_t fd,
                                  wasi_fdstat_t *st)
{
  (void)exec_env;
  if (st == NULL) {
    return WASI_EINVAL;
  }
  if (!is_stdio(fd)) {
    return WASI_EBADF;
  }
  memset(st, 0, sizeof(*st));
  st->filetype = WASI_FILETYPE_CHARACTER_DEVICE;
  st->rights_base = 0xffffffffffffffffull;
  st->rights_inheriting = 0xffffffffffffffffull;
  return WASI_ESUCCESS;
}

static int32_t wasi_fd_fdstat_set_flags(wasm_exec_env_t exec_env, int32_t fd,
                                        int32_t flags)
{
  (void)exec_env;
  (void)flags;
  return is_stdio(fd) ? WASI_ESUCCESS : WASI_EBADF;
}

static int32_t wasi_fd_prestat_get(wasm_exec_env_t exec_env, int32_t fd,
                                   void *out)
{
  (void)exec_env;
  (void)fd;
  (void)out;
  return WASI_EBADF;
}

static int32_t wasi_fd_prestat_dir_name(wasm_exec_env_t exec_env, int32_t fd,
                                        char *path, uint32_t path_len)
{
  (void)exec_env;
  (void)fd;
  (void)path;
  (void)path_len;
  return WASI_EBADF;
}

static int32_t wasi_fd_read(wasm_exec_env_t exec_env, int32_t fd, void *iovs,
                            uint32_t iovs_len, uint32_t *nread)
{
  (void)exec_env;
  (void)iovs;
  (void)iovs_len;
  if (nread != NULL) {
    *nread = 0;
  }
  return is_stdio(fd) ? WASI_EAGAIN : WASI_EBADF;
}

static int32_t wasi_fd_seek(wasm_exec_env_t exec_env, int32_t fd, int64_t offset,
                            int32_t whence, uint64_t *newoffset)
{
  (void)exec_env;
  (void)offset;
  (void)whence;
  if (newoffset != NULL) {
    *newoffset = 0;
  }
  return is_stdio(fd) ? WASI_ESUCCESS : WASI_EBADF;
}

static int32_t wasi_fd_write(wasm_exec_env_t exec_env, int32_t fd, void *iovs,
                             uint32_t iovs_len, uint32_t *nwritten)
{
  wasm_module_inst_t inst;
  uint32_t total;
  uint32_t i;
  wasi_ciovec_guest_t *vec;

  if (nwritten != NULL) {
    *nwritten = 0;
  }
  if (fd != 1 && fd != 2) {
    return WASI_EBADF;
  }
  if (iovs == NULL || iovs_len == 0) {
    return WASI_ESUCCESS;
  }
  inst = wasm_runtime_get_module_inst(exec_env);
  vec = (wasi_ciovec_guest_t *)iovs;
  total = 0;
  for (i = 0; i < iovs_len; i++) {
    uint32_t off;
    uint32_t len;
    uint32_t n;
    char *p;
    char line[240];

    off = vec[i].buf;
    len = vec[i].buf_len;
    if (len == 0u) {
      continue;
    }
    p = (char *)wasm_runtime_addr_app_to_native(inst, (uint64_t)off);
    if (p == NULL) {
      return WASI_EINVAL;
    }
    n = len;
    if (n > sizeof(line) - 1u) {
      n = (uint32_t)(sizeof(line) - 1u);
    }
    memcpy(line, p, n);
    line[n] = '\0';
    while (n > 0u && (line[n - 1u] == '\n' || line[n - 1u] == '\r')) {
      n--;
      line[n] = '\0';
    }
    if (n > 0u) {
      pm_metal_shell_log((const uint8_t *)line);
    }
    total += len;
  }
  if (nwritten != NULL) {
    *nwritten = total;
  }
  return WASI_ESUCCESS;
}

static int32_t wasi_path_create_directory(wasm_exec_env_t exec_env, int32_t fd,
                                          char *path, uint32_t path_len)
{
  (void)exec_env;
  (void)fd;
  (void)path;
  (void)path_len;
  return WASI_ENOTCAPABLE;
}

static int32_t wasi_path_open(wasm_exec_env_t exec_env, int32_t dirfd,
                              int32_t dirflags, char *path, uint32_t path_len,
                              int32_t oflags, int64_t fs_rights_base,
                              int64_t fs_rights_inheriting, int32_t fdflags,
                              int32_t *fd_out)
{
  (void)exec_env;
  (void)dirfd;
  (void)dirflags;
  (void)path;
  (void)path_len;
  (void)oflags;
  (void)fs_rights_base;
  (void)fs_rights_inheriting;
  (void)fdflags;
  if (fd_out != NULL) {
    *fd_out = -1;
  }
  return WASI_ENOENT;
}

static void wasi_proc_exit(wasm_exec_env_t exec_env, int32_t rval)
{
  (void)exec_env;
  (void)rval;
  /* Guest stem should finish via Metal status; do not tear down host. */
}

static int32_t wasi_environ_get(wasm_exec_env_t exec_env, uint32_t *environ,
                                uint32_t *environ_buf)
{
  (void)exec_env;
  (void)environ;
  (void)environ_buf;
  return WASI_ESUCCESS;
}

static int32_t wasi_environ_sizes_get(wasm_exec_env_t exec_env, uint32_t *count,
                                      uint32_t *buf_size)
{
  (void)exec_env;
  if (count != NULL) {
    *count = 0;
  }
  if (buf_size != NULL) {
    *buf_size = 0;
  }
  return WASI_ESUCCESS;
}

static int32_t wasi_args_get(wasm_exec_env_t exec_env, uint32_t *argv,
                             uint32_t *argv_buf)
{
  (void)exec_env;
  (void)argv;
  (void)argv_buf;
  return WASI_ESUCCESS;
}

static int32_t wasi_args_sizes_get(wasm_exec_env_t exec_env, uint32_t *argc,
                                   uint32_t *argv_buf_size)
{
  (void)exec_env;
  if (argc != NULL) {
    *argc = 0;
  }
  if (argv_buf_size != NULL) {
    *argv_buf_size = 0;
  }
  return WASI_ESUCCESS;
}

static int32_t wasi_clock_time_get(wasm_exec_env_t exec_env, int32_t clock_id,
                                   int64_t precision, uint64_t *time)
{
  (void)exec_env;
  (void)clock_id;
  (void)precision;
  if (time != NULL) {
    *time = 0;
  }
  return WASI_ESUCCESS;
}

static int32_t wasi_random_get(wasm_exec_env_t exec_env, void *buf,
                               uint32_t buf_len)
{
  uint8_t *p;
  uint32_t i;

  (void)exec_env;
  if (buf == NULL) {
    return WASI_EINVAL;
  }
  p = (uint8_t *)buf;
  for (i = 0; i < buf_len; i++) {
    p[i] = (uint8_t)(i * 17u + 0x5au);
  }
  return WASI_ESUCCESS;
}

static NativeSymbol g_wasi_syms[] = {
    {"args_get", (void *)wasi_args_get, "(**)i", NULL},
    {"args_sizes_get", (void *)wasi_args_sizes_get, "(**)i", NULL},
    {"clock_time_get", (void *)wasi_clock_time_get, "(iI*)i", NULL},
    {"environ_get", (void *)wasi_environ_get, "(**)i", NULL},
    {"environ_sizes_get", (void *)wasi_environ_sizes_get, "(**)i", NULL},
    {"fd_close", (void *)wasi_fd_close, "(i)i", NULL},
    {"fd_fdstat_get", (void *)wasi_fd_fdstat_get, "(i*)i", NULL},
    {"fd_fdstat_set_flags", (void *)wasi_fd_fdstat_set_flags, "(ii)i", NULL},
    {"fd_prestat_get", (void *)wasi_fd_prestat_get, "(i*)i", NULL},
    {"fd_prestat_dir_name", (void *)wasi_fd_prestat_dir_name, "(i*~)i", NULL},
    {"fd_read", (void *)wasi_fd_read, "(i*i*)i", NULL},
    {"fd_seek", (void *)wasi_fd_seek, "(iIi*)i", NULL},
    {"fd_write", (void *)wasi_fd_write, "(i*i*)i", NULL},
    {"path_create_directory", (void *)wasi_path_create_directory, "(i*~)i", NULL},
    {"path_open", (void *)wasi_path_open, "(ii*~iIIi*)i", NULL},
    {"proc_exit", (void *)wasi_proc_exit, "(i)", NULL},
    {"random_get", (void *)wasi_random_get, "(*~)i", NULL},
};

int32_t pm_metal_wasm_port_register_wasi_stubs(void)
{
  if (!wasm_runtime_register_natives(
          "wasi_snapshot_preview1", g_wasi_syms,
          (uint32_t)(sizeof(g_wasi_syms) / sizeof(g_wasi_syms[0])))) {
    return -1;
  }
  return 0;
}

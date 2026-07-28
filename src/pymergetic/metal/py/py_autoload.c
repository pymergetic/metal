/**
 * @file Run /mods/<name>/autoload.py once on the shared µPy context
 * (autoexec for guest Python — externals.register, etc.).
 *
 * Discovery: listdir /mods + a few fixed roots; path-exec each autoload.py
 * (not import) so names never collide across mods.
 */
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/slot/slot_table.h>
#include <runtime/time/cpu.h>

#include "py/compile.h"
#include "py/lexer.h"
#include "py/runtime.h"
#include "py/stackctrl.h"

#include "py_internal.h"

#define PY_AUTOLOAD_NAME       "autoload.py"
#define PY_AUTOLOAD_MAX_BYTES  (64u * 1024u)
#define PY_AUTOLOAD_LOCK_TRIES 100000u

static volatile uint32_t g_autoload_done;

int pm_metal_py_autoload_done(void)
{
  return (g_autoload_done != 0u) ? 1 : 0;
}

static int AutoloadRead(const char *path, char **out, size_t *out_len)
{
  uint32_t sz;
  uint32_t n;
  char    *buf;

  sz = pm_metal_fs_size(path);
  if (sz == 0u || sz > PY_AUTOLOAD_MAX_BYTES) {
    return -1;
  }
  buf = (char *)pm_metal_mem_alloc(sz + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (buf == NULL) {
    return -1;
  }
  n = pm_metal_fs_read(path, buf, sz);
  if (n == 0u) {
    pm_metal_mem_free(buf);
    return -1;
  }
  buf[n]   = '\0';
  *out     = buf;
  *out_len = n;
  return 0;
}

/* Caller holds mPyRunLock (shared context). Unlocks before return. */
static void AutoloadExecHoldingLock(const char *path, const char *src, size_t len)
{
  nlr_buf_t nlr;

  pm_metal_py_ctx_enter(NULL);
  mp_stack_set_top(&nlr);
  if (nlr_push(&nlr) == 0) {
    mp_lexer_t     *lex         = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, len, 0);
    qstr            source_name = lex->source_name;
    mp_parse_tree_t parse_tree  = mp_parse(lex, MP_PARSE_FILE_INPUT);
    mp_obj_t        module_fun  = mp_compile(&parse_tree, source_name, true);

    (void)path;
    mp_call_function_0(module_fun);
    nlr_pop();
  } else {
    pm_metal_logf("py: autoload fail %s", path != NULL ? path : "?");
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
  }
  pm_metal_py_stdout_flush();
  pm_metal_py_ctx_leave();
  pm_metal_py_run_unlock();
}

static void AutoloadOnePath(const char *path)
{
  char   *src;
  size_t  len;
  uint32_t tries;

  if (AutoloadRead(path, &src, &len) != 0) {
    return;
  }

  tries = 0u;
  while (pm_metal_py_run_try_lock() != 0) {
    pm_metal_cpu_pause();
    if (++tries > PY_AUTOLOAD_LOCK_TRIES) {
      pm_metal_logf("py: autoload busy %s", path);
      pm_metal_mem_free(src);
      return;
    }
  }

  pm_metal_logf("py: autoload %s", path);
  AutoloadExecHoldingLock(path, src, len);
  pm_metal_mem_free(src);
}

static void AutoloadTryDir(const char *dir)
{
  char path[192];
  int  n;

  if (dir == NULL || dir[0] == '\0') {
    return;
  }
  n = snprintf(path, sizeof(path), "%s/%s", dir, PY_AUTOLOAD_NAME);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return;
  }
  if (pm_metal_fs_size(path) == 0u) {
    return;
  }
  AutoloadOnePath(path);
}

void pm_metal_py_autoload_run_once(void)
{
  pm_metal_fs_h dh;
  char          name[64];
  char          child[192];
  static const char *const k_fixed[] = {
    "/mods/httpd",
    "/mods/py",
    "/mods/api",
  };
  uint32_t i;

  if (g_autoload_done != 0u) {
    return;
  }
  if (pm_metal_py_init() != 0) {
    return;
  }
  /* Claim once — concurrent ZIP job + REPL banner must not double-run. */
  if (pm_metal_slot_port_cas32(&g_autoload_done, 0u, 1u) != 0) {
    return;
  }

  for (i = 0u; i < sizeof(k_fixed) / sizeof(k_fixed[0]); i++) {
    AutoloadTryDir(k_fixed[i]);
  }

  dh = pm_metal_fs_open("/mods", PM_METAL_FS_O_DIRECTORY);
  if (dh == PM_METAL_FS_INVALID) {
    return;
  }
  while (pm_metal_fs_readdir(dh, name, sizeof(name)) > 0u) {
    int n;

    if (name[0] == '\0' || name[0] == '.') {
      continue;
    }
    /* Skip roots already handled above (readdir order is free). */
    if (strcmp(name, "httpd") == 0 || strcmp(name, "py") == 0 || strcmp(name, "api") == 0) {
      continue;
    }
    n = snprintf(child, sizeof(child), "/mods/%s", name);
    if (n <= 0 || (size_t)n >= sizeof(child)) {
      continue;
    }
    AutoloadTryDir(child);
  }
  pm_metal_fs_close(dh);
}

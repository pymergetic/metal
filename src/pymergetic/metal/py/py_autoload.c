/**
 * @file Run /mods/<name>/autoload.py once on the shared µPy context
 * (autoexec for guest Python — externals.register, etc.).
 *
 * Discovery: listdir /mods + fixed roots; path-exec each autoload.py
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
static int32_t           g_autoload_n; /* scripts run on first pass; -1 = init fail */

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

/* Caller holds mPyRunLock (shared context). Unlocks before return. 0 ok. */
static int32_t AutoloadExecHoldingLock(const char *path, const char *src, size_t len)
{
  nlr_buf_t nlr;
  int32_t   rc;

  pm_metal_py_ctx_enter(NULL);
  mp_stack_set_top(&nlr);
  rc = 0;
  if (nlr_push(&nlr) == 0) {
    mp_lexer_t     *lex         = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, len, 0);
    qstr            source_name = lex->source_name;
    mp_parse_tree_t parse_tree  = mp_parse(lex, MP_PARSE_FILE_INPUT);
    /* is_repl=false — else expression stmts print their value (True True). */
    mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);

    (void)path;
    mp_call_function_0(module_fun);
    nlr_pop();
  } else {
    pm_metal_logf("py: autoload fail %s", path != NULL ? path : "?");
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    rc = -1;
  }
  pm_metal_py_stdout_flush();
  pm_metal_py_ctx_leave();
  pm_metal_py_run_unlock();
  return rc;
}

static int32_t AutoloadOnePath(const char *path)
{
  char    *src;
  size_t   len;
  uint32_t tries;
  int32_t  rc;

  if (AutoloadRead(path, &src, &len) != 0) {
    return -1;
  }

  tries = 0u;
  while (pm_metal_py_run_try_lock() != 0) {
    pm_metal_cpu_pause();
    if (++tries > PY_AUTOLOAD_LOCK_TRIES) {
      pm_metal_logf("py: autoload busy %s", path);
      pm_metal_mem_free(src);
      return -1;
    }
  }

  rc = AutoloadExecHoldingLock(path, src, len);
  pm_metal_mem_free(src);
  return rc;
}

static int32_t AutoloadTryDir(const char *dir)
{
  char path[192];
  int  n;

  if (dir == NULL || dir[0] == '\0') {
    return -1;
  }
  n = snprintf(path, sizeof(path), "%s/%s", dir, PY_AUTOLOAD_NAME);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }
  if (pm_metal_fs_size(path) == 0u) {
    return -1;
  }
  return AutoloadOnePath(path);
}

int pm_metal_py_autoload_run_once(void)
{
  pm_metal_fs_h dh;
  char          name[64];
  char          child[192];
  static const char *const k_fixed[] = {
    "/mods/httpd",
    "/mods/py",
  };
  uint32_t i;
  int32_t  n;

  if (g_autoload_done != 0u) {
    return (int)g_autoload_n;
  }
  if (pm_metal_py_init() != 0) {
    g_autoload_n = -1;
    (void)pm_metal_slot_port_cas32(&g_autoload_done, 0u, 1u);
    return -1;
  }
  /* Claim once — concurrent ZIP job + REPL banner must not double-run. */
  if (pm_metal_slot_port_cas32(&g_autoload_done, 0u, 1u) != 0) {
    return (int)g_autoload_n;
  }

  n = 0;
  for (i = 0u; i < sizeof(k_fixed) / sizeof(k_fixed[0]); i++) {
    if (AutoloadTryDir(k_fixed[i]) == 0) {
      n++;
    }
  }

  dh = pm_metal_fs_open("/mods", PM_METAL_FS_O_DIRECTORY);
  if (dh != PM_METAL_FS_INVALID) {
    while (pm_metal_fs_readdir(dh, name, sizeof(name)) > 0u) {
      int dn;

      if (name[0] == '\0' || name[0] == '.') {
        continue;
      }
      if (strcmp(name, "httpd") == 0 || strcmp(name, "py") == 0) {
        continue;
      }
      /* Skip zip files and other non-dirs named like api.zip. */
      if (strstr(name, ".") != NULL) {
        continue;
      }
      dn = snprintf(child, sizeof(child), "/mods/%s", name);
      if (dn <= 0 || (size_t)dn >= sizeof(child)) {
        continue;
      }
      if (AutoloadTryDir(child) == 0) {
        n++;
      }
    }
    pm_metal_fs_close(dh);
  }

  g_autoload_n = n;
  return (int)n;
}

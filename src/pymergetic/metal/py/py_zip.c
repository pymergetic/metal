/**
 * @file stdlib.zip on sys.path — trust check + single-flight HTTP fetch on
 * ESP miss (Phase 3, docs/MICROPYTHON.md). Fetch/verify runs as a coroutine
 * step (pm_metal_py_zip_step, driven from py.c's py_job_step) so a network
 * round trip never blocks a runner; pm_metal_py_zip_ensure() stays as a
 * local-only sync fallback for callers outside any async step.
 */
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/trust/trust.h>
#include <pymergetic/metal/dev/net/http.h>
#include <pymergetic/metal/dev/net/net_cfg.h>
#include <pymergetic/metal/util/ip.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <pymergetic/metal/runtime/async/async.h>

#include "py/runtime.h"

#include "py_internal.h"

/* PM_METAL_PY_STDLIB_ZIP / _SIG: py_internal.h (shared with py_zip_embed.c) */
#define PM_METAL_PY_STDLIB_ZIP_URL_PATH "py/stdlib.zip"
#define PY_ZIP_FETCH_CAP                (512u * 1024u)
#define PY_ZIP_HOST_FALLBACK            0xC0A80A01u /* 192.168.10.1 — mirrors net_life.c's lab default */

typedef enum {
  ZIP_UNVERIFIED = 0,
  ZIP_READY,  /* usable: trusted zip present, or confirmed-absent (optional) */
  ZIP_FAILED, /* present but rejected by trust policy — hard stop, no import */
} py_zip_state_t;

static py_zip_state_t          g_zip_state;
static pm_metal_async_handle_t g_zip_fetch_h;      /* in-flight fetch, 0 = none */
static pm_metal_async_handle_t g_zip_fetch_leader; /* only this self_h may await g_zip_fetch_h */
static uint8_t                *g_zip_fetch_buf;    /* owned while g_zip_fetch_h != 0 */
static int                     g_zip_fetch_tried;  /* one attempt total, ever */

void pm_metal_py_zip_init_sys_path(void)
{
  mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/mods/py")));
  mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str(PM_METAL_PY_STDLIB_ZIP)));
  /* Microdot (ASGI) — signed zip beside stdlib; optional if absent. */
  mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/mods/py/microdot.zip")));
}

/* 0 present+trusted, 1 absent (optional), -1 present but rejected. */
static int32_t ZipVerifyLocal(void)
{
  uint32_t sz;
  uint32_t sig_sz;
  uint8_t *data;
  uint8_t *sig;
  uint32_t n;
  int32_t  rc;

  sz = pm_metal_fs_size(PM_METAL_PY_STDLIB_ZIP);
  if (sz == 0) {
    return 1;
  }

  data = (uint8_t *)pm_metal_mem_alloc(sz, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (data == NULL) {
    return -1;
  }
  n = pm_metal_fs_read(PM_METAL_PY_STDLIB_ZIP, data, sz);
  if (n == 0) {
    pm_metal_mem_free(data);
    return -1;
  }

  sig    = NULL;
  sig_sz = pm_metal_fs_size(PM_METAL_PY_STDLIB_ZIP_SIG);
  if (sig_sz > 0) {
    sig = (uint8_t *)pm_metal_mem_alloc(sig_sz, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (sig != NULL && pm_metal_fs_read(PM_METAL_PY_STDLIB_ZIP_SIG, sig, sig_sz) != sig_sz) {
      pm_metal_mem_free(sig);
      sig    = NULL;
      sig_sz = 0;
    }
  }

  rc = (pm_metal_trust_accept_mods(data, n, sig, sig_sz) == 0) ? 0 : -1;
  pm_metal_mem_free(data);
  if (sig != NULL) {
    pm_metal_mem_free(sig);
  }
  return rc;
}

/* Best-effort HTTP host: default-gateway (dev box usually doubles as the
 * seed server), else the same lab fallback net_life.c uses for pkg seeding. */
static int32_t ZipResolveHost(char *out, size_t cap)
{
  pm_metal_net_ifcfg_t cfg;
  uint32_t             gw;

  if (out == NULL || cap == 0) {
    return -1;
  }
  out[0] = '\0';
  if (pm_metal_net_if_get(&cfg) == 0 && pm_metal_util_ip4_parse(cfg.gw, &gw) == 0 &&
      !pm_metal_util_ip4_is_unspecified(gw) && pm_metal_util_ip4_format(gw, out, cap) > 0) {
    return 0;
  }
  return (pm_metal_util_ip4_format(PY_ZIP_HOST_FALLBACK, out, cap) > 0) ? 0 : -1;
}

static void ZipFreeFetchBuf(void)
{
  if (g_zip_fetch_buf != NULL) {
    pm_metal_mem_free(g_zip_fetch_buf);
    g_zip_fetch_buf = NULL;
  }
}

static pm_metal_async_handle_t ZipStartFetch(void)
{
  char                    host[PM_METAL_NET_TFTP_HOST_MAX];
  char                    url[192];
  pm_metal_async_handle_t h;

  if (ZipResolveHost(host, sizeof(host)) != 0) {
    return 0;
  }
  if (snprintf(url, sizeof(url), "http://%s:8080/%s", host, PM_METAL_PY_STDLIB_ZIP_URL_PATH) <= 0) {
    return 0;
  }

  g_zip_fetch_buf =
    (uint8_t *)pm_metal_mem_alloc(PY_ZIP_FETCH_CAP, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (g_zip_fetch_buf == NULL) {
    return 0;
  }

  h = pm_metal_net_http_get(url, g_zip_fetch_buf, PY_ZIP_FETCH_CAP);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    ZipFreeFetchBuf();
    return 0;
  }
  return h;
}

/* Leader-only: called right after its await(g_zip_fetch_h) resumes. */
static void ZipFinishFetch(void)
{
  uint32_t status;
  uint32_t n;

  status        = pm_metal_net_http_status(g_zip_fetch_h);
  n             = pm_metal_net_http_body_len(g_zip_fetch_h);
  g_zip_fetch_h = 0;

  if (status == 200u && n > 0u && g_zip_fetch_buf != NULL) {
    if (pm_metal_fs_write(PM_METAL_PY_STDLIB_ZIP, g_zip_fetch_buf, n) != n) {
      pm_metal_shell_out("py: stdlib.zip write failed");
    }
  } else {
    char msg[64];
    (void)snprintf(msg, sizeof(msg), "py: stdlib.zip fetch miss (http %u)", status);
    pm_metal_shell_out(msg);
  }
  ZipFreeFetchBuf();
}

/*
 * Coroutine-friendly step, mirroring py_job_step's own case shape:
 *   0  = ready to proceed (zip usable, or definitively/optionally absent)
 *  -1  = hard fail (trust policy rejected a present zip) — caller must abort
 *   1  = not decided yet; *out_pending is a handle the caller must await
 *        (pm_metal_async_await(self_h, *out_pending)) before calling again
 *
 * Single-flight: only the coroutine that actually started the fetch (the
 * "leader") may await the real fetch handle — pm_metal_await's waiter slot
 * is single-occupant (runtime/coro/coro.c), so a second direct awaiter
 * would silently steal the wake-up from the first. Every other concurrent
 * caller instead parks on its own private yield and re-polls next tick,
 * the same busy-retry shape py_run_lock contention already uses.
 */
int32_t pm_metal_py_zip_step(pm_metal_async_handle_t self_h, pm_metal_async_handle_t *out_pending)
{
  int32_t vr;

  if (out_pending != NULL) {
    *out_pending = 0;
  }

  if (g_zip_state == ZIP_READY) {
    return 0;
  }
  if (g_zip_state == ZIP_FAILED) {
    return -1;
  }

  if (g_zip_fetch_h != 0) {
    if (self_h != g_zip_fetch_leader) {
      if (out_pending != NULL) {
        *out_pending = pm_metal_async_yield();
      }
      return 1;
    }
    ZipFinishFetch();
    /* fall through: re-verify locally now that a fetch attempt landed */
  }

  vr = ZipVerifyLocal();
  if (vr == 0) {
    g_zip_state = ZIP_READY;
    return 0;
  }
  if (vr < 0) {
    g_zip_state = ZIP_FAILED;
    pm_metal_shell_out("py: stdlib.zip trust FAIL");
    return -1;
  }

  /* vr == 1: not present locally. One fetch attempt total, ever. */
  if (!g_zip_fetch_tried) {
    pm_metal_async_handle_t h;

    g_zip_fetch_tried = 1;
    h                 = ZipStartFetch();
    if (h != 0) {
      g_zip_fetch_h      = h;
      g_zip_fetch_leader = self_h;
      if (out_pending != NULL) {
        *out_pending = h;
      }
      return 1;
    }
  }

  g_zip_state = ZIP_READY;
  pm_metal_shell_out("py: stdlib.zip miss (optional)");
  return 0;
}

int pm_metal_py_zip_ensure(void)
{
  /*
   * Legacy sync entry point for callers outside any async step (can't
   * await a network round trip) — local-only, never fetches. Real callers
   * (py_spawn via py_job_step's PY_STEP_ZIP) use pm_metal_py_zip_step
   * instead, which also covers the HTTP-fetch-on-miss path.
   */
  int32_t vr;

  if (g_zip_state == ZIP_READY) {
    return 0;
  }
  if (g_zip_state == ZIP_FAILED) {
    return -1;
  }

  vr = ZipVerifyLocal();
  if (vr == 0) {
    g_zip_state = ZIP_READY;
    return 0;
  }
  if (vr < 0) {
    g_zip_state = ZIP_FAILED;
    pm_metal_shell_out("py: stdlib.zip trust FAIL");
    return -1;
  }

  pm_metal_shell_out("py: stdlib.zip miss (optional for hello)");
  return 0;
}

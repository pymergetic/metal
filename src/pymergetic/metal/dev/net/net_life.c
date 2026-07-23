/** @file
  Net life — lease watch, NTP; HTTP seed transport on run/tab demand.
  Package catalog is guest/pkg — this file only fetches URLs.
**/
#include <pymergetic/metal/dev/net/net_life.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/net/net_cfg.h>
#include <pymergetic/metal/dev/net/http.h>
#include <pymergetic/metal/dev/net/ntp.h>
#include <pymergetic/metal/guest/pkg/pkg.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/util/ip.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/coro/coro.h>
#include <runtime/task/task.h>
#include <runtime/time/time.h>
#include <runtime/mem/mem.h>
#include <runtime/run/run.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#define LIFE_DOWN_US       500000ull
#define LIFE_UP_US         5000000ull
#define LIFE_NTP_PERIOD_US (30ull * 60ull * 1000000ull)
#define LIFE_PKG_ENSURE_US (60ull * 1000000ull) /* on-demand fetch budget */
#define LIFE_PKG_HOST_FALLBACK  0xC0A80A01u /* 192.168.10.1 */

typedef enum {
  LIFE_DOWN = 0,
  LIFE_SEED,
  LIFE_SEED_WAIT,
  LIFE_NTP,
  LIFE_NTP_WAIT,
  LIFE_UP
} life_step_t;

STATIC CONST CHAR8 *CONST  mNtpFallback[] = {
  "pool.ntp.org",
  "time.google.com",
};

typedef struct {
  pm_metal_coro_t          coro;
  life_step_t              step;
  pm_metal_async_handle_t  http_aw;
  pm_metal_async_handle_t  ntp_aw;
  UINT32                   pkg_i;
  UINT32                   ntp_i;
  UINT32                   pkg_got;
  UINT8                   *pkg_buf;
  UINT32                   pkg_cap;
  UINT64                   last_ntp_us;
  UINT64                   lease_up_us;
  UINT8                    pkg_done; /* 1 = last on-demand attempt finished */
  UINT8                    pkg_want; /* 1 = run/tab requested a fetch */
  UINT8                    pkg_busy; /* 1 = currently in LIFE_SEED* */
  UINT8                    ntp_ok;
  UINT8                    logged_up;
  CHAR8                    pkg_name[32];
  CHAR8                    pkg_host[PM_METAL_NET_TFTP_HOST_MAX];
  CHAR8                    pkg_url[192];
  CHAR8                    ntp_host[64];
} life_t;

STATIC life_t  *mLife;

STATIC
INT32
LifeHasLease (
  VOID
  )
{
  UINT32               n;
  UINT32               i;
  pm_metal_net_ifcfg_t cfg;
  UINT32               ip;

  n = pm_metal_net_if_count ();
  for (i = 0; i < n; i++) {
    if (pm_metal_net_if_get_index (i, &cfg) != 0) {
      continue;
    }

    if (AsciiStrCmp (cfg.name, "lo") == 0) {
      continue;
    }

    if (pm_metal_util_ip4_parse (cfg.ip, &ip) == 0
        && !pm_metal_util_ip4_is_unspecified (ip))
    {
      return 1;
    }
  }

  return 0;
}

STATIC
INT32
LifeBuildHttpUrl (
  CHAR8        *out,
  UINTN         cap,
  CONST CHAR8  *host,
  CONST CHAR8  *path
  )
{
  STATIC CONST CHAR8  Pre[] = "http://";
  STATIC CONST CHAR8  Mid[] = ":8080/";
  UINTN               i;
  CONST CHAR8        *p;

  if (out == NULL || cap < 2u || host == NULL || path == NULL) {
    return -1;
  }

  i = 0;
  for (p = Pre; *p != '\0'; p++) {
    if (i + 1u >= cap) {
      return -1;
    }

    out[i++] = *p;
  }

  for (p = host; *p != '\0'; p++) {
    if (i + 1u >= cap) {
      return -1;
    }

    out[i++] = *p;
  }

  for (p = Mid; *p != '\0'; p++) {
    if (i + 1u >= cap) {
      return -1;
    }

    out[i++] = *p;
  }

  for (p = path; *p != '\0'; p++) {
    if (i + 1u >= cap) {
      return -1;
    }

    out[i++] = *p;
  }

  out[i] = '\0';
  return (INT32)i;
}

STATIC
VOID
LifeResolvePkgHost (
  life_t  *s
  )
{
  CHAR8                boot[PM_METAL_NET_BOOT_FILE_MAX];
  UINT32               pkg_ip;
  UINT32               gw;
  pm_metal_net_ifcfg_t cfg;

  s->pkg_host[0] = '\0';
  /* 1) DHCP next-server / siaddr (PXE HTTP mirror). */
  if (pm_metal_net_if_boot_get (
        NULL,
        s->pkg_host,
        sizeof (s->pkg_host),
        boot,
        sizeof (boot)
        ) == 0
      && s->pkg_host[0] != '\0'
      && pm_metal_util_ip4_parse (s->pkg_host, &pkg_ip) == 0
      && !pm_metal_util_ip4_is_unspecified (pkg_ip))
  {
    return;
  }

  /* 2) Default gateway — common when next-server unset but :8080 is the router/dev box. */
  if (pm_metal_net_if_get (&cfg) == 0
      && pm_metal_util_ip4_parse (cfg.gw, &gw) == 0
      && !pm_metal_util_ip4_is_unspecified (gw)
      && pm_metal_util_ip4_format (gw, s->pkg_host, sizeof (s->pkg_host)) > 0)
  {
    return;
  }

  /* 3) Lab default. */
  (VOID)pm_metal_util_ip4_format (
          LIFE_PKG_HOST_FALLBACK,
          s->pkg_host,
          sizeof (s->pkg_host)
          );
}

STATIC
VOID
LifeFreePkgBuf (
  life_t  *s
  )
{
  if (s->pkg_buf != NULL) {
    pm_metal_mem_free (s->pkg_buf);
    s->pkg_buf = NULL;
  }

  s->pkg_cap = 0;
}

STATIC
VOID
LifeLog (
  CONST CHAR8  *line
  )
{
  /* Mid-prompt UART: break the line, log, ask shell to re-prompt. */
  pm_metal_console_com1_write ("\r\n", 2);
  pm_metal_log (line);
  pm_metal_shell_prompt_dirty ();
}

STATIC
pm_metal_status_t
LifeCoro (
  pm_metal_coro_t  *self
  )
{
  life_t  *s;

  s = (life_t *)self;
  for (;;) {
    switch (s->step) {
      case LIFE_DOWN:
        pm_metal_net_poll ();
        if (!LifeHasLease ()) {
          s->logged_up  = 0;
          s->lease_up_us = 0;
          return pm_metal_await (self, pm_metal_sleep_us (LIFE_DOWN_US));
        }

        if (s->logged_up == 0u) {
          /* Quiet success — tray/tree show lease; avoid clobbering the prompt. */
          s->logged_up   = 1;
          s->lease_up_us = pm_metal_time_mono_us ();
        }

        s->step = LIFE_UP;
        continue;

      case LIFE_SEED:
        {
          CONST pm_metal_pkg_file_t  *files;
          UINT32                      nfiles;

          s->pkg_busy = 1;
          files       = pm_metal_pkg_files (s->pkg_name, &nfiles);
          /* Catalog says ready — never open HTTP. */
          if (pm_metal_pkg_ready (s->pkg_name)) {
            s->pkg_done = 1;
            s->pkg_want = 0;
            s->pkg_busy = 0;
            LifeFreePkgBuf (s);
            s->step = LIFE_UP;
            continue;
          }

          while (files != NULL && s->pkg_i < nfiles) {
            CONST pm_metal_pkg_file_t  *pkg;
            UINT32                      sz;

            pkg = &files[s->pkg_i];
            if (pm_metal_esp_file_size (pkg->esp_path, &sz) == 0
                || pm_metal_pkg_file_optional (s->pkg_name, pkg))
            {
              s->pkg_i++;
              continue;
            }

            LifeFreePkgBuf (s);
            s->pkg_cap = pkg->cap;
            s->pkg_buf = (UINT8 *)pm_metal_mem_alloc (
                           s->pkg_cap,
                           PM_METAL_MEM_HEAP,
                           PM_METAL_MEM_ID_NONE
                           );
            if (s->pkg_buf == NULL
                || LifeBuildHttpUrl (
                     s->pkg_url,
                     sizeof (s->pkg_url),
                     s->pkg_host,
                     pkg->url_path
                     ) < 0)
            {
              LifeFreePkgBuf (s);
              s->pkg_i++;
              continue;
            }

            s->http_aw = pm_metal_net_http_get (
                           s->pkg_url,
                           s->pkg_buf,
                           s->pkg_cap
                           );
            if (s->http_aw == PM_METAL_ASYNC_HANDLE_INVALID) {
              LifeFreePkgBuf (s);
              s->pkg_i++;
              continue;
            }

            s->step = LIFE_SEED_WAIT;
            return (pm_metal_status_t)pm_metal_async_await_coro (self, s->http_aw);
          }

          /* One on-demand pass finished (ok or miss) — never loop without ask. */
          s->pkg_done = 1;
          s->pkg_want = 0;
          s->pkg_busy = 0;
          if (s->pkg_got > 0u && pm_metal_pkg_ready (s->pkg_name)) {
            LifeLog ("metal-net: pkg seeded");
          } else if (!pm_metal_pkg_ready (s->pkg_name)) {
            LifeLog ("metal-net: pkg fetch miss");
          }

          LifeFreePkgBuf (s);
          s->step = LIFE_UP;
          continue;
        }

      case LIFE_SEED_WAIT:
        {
          CONST pm_metal_pkg_file_t  *files;
          CONST pm_metal_pkg_file_t  *pkg;
          UINT32                      nfiles;
          UINT32                      st;
          UINT32                      n;
          CHAR8                       msg[160];

          files = pm_metal_pkg_files (s->pkg_name, &nfiles);
          pkg   = (files != NULL && s->pkg_i < nfiles) ? &files[s->pkg_i] : NULL;
          st    = pm_metal_net_http_status (s->http_aw);
          n     = pm_metal_net_http_body_len (s->http_aw);
          s->http_aw = PM_METAL_ASYNC_HANDLE_INVALID;

          if (pkg != NULL && st == 200u && n > 0u && s->pkg_buf != NULL) {
            if (pm_metal_esp_cache_put (pkg->esp_path, s->pkg_buf, n) == 0) {
              s->pkg_got++;
            } else {
              AsciiSPrint (
                msg,
                sizeof (msg),
                "metal-net: pkg cache fail %a",
                pkg->esp_path
                );
              LifeLog (msg);
            }
          } else {
            AsciiSPrint (
              msg,
              sizeof (msg),
              "metal-net: pkg http %u %a",
              st,
              s->pkg_url
              );
            LifeLog (msg);
          }

          LifeFreePkgBuf (s);
          s->pkg_i++;
          s->step = LIFE_SEED;
          continue;
        }

      case LIFE_NTP:
        if (!LifeHasLease ()) {
          s->step = LIFE_DOWN;
          continue;
        }

        s->ntp_host[0] = '\0';
        if (s->ntp_i == 0u) {
          pm_metal_net_ifcfg_t  cfg;

          if (pm_metal_net_if_get (&cfg) == 0 && cfg.ntp[0] != '\0') {
            AsciiStrCpyS (s->ntp_host, sizeof (s->ntp_host), cfg.ntp);
          }
        } else {
          UINT32  fi;

          fi = s->ntp_i - 1u;
          if (fi < (UINT32)(sizeof (mNtpFallback) / sizeof (mNtpFallback[0]))) {
            AsciiStrCpyS (
              s->ntp_host,
              sizeof (s->ntp_host),
              mNtpFallback[fi]
              );
          }
        }

        if (s->ntp_host[0] == '\0') {
          s->ntp_i++;
          if (s->ntp_i
              > (UINT32)(sizeof (mNtpFallback) / sizeof (mNtpFallback[0])))
          {
            LifeLog ("metal-net: ntp fail");
            s->step = LIFE_UP;
            continue;
          }

          continue;
        }

        s->ntp_aw = pm_metal_net_ntp_sync (s->ntp_host);
        if (s->ntp_aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          s->ntp_i++;
          continue;
        }

        s->step = LIFE_NTP_WAIT;
        return (pm_metal_status_t)pm_metal_async_await_coro (self, s->ntp_aw);

      case LIFE_NTP_WAIT:
        if (pm_metal_net_ntp_status (s->ntp_aw) == 0u) {
          s->last_ntp_us = pm_metal_time_mono_us ();
          s->ntp_ok      = 1;
          /* Quiet success — clock tray turns green; no console spam. */
          s->ntp_aw = PM_METAL_ASYNC_HANDLE_INVALID;
          s->step   = LIFE_UP;
          continue;
        }

        s->ntp_aw = PM_METAL_ASYNC_HANDLE_INVALID;
        s->ntp_i++;
        if (s->ntp_i
            > (UINT32)(sizeof (mNtpFallback) / sizeof (mNtpFallback[0])))
        {
          s->last_ntp_us = pm_metal_time_mono_us ();
          s->ntp_ok      = 0;
          LifeLog ("metal-net: ntp fail");
          s->step = LIFE_UP;
          continue;
        }

        s->step = LIFE_NTP;
        continue;

      case LIFE_UP:
        {
          UINT64  now;
          UINT64  period;

          pm_metal_net_poll ();
          if (!LifeHasLease ()) {
            s->step = LIFE_DOWN;
            continue;
          }

          now = pm_metal_time_mono_us ();

          /* HTTP seed only when run/tab called seed_ensure (pkg_want). */
          if (s->pkg_want != 0u && s->pkg_name[0] != '\0'
              && !pm_metal_pkg_ready (s->pkg_name) && s->pkg_busy == 0u)
          {
            LifeResolvePkgHost (s);
            s->pkg_i    = 0;
            s->pkg_got  = 0;
            s->pkg_done = 0;
            s->step     = LIFE_SEED;
            continue;
          }

          period = (s->ntp_ok != 0u) ? LIFE_NTP_PERIOD_US : LIFE_UP_US;
          if (s->last_ntp_us == 0ull || (now - s->last_ntp_us) >= period) {
            s->ntp_i = 0;
            s->step  = LIFE_NTP;
            continue;
          }

          return pm_metal_await (self, pm_metal_sleep_us (LIFE_UP_US));
        }

      default:
        s->step = LIFE_DOWN;
        continue;
    }
  }
}

int
pm_metal_net_life_seed_ensure (
  CONST CHAR8  *name
  )
{
  UINT64  deadline;

  if (name == NULL || name[0] == '\0') {
    return -1;
  }

  if (pm_metal_pkg_lookup (name) == NULL) {
    return 0;
  }

  if (pm_metal_pkg_ready (name)) {
    return 0;
  }

  if (mLife == NULL) {
    return -1;
  }

  if (!LifeHasLease ()) {
    LifeLog ("metal-net: pkg ensure needs lease");
    return -1;
  }

  AsciiStrCpyS (mLife->pkg_name, sizeof (mLife->pkg_name), name);
  LifeLog ("metal-net: pkg ensure (run/tab)");
  mLife->pkg_want = 1;
  mLife->pkg_done = 0;
  deadline        = pm_metal_time_mono_us () + LIFE_PKG_ENSURE_US;

  while (!pm_metal_pkg_ready (name)) {
    if (mLife->pkg_done != 0u && mLife->pkg_busy == 0u) {
      break;
    }

    if (pm_metal_time_mono_us () >= deadline) {
      LifeLog ("metal-net: pkg ensure timeout");
      break;
    }

    pm_metal_net_poll ();
    pm_metal_run_poll_all ();
  }

  mLife->pkg_want = 0;
  return pm_metal_pkg_ready (name) ? 0 : -1;
}

int
pm_metal_net_life_start (
  VOID
  )
{
  pm_metal_task_t  *task;

  if (mLife != NULL) {
    return 0;
  }

  mLife = (life_t *)pm_metal_coro (LifeCoro, sizeof (*mLife));
  if (mLife == NULL) {
    return -1;
  }

  mLife->step     = LIFE_DOWN;
  mLife->http_aw  = PM_METAL_ASYNC_HANDLE_INVALID;
  mLife->ntp_aw   = PM_METAL_ASYNC_HANDLE_INVALID;
  mLife->pkg_done = 1; /* no fetch until run/tab asks */
  mLife->pkg_want = 0;
  mLife->pkg_busy = 0;

  task = pm_metal_create_task (&mLife->coro);
  if (task == NULL) {
    mLife = NULL;
    return -1;
  }

  return 0;
}

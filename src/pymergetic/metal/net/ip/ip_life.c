/** @file
  Net life — lease watch, NTP; HTTP seed transport on run/tab demand.
  Package catalog is guest/pkg — this file only fetches URLs.
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_life.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/net/asgi/asgi.h>
#include <pymergetic/metal/net/http/http.h>
#include <pymergetic/metal/net/ntp/ntp.h>
#include <pymergetic/metal/net/ssh/ssh.h>
#include <pymergetic/metal/guest/pkg/pkg.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/util/ip.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/time/time.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/run/run.h>

#define LIFE_DOWN_US           500000ull
#define LIFE_UP_US             5000000ull
#define LIFE_NTP_PERIOD_US     (30ull * 60ull * 1000000ull)
#define LIFE_PKG_ENSURE_US     (60ull * 1000000ull) /* on-demand fetch budget */
#define LIFE_PKG_HOST_FALLBACK 0xC0A80A01u          /* 192.168.10.1 */

typedef enum {
  LIFE_DOWN = 0,
  LIFE_SEED,
  LIFE_SEED_WAIT,
  LIFE_NTP,
  LIFE_NTP_WAIT,
  LIFE_UP
} life_step_t;

static const char *const mNtpFallback[] = {
  "pool.ntp.org",
  "time.google.com",
};

typedef struct {
  life_step_t             step;
  pm_metal_async_handle_t http_aw;
  pm_metal_async_handle_t ntp_aw;
  uint32_t                pkg_i;
  uint32_t                ntp_i;
  uint32_t                pkg_got;
  uint8_t                *pkg_buf;
  uint32_t                pkg_cap;
  uint64_t                last_ntp_us;
  uint64_t                lease_up_us;
  uint8_t                 pkg_done; /* 1 = last on-demand attempt finished */
  uint8_t                 pkg_want; /* 1 = run/tab requested a fetch */
  uint8_t                 pkg_busy; /* 1 = currently in LIFE_SEED* */
  uint8_t                 ntp_ok;
  uint8_t                 logged_up;
  char                    pkg_name[32];
  char                    pkg_host[PM_METAL_NET_TFTP_HOST_MAX];
  char                    pkg_url[192];
  char                    ntp_host[64];
} life_t;

static life_t *mLife;

static int32_t LifeHasLease(void)
{
  uint32_t                n;
  uint32_t                i;
  pm_metal_net_ip_ifcfg_t cfg;
  uint32_t                ip;

  n = pm_metal_net_ip_if_count();
  for (i = 0; i < n; i++) {
    if (pm_metal_net_ip_if_get_index(i, &cfg) != 0) {
      continue;
    }

    if (strcmp(cfg.name, "lo") == 0) {
      continue;
    }

    if (pm_metal_util_ip4_parse(cfg.ip, &ip) == 0 && !pm_metal_util_ip4_is_unspecified(ip)) {
      return 1;
    }
  }

  return 0;
}

static int32_t LifeBuildHttpUrl(char *out, uintptr_t cap, const char *host, const char *path)
{
  static const char Pre[] = "http://";
  static const char Mid[] = ":8080/";
  uintptr_t         i;
  const char       *p;

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
  return (int32_t)i;
}

/*
 * Shared with the public pm_metal_net_ip_seed_host() guest/host accessor
 * (net.h) -- keep the resolution in one place so a diagnostic caller and
 * the actual HTTP seed fetch below always agree on "the host".
 */
int32_t pm_metal_net_ip_seed_host(char *out, uint32_t out_cap)
{
  char                    boot[PM_METAL_NET_IP_BOOT_FILE_MAX];
  uint32_t                pkg_ip;
  uint32_t                gw;
  pm_metal_net_ip_ifcfg_t cfg;

  if (out == NULL || out_cap == 0u) {
    return -1;
  }

  out[0] = '\0';
  /* 1) DHCP next-server / siaddr (PXE HTTP mirror). */
  if (pm_metal_net_ip_if_boot_get(NULL, out, out_cap, boot, sizeof(boot)) == 0 && out[0] != '\0' &&
      pm_metal_util_ip4_parse(out, &pkg_ip) == 0 && !pm_metal_util_ip4_is_unspecified(pkg_ip)) {
    return 0;
  }

  /* 2) Default gateway — common when next-server unset but :8080 is the router/dev box. */
  if (pm_metal_net_ip_if_get(&cfg) == 0 && pm_metal_util_ip4_parse(cfg.gw, &gw) == 0 &&
      !pm_metal_util_ip4_is_unspecified(gw) && pm_metal_util_ip4_format(gw, out, out_cap) > 0) {
    return 0;
  }

  /* 3) Lab default. */
  return (pm_metal_util_ip4_format(LIFE_PKG_HOST_FALLBACK, out, out_cap) > 0) ? 0 : -1;
}

static void LifeResolvePkgHost(life_t *s)
{
  (void)pm_metal_net_ip_seed_host(s->pkg_host, sizeof(s->pkg_host));
}

static void LifeFreePkgBuf(life_t *s)
{
  if (s->pkg_buf != NULL) {
    pm_metal_mem_free(s->pkg_buf);
    s->pkg_buf = NULL;
  }

  s->pkg_cap = 0;
}

static void LifeLog(const char *line)
{
  /* Mid-prompt UART: break the line, log, ask shell to re-prompt. */
  pm_metal_console_com1_write("\r\n", 2);
  pm_metal_log(line);
  pm_metal_shell_prompt_dirty();
}

static pm_metal_status_t LifeStep(pm_metal_async_handle_t self_h)
{
  life_t                 *s;
  pm_metal_async_handle_t h;

  h = self_h;
  s = (life_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  for (;;) {
    switch (s->step) {
    case LIFE_DOWN:
      pm_metal_net_ip_poll();
      if (!LifeHasLease()) {
        s->logged_up   = 0;
        s->lease_up_us = 0;
        return pm_metal_async_await(h, pm_metal_async_sleep_us(LIFE_DOWN_US));
      }

      if (s->logged_up == 0u) {
        /* Quiet success — tray/tree show lease; avoid clobbering the prompt. */
        s->logged_up   = 1;
        s->lease_up_us = pm_metal_time_mono_us();
        (void)pm_metal_net_asgi_autoload();
        (void)pm_metal_net_ssh_autoload();
      }

      s->step = LIFE_UP;
      continue;

    case LIFE_SEED: {
      const pm_metal_pkg_file_t *files;
      uint32_t                   nfiles;

      s->pkg_busy = 1;
      files       = pm_metal_pkg_files(s->pkg_name, &nfiles);
      /* Catalog says ready — never open HTTP. */
      if (pm_metal_pkg_ready(s->pkg_name)) {
        s->pkg_done = 1;
        s->pkg_want = 0;
        s->pkg_busy = 0;
        LifeFreePkgBuf(s);
        s->step = LIFE_UP;
        continue;
      }

      while (files != NULL && s->pkg_i < nfiles) {
        const pm_metal_pkg_file_t *pkg;
        uint32_t                   sz;

        pkg = &files[s->pkg_i];
        if (pm_metal_esp_file_size(pkg->esp_path, &sz) == 0 ||
            pm_metal_pkg_file_optional(s->pkg_name, pkg)) {
          s->pkg_i++;
          continue;
        }

        LifeFreePkgBuf(s);
        s->pkg_cap = pkg->cap;
        s->pkg_buf =
          (uint8_t *)pm_metal_mem_alloc(s->pkg_cap, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
        if (s->pkg_buf == NULL ||
            LifeBuildHttpUrl(s->pkg_url, sizeof(s->pkg_url), s->pkg_host, pkg->url_path) < 0) {
          LifeFreePkgBuf(s);
          s->pkg_i++;
          continue;
        }

        s->http_aw = pm_metal_net_http_get(s->pkg_url, s->pkg_buf, s->pkg_cap);
        if (s->http_aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          LifeFreePkgBuf(s);
          s->pkg_i++;
          continue;
        }

        s->step = LIFE_SEED_WAIT;
        return pm_metal_async_await(h, s->http_aw);
      }

      /* One on-demand pass finished (ok or miss) — never loop without ask. */
      s->pkg_done = 1;
      s->pkg_want = 0;
      s->pkg_busy = 0;
      if (s->pkg_got > 0u && pm_metal_pkg_ready(s->pkg_name)) {
        LifeLog("metal-net: pkg seeded");
      } else if (!pm_metal_pkg_ready(s->pkg_name)) {
        LifeLog("metal-net: pkg fetch miss");
      }

      LifeFreePkgBuf(s);
      s->step = LIFE_UP;
      continue;
    }

    case LIFE_SEED_WAIT: {
      const pm_metal_pkg_file_t *files;
      const pm_metal_pkg_file_t *pkg;
      uint32_t                   nfiles;
      uint32_t                   st;
      uint32_t                   n;
      char                       msg[160];

      files      = pm_metal_pkg_files(s->pkg_name, &nfiles);
      pkg        = (files != NULL && s->pkg_i < nfiles) ? &files[s->pkg_i] : NULL;
      st         = pm_metal_net_http_status(s->http_aw);
      n          = pm_metal_net_http_body_len(s->http_aw);
      s->http_aw = PM_METAL_ASYNC_HANDLE_INVALID;

      if (pkg != NULL && st == 200u && n > 0u && s->pkg_buf != NULL) {
        if (pm_metal_esp_cache_put(pkg->esp_path, s->pkg_buf, n) == 0) {
          s->pkg_got++;
        } else {
          snprintf(msg, sizeof(msg), "metal-net: pkg cache fail %s", pkg->esp_path);
          LifeLog(msg);
        }
      } else {
        snprintf(msg, sizeof(msg), "metal-net: pkg http %u %s", st, s->pkg_url);
        LifeLog(msg);
      }

      LifeFreePkgBuf(s);
      s->pkg_i++;
      s->step = LIFE_SEED;
      continue;
    }

    case LIFE_NTP:
      if (!LifeHasLease()) {
        s->step = LIFE_DOWN;
        continue;
      }

      s->ntp_host[0] = '\0';
      if (s->ntp_i == 0u) {
        pm_metal_net_ip_ifcfg_t cfg;

        if (pm_metal_net_ip_if_get(&cfg) == 0 && cfg.ntp[0] != '\0') {
          snprintf(s->ntp_host, sizeof(s->ntp_host), "%s", cfg.ntp);
        }
      } else {
        uint32_t fi;

        fi = s->ntp_i - 1u;
        if (fi < (uint32_t)(sizeof(mNtpFallback) / sizeof(mNtpFallback[0]))) {
          snprintf(s->ntp_host, sizeof(s->ntp_host), "%s", mNtpFallback[fi]);
        }
      }

      if (s->ntp_host[0] == '\0') {
        s->ntp_i++;
        if (s->ntp_i > (uint32_t)(sizeof(mNtpFallback) / sizeof(mNtpFallback[0]))) {
          LifeLog("metal-net: ntp fail");
          s->step = LIFE_UP;
          continue;
        }

        continue;
      }

      s->ntp_aw = pm_metal_net_ntp_sync(s->ntp_host);
      if (s->ntp_aw == PM_METAL_ASYNC_HANDLE_INVALID) {
        s->ntp_i++;
        continue;
      }

      s->step = LIFE_NTP_WAIT;
      return pm_metal_async_await(h, s->ntp_aw);

    case LIFE_NTP_WAIT:
      if (pm_metal_net_ntp_status(s->ntp_aw) == 0u) {
        s->last_ntp_us = pm_metal_time_mono_us();
        s->ntp_ok      = 1;
        /* Quiet success — clock tray turns green; no console spam. */
        s->ntp_aw = PM_METAL_ASYNC_HANDLE_INVALID;
        s->step   = LIFE_UP;
        continue;
      }

      s->ntp_aw = PM_METAL_ASYNC_HANDLE_INVALID;
      s->ntp_i++;
      if (s->ntp_i > (uint32_t)(sizeof(mNtpFallback) / sizeof(mNtpFallback[0]))) {
        s->last_ntp_us = pm_metal_time_mono_us();
        s->ntp_ok      = 0;
        LifeLog("metal-net: ntp fail");
        s->step = LIFE_UP;
        continue;
      }

      s->step = LIFE_NTP;
      continue;

    case LIFE_UP: {
      uint64_t now;
      uint64_t period;

      pm_metal_net_ip_poll();
      if (!LifeHasLease()) {
        s->step = LIFE_DOWN;
        continue;
      }

      now = pm_metal_time_mono_us();

      /* HTTP seed only when run/tab called seed_ensure (pkg_want). */
      if (s->pkg_want != 0u && s->pkg_name[0] != '\0' && !pm_metal_pkg_ready(s->pkg_name) &&
          s->pkg_busy == 0u) {
        LifeResolvePkgHost(s);
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

      return pm_metal_async_await(h, pm_metal_async_sleep_us(LIFE_UP_US));
    }

    default:
      s->step = LIFE_DOWN;
      continue;
    }
  }
}

int pm_metal_net_ip_life_seed_ensure(const char *name)
{
  uint64_t deadline;

  if (name == NULL || name[0] == '\0') {
    return -1;
  }

  if (pm_metal_pkg_lookup(name) == NULL) {
    return 0;
  }

  if (pm_metal_pkg_ready(name)) {
    return 0;
  }

  if (mLife == NULL) {
    return -1;
  }

  if (!LifeHasLease()) {
    LifeLog("metal-net: pkg ensure needs lease");
    return -1;
  }

  snprintf(mLife->pkg_name, sizeof(mLife->pkg_name), "%s", name);
  {
    char host[PM_METAL_NET_TFTP_HOST_MAX];
    char msg[128];

    (void)pm_metal_net_ip_seed_host(host, sizeof(host));
    snprintf(
      msg, sizeof(msg), "metal-net: pkg ensure '%s' -> http://%s:8080/ (run/tab)", name, host);
    LifeLog(msg);
  }
  mLife->pkg_want = 1;
  mLife->pkg_done = 0;
  deadline        = pm_metal_time_mono_us() + LIFE_PKG_ENSURE_US;

  while (!pm_metal_pkg_ready(name)) {
    if (mLife->pkg_done != 0u && mLife->pkg_busy == 0u) {
      break;
    }

    if (pm_metal_time_mono_us() >= deadline) {
      LifeLog("metal-net: pkg ensure timeout");
      break;
    }

    pm_metal_net_ip_poll();
    pm_metal_run_poll_all();
  }

  mLife->pkg_want = 0;
  return pm_metal_pkg_ready(name) ? 0 : -1;
}

int pm_metal_net_ip_life_start(void)
{
  pm_metal_async_handle_t h;
  pm_metal_async_handle_t th;

  if (mLife != NULL) {
    return 0;
  }

  h = pm_metal_async_coro_create(LifeStep, sizeof(*mLife));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return -1;
  }

  mLife = (life_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (mLife == NULL) {
    pm_metal_async_coro_close(h);
    return -1;
  }

  mLife->step     = LIFE_DOWN;
  mLife->http_aw  = PM_METAL_ASYNC_HANDLE_INVALID;
  mLife->ntp_aw   = PM_METAL_ASYNC_HANDLE_INVALID;
  mLife->pkg_done = 1; /* no fetch until run/tab asks */
  mLife->pkg_want = 0;
  mLife->pkg_busy = 0;

  th = pm_metal_async_create_task(h);
  if (th == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close(h);
    mLife = NULL;
    return -1;
  }

  return 0;
}

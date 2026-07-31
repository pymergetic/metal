/** @file
  Package HTTP seed — on-demand fetch of guest/assets onto ESP.
  Catalog/ready live in pkg.c; transport host from net_ip_seed_host.
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/guest/pkg/pkg.h>
#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/http/http.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/util/ip.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/time/time.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/run/run.h>

#define PKG_SEED_DOWN_US   500000ull
#define PKG_SEED_ENSURE_US (180ull * 1000000ull)

typedef enum {
  PKG_SEED_IDLE = 0,
  PKG_SEED_FETCH,
  PKG_SEED_WAIT
} pkg_seed_step_t;

typedef struct {
  pkg_seed_step_t         step;
  pm_metal_async_handle_t http_aw;
  uint32_t                pkg_i;
  uint32_t                pkg_got;
  uint8_t                *pkg_buf;
  uint32_t                pkg_cap;
  uint8_t                 pkg_done;
  uint8_t                 pkg_want;
  uint8_t                 pkg_busy;
  uint8_t                 pkg_phase;
  char                    pkg_name[32];
  char                    pkg_host[PM_METAL_NET_TFTP_HOST_MAX];
  char                    pkg_url[192];
} pkg_seed_t;

static pkg_seed_t *mSeed;

static int32_t PkgSeedHasLease(void)
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

static int32_t PkgSeedBuildHttpUrl(char *out, uintptr_t cap, const char *host, const char *path)
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

/* Guest seed rows: assets.list + .aot/.wasm (+ .sig). Everything else = asset. */
static int32_t PkgSeedPathIsGuest(const char *path)
{
  if (path == NULL) {
    return 0;
  }

  if (strstr(path, "assets.list") != NULL) {
    return 1;
  }

  if (strstr(path, ".aot") != NULL || strstr(path, ".wasm") != NULL ||
      strstr(path, ".sig") != NULL) {
    return 1;
  }

  return 0;
}

static int32_t PkgSeedPhaseSatisfied(pkg_seed_t *s)
{
  if (s == NULL || s->pkg_name[0] == '\0') {
    return 1;
  }

  if (s->pkg_phase == PM_METAL_PKG_SEED_ASSETS) {
    return pm_metal_pkg_ready(s->pkg_name);
  }

  return pm_metal_pkg_guest_ready(s->pkg_name);
}

static void PkgSeedFreeBuf(pkg_seed_t *s)
{
  if (s->pkg_buf != NULL) {
    pm_metal_mem_free(s->pkg_buf);
    s->pkg_buf = NULL;
  }

  s->pkg_cap = 0;
}

static void PkgSeedLog(const char *line)
{
  pm_metal_console_com1_write("\r\n", 2);
  pm_metal_log(line);
  pm_metal_shell_prompt_dirty();
}

static pm_metal_status_t PkgSeedStep(pm_metal_async_handle_t self_h)
{
  pkg_seed_t             *s;
  pm_metal_async_handle_t h;

  h = self_h;
  s = (pkg_seed_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  for (;;) {
    switch (s->step) {
    case PKG_SEED_IDLE:
      if (s->pkg_want != 0u && s->pkg_name[0] != '\0' && !PkgSeedPhaseSatisfied(s) &&
          s->pkg_busy == 0u) {
        (void)pm_metal_net_ip_seed_host(s->pkg_host, sizeof(s->pkg_host));
        s->pkg_i    = 0;
        s->pkg_got  = 0;
        s->pkg_done = 0;
        s->step     = PKG_SEED_FETCH;
        continue;
      }

      return pm_metal_async_await(
        h, pm_metal_async_sleep_us((s->pkg_want != 0u) ? PKG_SEED_DOWN_US : (5ull * 1000000ull)));

    case PKG_SEED_FETCH: {
      const pm_metal_pkg_file_t *files;
      uint32_t                   nfiles;

      s->pkg_busy = 1;
      files       = pm_metal_pkg_files(s->pkg_name, &nfiles);
      /*
       * ASSETS: stop once one IWAD is present.
       * GUEST: never early-exit on guest_ready — must finish wasm+AOT rows so
       * image_open AOT failure can fall back to wasm on ESP.
       */
      if (s->pkg_phase == PM_METAL_PKG_SEED_ASSETS && PkgSeedPhaseSatisfied(s)) {
        s->pkg_done = 1;
        s->pkg_want = 0;
        s->pkg_busy = 0;
        PkgSeedFreeBuf(s);
        s->step = PKG_SEED_IDLE;
        continue;
      }

      while (files != NULL && s->pkg_i < nfiles) {
        const pm_metal_pkg_file_t *pkg;
        uint32_t                   sz;
        int32_t                    guest_slot;

        pkg        = &files[s->pkg_i];
        guest_slot = PkgSeedPathIsGuest(pkg->esp_path);
        if (s->pkg_phase == PM_METAL_PKG_SEED_GUEST && guest_slot == 0) {
          s->pkg_i++;
          continue;
        }

        if (s->pkg_phase == PM_METAL_PKG_SEED_ASSETS && guest_slot != 0) {
          s->pkg_i++;
          continue;
        }

        if (pm_metal_esp_file_size(pkg->esp_path, &sz) == 0 ||
            pm_metal_pkg_file_optional(s->pkg_name, pkg)) {
          s->pkg_i++;
          continue;
        }

        PkgSeedFreeBuf(s);
        s->pkg_cap = pkg->cap;
        s->pkg_buf =
          (uint8_t *)pm_metal_mem_alloc(s->pkg_cap, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
        if (s->pkg_buf == NULL) {
          char msg[160];

          snprintf(msg, sizeof(msg), "metal-pkg: oom %u %s", s->pkg_cap, pkg->esp_path);
          PkgSeedLog(msg);
          s->pkg_i++;
          continue;
        }
        if (PkgSeedBuildHttpUrl(s->pkg_url, sizeof(s->pkg_url), s->pkg_host, pkg->url_path) < 0) {
          PkgSeedFreeBuf(s);
          PkgSeedLog("metal-pkg: url build fail");
          s->pkg_i++;
          continue;
        }

        s->http_aw = pm_metal_net_http_get(s->pkg_url, s->pkg_buf, s->pkg_cap);
        if (s->http_aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          PkgSeedFreeBuf(s);
          PkgSeedLog("metal-pkg: http_get start fail");
          s->pkg_i++;
          continue;
        }

        s->step = PKG_SEED_WAIT;
        return pm_metal_async_await(h, s->http_aw);
      }

      s->pkg_done = 1;
      s->pkg_want = 0;
      s->pkg_busy = 0;
      if (s->pkg_got > 0u && PkgSeedPhaseSatisfied(s)) {
        PkgSeedLog("metal-pkg: seeded");
      } else if (!PkgSeedPhaseSatisfied(s)) {
        PkgSeedLog("metal-pkg: fetch miss");
      }

      PkgSeedFreeBuf(s);
      s->step = PKG_SEED_IDLE;
      continue;
    }

    case PKG_SEED_WAIT: {
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
        if (n < s->pkg_cap) {
          uint8_t *shrunk;

          shrunk = (uint8_t *)pm_metal_mem_realloc(s->pkg_buf, n);
          if (shrunk != NULL) {
            s->pkg_buf = shrunk;
            s->pkg_cap = n;
          }
        }

        if (pm_metal_esp_cache_take(pkg->esp_path, s->pkg_buf, n) == 0) {
          s->pkg_got++;
          s->pkg_buf = NULL;
          s->pkg_cap = 0;
        } else {
          snprintf(msg, sizeof(msg), "metal-pkg: cache fail %s", pkg->esp_path);
          PkgSeedLog(msg);
        }
      } else if (pkg != NULL && st == 200u && n == 0u) {
        snprintf(msg, sizeof(msg), "metal-pkg: http empty %s", s->pkg_url);
        PkgSeedLog(msg);
      } else {
        snprintf(msg, sizeof(msg), "metal-pkg: http %u %s", st, s->pkg_url);
        PkgSeedLog(msg);
      }

      PkgSeedFreeBuf(s);
      s->pkg_i++;
      s->step = PKG_SEED_FETCH;
      continue;
    }

    default:
      s->step = PKG_SEED_IDLE;
      continue;
    }
  }
}

static int32_t PkgSeedStart(void)
{
  pm_metal_async_handle_t h;
  pm_metal_async_handle_t th;

  if (mSeed != NULL) {
    return 0;
  }

  h = pm_metal_async_coro_create(PkgSeedStep, sizeof(*mSeed));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return -1;
  }

  mSeed = (pkg_seed_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (mSeed == NULL) {
    pm_metal_async_coro_close(h);
    return -1;
  }

  mSeed->step     = PKG_SEED_IDLE;
  mSeed->http_aw  = PM_METAL_ASYNC_HANDLE_INVALID;
  mSeed->pkg_done = 1;
  mSeed->pkg_want = 0;
  mSeed->pkg_busy = 0;

  th = pm_metal_async_create_task(h);
  if (th == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close(h);
    mSeed = NULL;
    return -1;
  }

  return 0;
}

static int32_t PkgSeedEnsure(const char *name, uint32_t phase)
{
  uint64_t    deadline;
  const char *phase_s;

  if (name == NULL || name[0] == '\0') {
    return -1;
  }

  if (phase != PM_METAL_PKG_SEED_GUEST && phase != PM_METAL_PKG_SEED_ASSETS) {
    return -1;
  }

  if (pm_metal_pkg_lookup(name) == NULL) {
    return 0;
  }

  if (phase == PM_METAL_PKG_SEED_ASSETS && pm_metal_pkg_ready(name)) {
    return 0;
  }

  if (PkgSeedStart() != 0 || mSeed == NULL) {
    return -1;
  }

  /*
   * Trigger is load/run (pkg_ensure), never lease-up. Wait for DHCP so an
   * early `run doom` still downloads once the link is up — no auto-fetch.
   */
  deadline = pm_metal_time_mono_us() + PKG_SEED_ENSURE_US;
  while (!PkgSeedHasLease()) {
    if (pm_metal_time_mono_us() >= deadline) {
      PkgSeedLog("metal-pkg: ensure needs lease");
      return -1;
    }
    pm_metal_net_ip_poll();
    pm_metal_run_poll_all();
  }

  snprintf(mSeed->pkg_name, sizeof(mSeed->pkg_name), "%s", name);
  mSeed->pkg_phase = (uint8_t)phase;
  phase_s          = (phase == PM_METAL_PKG_SEED_ASSETS) ? "assets" : "guest";
  {
    char host[PM_METAL_NET_TFTP_HOST_MAX];
    char msg[160];

    (void)pm_metal_net_ip_seed_host(host, sizeof(host));
    snprintf(msg,
             sizeof(msg),
             "metal-pkg: ensure '%s' %s -> http://%s:8080/ (load/run)",
             name,
             phase_s,
             host);
    PkgSeedLog(msg);
  }
  mSeed->pkg_want = 1;
  mSeed->pkg_done = 0;
  mSeed->pkg_i    = 0;
  mSeed->pkg_got  = 0;

  for (;;) {
    if (phase == PM_METAL_PKG_SEED_GUEST) {
      if (mSeed->pkg_done != 0u && mSeed->pkg_busy == 0u) {
        break;
      }
    } else {
      if (PkgSeedPhaseSatisfied(mSeed)) {
        break;
      }
      if (mSeed->pkg_done != 0u && mSeed->pkg_busy == 0u) {
        break;
      }
    }

    if (pm_metal_time_mono_us() >= deadline) {
      PkgSeedLog("metal-pkg: ensure timeout");
      break;
    }

    pm_metal_net_ip_poll();
    pm_metal_run_poll_all();
  }

  mSeed->pkg_want = 0;
  return PkgSeedPhaseSatisfied(mSeed) ? 0 : -1;
}

static int32_t PkgWasmExists(const char *name)
{
  char     path[96];
  uint32_t sz;

  snprintf(path, sizeof(path), "mods/apps/%s/%s.wasm", name, name);
  return (pm_metal_esp_file_size(path, &sz) == 0) ? 1 : 0;
}

int32_t pm_metal_pkg_ensure(const char *name)
{
  if (pm_metal_pkg_lookup(name) == NULL) {
    return 0;
  }

  /*
   * Guest binary only (no IWAD). Require wasm on ESP too — AOT-only ready
   * used to skip the wasm seed row, then image_open AOT fail could not fall back.
   */
  if (pm_metal_pkg_guest_ready(name) && PkgWasmExists(name)) {
    return 0;
  }

  return PkgSeedEnsure(name, PM_METAL_PKG_SEED_GUEST);
}

int32_t pm_metal_pkg_ensure_assets(const char *name)
{
  const pm_metal_pkg_t *pkg;
  char                  path[96];
  uint32_t              sz;

  pkg = pm_metal_pkg_lookup(name);
  if (pkg == NULL) {
    return 0;
  }

  if (pm_metal_pkg_ready(name)) {
    return 0;
  }

  /* Manifest never landed — cannot know assets; force another guest pass. */
  if (pkg->nassets == 0u) {
    snprintf(path, sizeof(path), "mods/apps/%s/assets.list", name);
    if (pm_metal_esp_file_size(path, &sz) != 0) {
      if (PkgSeedEnsure(name, PM_METAL_PKG_SEED_GUEST) != 0) {
        return -1;
      }
      pkg = pm_metal_pkg_lookup(name);
      if (pkg == NULL || pkg->nassets == 0u) {
        return -1;
      }
    } else {
      return -1;
    }
  }

  return PkgSeedEnsure(name, PM_METAL_PKG_SEED_ASSETS);
}

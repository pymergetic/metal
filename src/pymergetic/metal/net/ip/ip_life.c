/** @file
  Net life — lease watch, NTP, ASGI/SSH autoload on lease-up.
  Package HTTP seed lives in guest/pkg/pkg_seed.c.
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_life.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/asgi/asgi.h>
#include <pymergetic/metal/net/ntp/ntp.h>
#include <pymergetic/metal/net/ssh/ssh.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/util/ip.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/time/time.h>

#define LIFE_DOWN_US       500000ull
#define LIFE_UP_US         5000000ull
#define LIFE_NTP_PERIOD_US (30ull * 60ull * 1000000ull)

#ifndef CONFIG_PM_METAL_NET_PKG_SEED_HOST
#define CONFIG_PM_METAL_NET_PKG_SEED_HOST ""
#endif

typedef enum {
  LIFE_DOWN = 0,
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
  pm_metal_async_handle_t ntp_aw;
  uint32_t                ntp_i;
  uint64_t                last_ntp_us;
  uint8_t                 ntp_ok;
  uint8_t                 logged_up;
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

/*
 * Shared with the public pm_metal_net_ip_seed_host() guest/host accessor
 * (net.h) -- keep the resolution in one place so a diagnostic caller and
 * package HTTP seed always agree on "the host".
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

  /* 3) Optional Kconfig host (empty = none). */
  if (CONFIG_PM_METAL_NET_PKG_SEED_HOST[0] != '\0') {
    snprintf(out, out_cap, "%s", CONFIG_PM_METAL_NET_PKG_SEED_HOST);
    return (out[0] != '\0') ? 0 : -1;
  }

  return -1;
}

static void LifeLog(const char *line)
{
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
        s->logged_up = 0;
        return pm_metal_async_await(h, pm_metal_async_sleep_us(LIFE_DOWN_US));
      }

      if (s->logged_up == 0u) {
        /* Quiet success — tray/tree show lease; avoid clobbering the prompt. */
        s->logged_up = 1;
        (void)pm_metal_net_asgi_autoload();
        (void)pm_metal_net_ssh_autoload();
      }

      s->step = LIFE_UP;
      continue;

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

      now    = pm_metal_time_mono_us();
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

  mLife->step    = LIFE_DOWN;
  mLife->ntp_aw  = PM_METAL_ASYNC_HANDLE_INVALID;
  mLife->ntp_ok  = 0;
  mLife->ntp_i   = 0;

  th = pm_metal_async_create_task(h);
  if (th == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close(h);
    mLife = NULL;
    return -1;
  }

  return 0;
}

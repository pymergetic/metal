/** @file
  Shell `net` / `nslookup` — lives with net config stack. (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/dev/net/net.h>
#include <pymergetic/metal/dev/net/net_cfg.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/time/time.h>

#define NSLOOKUP_TIMEOUT_MS 9000u

static void NetUsage(void)
{
  pm_metal_shell_out("usage: net [status [ethN]] | net set [ethN] <ip> <mask> <gw> [dns] | net set "
                     "[ethN] dhcp | net set [ethN] dhcp6 off|stateless|stateful");
}

static void NetShellCmd(int32_t argc, char **argv)
{
  char        buf[640];
  char        ifname[PM_METAL_NET_IFNAME_MAX];
  const char *iface;
  int32_t     i;

  ifname[0] = '\0';
  iface     = NULL;

  if (argc <= 1 || strcmp(argv[1], "status") == 0) {
    if (argc >= 3) {
      if (pm_metal_net_if_status_named(argv[2], buf, sizeof(buf)) != 0) {
        pm_metal_shell_out("net: unavailable");
      } else {
        pm_metal_shell_out(buf);
      }
    } else if (pm_metal_net_if_status(buf, sizeof(buf)) != 0) {
      pm_metal_shell_out("net: unavailable");
    } else {
      pm_metal_shell_out_lines(buf);
    }

    return;
  }

  if (strcmp(argv[1], "set") != 0) {
    NetUsage();
    return;
  }

  i = 2;
  if (i < argc && strncmp(argv[i], "eth", 3) == 0) {
    snprintf(ifname, sizeof(ifname), "%.*s", (int)(sizeof(ifname) - 1), argv[i]);
    iface = ifname;
    i++;
  }

  if (i >= argc) {
    NetUsage();
    return;
  }

  if (strcmp(argv[i], "dhcp") == 0) {
    if (pm_metal_net_if_set_dhcp_named(iface) != 0) {
      pm_metal_shell_out("net set: failed");
    } else {
      pm_metal_shell_out("net set: ok");
      if (pm_metal_net_if_status_named(iface, buf, sizeof(buf)) == 0) {
        pm_metal_shell_out(buf);
      }
    }

    return;
  }

  if (strcmp(argv[i], "dhcp6") == 0) {
    int32_t d6;

    if (i + 1 >= argc) {
      pm_metal_shell_out("usage: net set [ethN] dhcp6 off|stateless|stateful");
      return;
    }

    if (strcmp(argv[i + 1], "off") == 0) {
      d6 = PM_METAL_NET_DHCP6_OFF;
    } else if (strcmp(argv[i + 1], "stateless") == 0) {
      d6 = PM_METAL_NET_DHCP6_STATELESS;
    } else if (strcmp(argv[i + 1], "stateful") == 0) {
      d6 = PM_METAL_NET_DHCP6_STATEFUL;
    } else {
      pm_metal_shell_out("usage: net set [ethN] dhcp6 off|stateless|stateful");
      return;
    }

    if (pm_metal_net_if_set_dhcp6_named(iface, d6) != 0) {
      pm_metal_shell_out("net set: failed");
    } else {
      pm_metal_shell_out("net set: ok");
      if (pm_metal_net_if_status_named(iface, buf, sizeof(buf)) == 0) {
        pm_metal_shell_out(buf);
      }
    }

    return;
  }

  /* net set [ethN] <ip> <mask> <gw> [dns] */
  if (i + 2 >= argc) {
    NetUsage();
    return;
  }

  if (pm_metal_net_if_set_named(
        iface, argv[i], argv[i + 1], argv[i + 2], (i + 3 < argc) ? argv[i + 3] : NULL) != 0) {
    pm_metal_shell_out("net set: failed");
  } else {
    pm_metal_shell_out("net set: ok");
    if (pm_metal_net_if_status_named(iface, buf, sizeof(buf)) == 0) {
      pm_metal_shell_out(buf);
    }
  }
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_net,
                   "net",
                   "net [status [ethN]] | net set [ethN] <ip> <mask> <gw> [dns] | dhcp | dhcp6",
                   NetShellCmd);

static void NslookupShellCmd(int32_t argc, char **argv)
{
  pm_metal_async_handle_t dns_h;
  pm_metal_async_handle_t task_h;
  uint64_t                deadline;

  if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
    pm_metal_shell_out("usage: nslookup <host>");
    return;
  }

  if (pm_metal_shell_job_busy()) {
    pm_metal_shell_out("nslookup: busy");
    return;
  }

  dns_h = pm_metal_net_dns(argv[1]);
  if (dns_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_shell_out("nslookup: start failed");
    return;
  }

  task_h = pm_metal_async_create_task(dns_h);
  if (task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close(dns_h);
    pm_metal_shell_out("nslookup: task failed");
    return;
  }

  deadline = pm_metal_time_mono_us() + ((uint64_t)NSLOOKUP_TIMEOUT_MS * 1000ull) + 500000ull;
  if (pm_metal_shell_job_start("nslookup", task_h, dns_h, argv[1], deadline) != 0) {
    pm_metal_async_task_cancel(task_h);
    pm_metal_shell_out("nslookup: job failed");
    return;
  }

  pm_metal_shell_out("nslookup: ...");
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_nslookup,
                   "nslookup",
                   "nslookup <host>   resolve name (DNS / hosts)",
                   NslookupShellCmd);

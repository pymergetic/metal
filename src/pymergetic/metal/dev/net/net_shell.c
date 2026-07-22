/** @file
  Shell `net` command — lives with net config stack. (impl: efi|bios)
**/
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/dev/net/net_cfg.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

STATIC
VOID
NetShellCmd (
  CONST CHAR8  *arg
  )
{
  CHAR8         buf[640];
  CHAR8         ip[16];
  CHAR8         mask[16];
  CHAR8         gw[16];
  CHAR8         dns[16];
  CHAR8         ifname[PM_METAL_NET_IFNAME_MAX];
  UINTN         i;
  UINTN         n;
  CONST CHAR8  *p;
  CHAR8        *outs[4];
  CHAR8        *cur;
  UINTN         oi;

  ifname[0] = '\0';
  if (arg == NULL || arg[0] == '\0') {
    if (pm_metal_net_if_status (buf, sizeof (buf)) != 0) {
      pm_metal_shell_out ("net: unavailable");
    } else {
      pm_metal_shell_out_lines (buf);
    }

    return;
  }

  if (AsciiStrCmp (arg, "status") == 0) {
    if (pm_metal_net_if_status (buf, sizeof (buf)) != 0) {
      pm_metal_shell_out ("net: unavailable");
    } else {
      pm_metal_shell_out_lines (buf);
    }

    return;
  }

  if (AsciiStrnCmp (arg, "status ", 7) == 0) {
    p = arg + 7;
    while (*p == ' ') {
      p++;
    }

    if (pm_metal_net_if_status_named (p, buf, sizeof (buf)) != 0) {
      pm_metal_shell_out ("net: unavailable");
    } else {
      pm_metal_shell_out (buf);
    }

    return;
  }

  if (AsciiStrnCmp (arg, "set ", 4) != 0) {
    pm_metal_shell_out (
      "usage: net [status [ethN]] | net set [ethN] <ip> <mask> <gw> [dns] | net set [ethN] dhcp | net set [ethN] dhcp6 off|stateless|stateful"
      );
    return;
  }

  p = arg + 4;
  while (*p == ' ') {
    p++;
  }

  if (AsciiStrnCmp (p, "eth", 3) == 0) {
    UINTN  j;

    j = 3;
    while (p[j] >= '0' && p[j] <= '9' && j + 1 < sizeof (ifname)) {
      j++;
    }

    if (j > 3 && (p[j] == ' ' || p[j] == '\0')) {
      CopyMem (ifname, p, j);
      ifname[j] = '\0';
      p         += j;
      while (*p == ' ') {
        p++;
      }
    }
  }

  if (AsciiStrCmp (p, "dhcp") == 0) {
    if (pm_metal_net_if_set_dhcp_named (ifname[0] != '\0' ? ifname : NULL) != 0) {
      pm_metal_shell_out ("net set: failed");
    } else {
      pm_metal_shell_out ("net set: ok");
      if (pm_metal_net_if_status_named (
            ifname[0] != '\0' ? ifname : NULL,
            buf,
            sizeof (buf)
            ) == 0)
      {
        pm_metal_shell_out (buf);
      }
    }

    return;
  }

  if (AsciiStrnCmp (p, "dhcp6 ", 6) == 0) {
    CONST CHAR8  *mode;
    INT32         d6;

    mode = p + 6;
    if (AsciiStrCmp (mode, "off") == 0) {
      d6 = PM_METAL_NET_DHCP6_OFF;
    } else if (AsciiStrCmp (mode, "stateless") == 0) {
      d6 = PM_METAL_NET_DHCP6_STATELESS;
    } else if (AsciiStrCmp (mode, "stateful") == 0) {
      d6 = PM_METAL_NET_DHCP6_STATEFUL;
    } else {
      pm_metal_shell_out ("usage: net set [ethN] dhcp6 off|stateless|stateful");
      return;
    }

    if (pm_metal_net_if_set_dhcp6_named (
          ifname[0] != '\0' ? ifname : NULL,
          d6
          ) != 0)
    {
      pm_metal_shell_out ("net set: failed");
    } else {
      pm_metal_shell_out ("net set: ok");
      if (pm_metal_net_if_status_named (
            ifname[0] != '\0' ? ifname : NULL,
            buf,
            sizeof (buf)
            ) == 0)
      {
        pm_metal_shell_out (buf);
      }
    }

    return;
  }

  outs[0] = ip;
  outs[1] = mask;
  outs[2] = gw;
  outs[3] = dns;
  ip[0] = mask[0] = gw[0] = dns[0] = '\0';
  oi    = 0;
  cur   = outs[0];
  n     = 0;
  for (i = 0; p[i] != '\0' && oi < 4; i++) {
    if (p[i] == ' ') {
      cur[n] = '\0';
      oi++;
      if (oi >= 4) {
        break;
      }

      cur = outs[oi];
      n   = 0;
      while (p[i + 1] == ' ') {
        i++;
      }

      continue;
    }

    if (n + 1 < 16) {
      cur[n++] = p[i];
    }
  }

  cur[n] = '\0';
  if (ip[0] == '\0' || mask[0] == '\0' || gw[0] == '\0') {
    pm_metal_shell_out (
      "usage: net set [ethN] <ip> <mask> <gw> [dns] | net set [ethN] dhcp | net set [ethN] dhcp6 off|stateless|stateful"
      );
    return;
  }

  if (pm_metal_net_if_set_named (
        ifname[0] != '\0' ? ifname : NULL,
        ip,
        mask,
        gw,
        dns[0] != '\0' ? dns : NULL
        ) != 0)
  {
    pm_metal_shell_out ("net set: failed");
  } else {
    pm_metal_shell_out ("net set: ok");
    if (pm_metal_net_if_status_named (
          ifname[0] != '\0' ? ifname : NULL,
          buf,
          sizeof (buf)
          ) == 0)
    {
      pm_metal_shell_out (buf);
    }
  }
}

STATIC CONST pm_metal_shell_cmd_t  g_pm_metal_shell_cmd_net = {
  "net",
  "net [status [ethN]] | net set [ethN] <ip> <mask> <gw> [dns] | dhcp | dhcp6",
  NetShellCmd
};

void
pm_metal_shell_cmds_register_net (
  VOID
  )
{
  pm_metal_shell_cmd_register (&g_pm_metal_shell_cmd_net);
}

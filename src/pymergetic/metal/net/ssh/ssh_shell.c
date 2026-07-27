/*
 * Shell: sshd autoload|listen [port]|close <srv>
 */
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/net/ssh/ssh.h>
#include <pymergetic/metal/shell/shell_cmd.h>

static uint32_t parse_u32(const char *s)
{
  uint32_t v;

  v = 0;
  if (s == NULL) {
    return 0;
  }
  while (*s >= '0' && *s <= '9') {
    v = v * 10u + (uint32_t)(*s - '0');
    s++;
  }
  return v;
}

static void SshdShellCmd(int32_t argc, char **argv)
{
  if (argc < 2) {
    pm_metal_shell_out("sshd autoload|listen [port]|close <srv>|status\n");
    return;
  }
  if (strcmp(argv[1], "autoload") == 0) {
    if (pm_metal_net_ssh_autoload() != 0) {
      pm_metal_shell_out("sshd: autoload failed\n");
    } else {
      pm_metal_shell_out("sshd: autoload ok\n");
    }
    return;
  }
  if (strcmp(argv[1], "status") == 0) {
    char line[128];

    if (pm_metal_net_ssh_status(line, (uint32_t)sizeof(line)) < 0) {
      pm_metal_shell_out("sshd: status failed\n");
      return;
    }
    pm_metal_shell_out(line);
    pm_metal_shell_out("\n");
    return;
  }
  if (strcmp(argv[1], "listen") == 0) {
    uint32_t               port;
    pm_metal_net_ssh_srv_h srv;
    char                   line[64];

    port = 22;
    if (argc >= 3) {
      port = parse_u32(argv[2]);
    }
    srv = pm_metal_net_ssh_listen(port);
    if (srv == PM_METAL_NET_SSH_SRV_INVALID) {
      pm_metal_shell_out("sshd: listen failed\n");
      return;
    }
    snprintf(line, sizeof(line), "sshd: srv=%u port=%u\n", (unsigned)srv, (unsigned)port);
    pm_metal_shell_out(line);
    return;
  }
  if (strcmp(argv[1], "close") == 0 && argc >= 3) {
    pm_metal_net_ssh_close((pm_metal_net_ssh_srv_h)parse_u32(argv[2]));
    pm_metal_shell_out("sshd: closed\n");
    return;
  }
  pm_metal_shell_out("sshd: unknown subcommand\n");
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_sshd, "sshd",
                   "sshd autoload|listen [port]|close <srv>|status", SshdShellCmd);

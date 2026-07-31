/*
 * Shell: httpd listen|mount|apps|close|autoload
 */
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/net/asgi/asgi.h>
#include <pymergetic/metal/shell/shell/shell.h>
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

static void HttpdShellCmd(int32_t argc, char **argv)
{
  if (argc < 2) {
    pm_metal_shell_out("httpd autoload|listen [port]|mount <path> <app>|close <srv>|apps\n");
    return;
  }
  if (strcmp(argv[1], "autoload") == 0) {
    if (pm_metal_net_asgi_autoload() != 0) {
      pm_metal_shell_out("httpd: autoload failed\n");
    } else {
      pm_metal_shell_out("httpd: autoload ok\n");
    }
    return;
  }
  if (strcmp(argv[1], "listen") == 0) {
    uint32_t                port;
    pm_metal_net_asgi_srv_h srv;
    pm_metal_net_asgi_app_h health;
    char                    line[64];

    port = 8000;
    if (argc >= 3) {
      port = parse_u32(argv[2]);
    }
    srv = pm_metal_net_asgi_listen(port, NULL, 0, PM_METAL_TLS_CREDS_INVALID);
    if (srv == PM_METAL_NET_ASGI_SRV_INVALID) {
      pm_metal_shell_out("httpd: listen failed\n");
      return;
    }
    health = pm_metal_net_asgi_app_health();
    (void)pm_metal_net_asgi_mount(srv, "/health", health);
    (void)pm_metal_net_asgi_mount(srv, "/", health);
    snprintf(line, sizeof(line), "httpd: srv=%u port=%u\n", (unsigned)srv, (unsigned)port);
    pm_metal_shell_out(line);
    return;
  }
  if (strcmp(argv[1], "close") == 0 && argc >= 3) {
    pm_metal_net_asgi_close((pm_metal_net_asgi_srv_h)parse_u32(argv[2]));
    pm_metal_shell_out("httpd: closed\n");
    return;
  }
  pm_metal_shell_out("httpd: unknown subcommand\n");
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_httpd,
                   "httpd",
                   "httpd autoload|listen [port]|close <srv>",
                   HttpdShellCmd);

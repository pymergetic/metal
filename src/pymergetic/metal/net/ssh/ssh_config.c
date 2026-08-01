/*
 * sshd config — in-memory defaults for W8 (no sync fs JSON yet).
 */
#include "ssh_config.h"

#include <string.h>

static pm_metal_sshd_cfg_t g_cfg;
static int32_t g_cfg_loaded;

pm_metal_sshd_cfg_t *pm_metal_net_ssh_cfg(void)
{
  if (g_cfg_loaded == 0) {
    (void)pm_metal_net_ssh_cfg_load();
  }
  return &g_cfg;
}

int32_t pm_metal_net_ssh_cfg_load(void)
{
  memset(&g_cfg, 0, sizeof(g_cfg));
  g_cfg.port = 22u;
  g_cfg.budget_pct = 0u;
  g_cfg.auth_passwd = 1;
  g_cfg.auth_pubkey = 1;
  g_cfg.auth_sslcert = 0;
  {
    static const char hk[] = "/etc/ssh/dropbear_ed25519_host_key";
    size_t n;

    n = strlen(hk);
    if (n >= sizeof(g_cfg.host_key)) {
      n = sizeof(g_cfg.host_key) - 1u;
    }
    memcpy(g_cfg.host_key, hk, n);
    g_cfg.host_key[n] = '\0';
  }
  /* Password users: bringup/tests call pm_metal_auth_users_set with a real hash. */
  g_cfg_loaded = 1;
  return 0;
}

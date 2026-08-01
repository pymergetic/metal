/*
 * Dropbear session glue: auth + PTY pump + coop poll (no shell mirror yet).
 */
#include "ssh_dropbear.h"

#include "dropbear_stubs/setjmp.h"
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "dropbear_fd.h"
#include "ssh_config.h"

#include <pymergetic/metal/auth/__init__.h>
#include <pymergetic/metal/dev/stream/__init__.h>
#include <pymergetic/metal/log/__init__.h>
#include <pymergetic/metal/net/ip/__init__.h>

extern void svr_session_setup(int sock, int childpipe);
extern int session_loop_once(void (*loophandler)(void));
extern void svr_chansess_checksignal(void);
extern void metal_dropbear_session_reclaim(void);
extern void load_all_hostkeys(void);
extern void crypto_init(void);
extern void seedrandom(void);
extern void svr_dropbear_exit(int exitcode, const char *format, va_list param);
extern void svr_dropbear_log(int priority, const char *format, va_list param);
extern void (*_dropbear_exit)(int exitcode, const char *format, va_list param);
extern void (*_dropbear_log)(int priority, const char *format, va_list param);
extern sigjmp_buf metal_dropbear_jmp;
extern int metal_dropbear_jmp_ready;
extern void svr_getopts(int argc, char **argv);

#define SSH_SESS_MAX 2u

typedef struct {
  int32_t used;
  int sock_fd;
  uint32_t stream_h;
  pm_metal_stream_h pty_m;
  pm_metal_stream_h pty_s;
  int pty_m_fd;
  int pty_s_fd;
} ssh_sess_t;

static ssh_sess_t g_sess[SSH_SESS_MAX];
static int32_t g_db_inited;
static uint32_t g_active_sess;

static void db_once_init(void)
{
  char *argv[] = {(char *)"dropbear", (char *)"-i", (char *)"-R", NULL};

  if (g_db_inited != 0) {
    return;
  }
  _dropbear_exit = svr_dropbear_exit;
  _dropbear_log = svr_dropbear_log;
  crypto_init();
  seedrandom();
  svr_getopts(3, argv);
  load_all_hostkeys();
  g_db_inited = 1;
}

int32_t metal_dropbear_ensure_hostkeys(void)
{
  /* DELAY_HOSTKEY (-R): keys generated on first KEX; tree shows hostkey=delay. */
  return 0;
}

int metal_dropbear_auth_passwd_enabled(void)
{
  pm_metal_sshd_cfg_t *cfg;

  cfg = pm_metal_net_ssh_cfg();
  return (cfg != NULL && cfg->auth_passwd) ? 1 : 0;
}

int metal_dropbear_auth_pubkey_enabled(void)
{
  pm_metal_sshd_cfg_t *cfg;

  cfg = pm_metal_net_ssh_cfg();
  return (cfg != NULL && cfg->auth_pubkey) ? 1 : 0;
}

int metal_dropbear_auth_sslcert_enabled(void)
{
  return 0;
}

int metal_dropbear_auth_password(const char *user, const char *pass)
{
  if (metal_dropbear_auth_passwd_enabled() == 0 || user == NULL || pass == NULL) {
    return 0;
  }
  return pm_metal_auth_user_check(user, pass) ? 1 : 0;
}

int metal_dropbear_auth_pubkey(const char *user, const char *algo, const unsigned char *keyblob,
                               unsigned int keybloblen)
{
  if (metal_dropbear_auth_pubkey_enabled() == 0 || user == NULL || keyblob == NULL ||
      keybloblen == 0u) {
    return 0;
  }
  if (algo != NULL && algo[0] != '\0') {
    return pm_metal_auth_pubkey_check(user, algo, keyblob, keybloblen) ? 1 : 0;
  }
  return pm_metal_auth_pubkey_check(user, NULL, keyblob, keybloblen) ? 1 : 0;
}

int metal_dropbear_auth_sslcert(const char *requested_user, const unsigned char *cert_der,
                                unsigned int cert_len, const unsigned char *signed_data,
                                unsigned int signed_len, const unsigned char *signature,
                                unsigned int signature_len)
{
  (void)requested_user;
  (void)cert_der;
  (void)cert_len;
  (void)signed_data;
  (void)signed_len;
  (void)signature;
  (void)signature_len;
  return 0;
}

int metal_dropbear_spawn_command(void)
{
  return -1;
}

int metal_dropbear_pty_allocate(int *ptyfd, int *ttyfd, char *namebuf, int namebuflen)
{
  ssh_sess_t *s;

  if (ptyfd == NULL || ttyfd == NULL) {
    return -1;
  }
  if (g_active_sess == 0u || g_active_sess >= SSH_SESS_MAX) {
    return -1;
  }
  s = &g_sess[g_active_sess];
  if (s->used == 0 || s->pty_m_fd < 0 || s->pty_s_fd < 0) {
    return -1;
  }
  *ptyfd = s->pty_m_fd;
  *ttyfd = s->pty_s_fd;
  if (namebuf != NULL && namebuflen > 0) {
    static const char name[] = "/dev/pts/metal";
    size_t i;

    for (i = 0; i + 1u < (size_t)namebuflen && name[i] != '\0'; i++) {
      namebuf[i] = name[i];
    }
    namebuf[i] = '\0';
  }
  return 0;
}

/** PTY ready for channel — shell mirror deferred until console viewport exists. */
int metal_dropbear_shell_attach(int slave_fd, int master_fd)
{
  (void)slave_fd;
  (void)master_fd;
  return 0;
}

void metal_dropbear_run_cmd(const unsigned char *cmd, unsigned int cmd_len)
{
  (void)cmd;
  (void)cmd_len;
}

static void sess_cleanup(uint32_t id)
{
  ssh_sess_t *s;

  if (id == 0u || id >= SSH_SESS_MAX) {
    return;
  }
  s = &g_sess[id];
  if (s->used == 0) {
    return;
  }
  if (s->pty_m_fd >= 0) {
    metal_db_fd_release(s->pty_m_fd);
  }
  if (s->pty_s_fd >= 0) {
    metal_db_fd_release(s->pty_s_fd);
  }
  if (s->sock_fd >= 0) {
    metal_db_fd_release(s->sock_fd);
  }
  memset(s, 0, sizeof(*s));
  s->sock_fd = -1;
  s->pty_m_fd = -1;
  s->pty_s_fd = -1;
}

uint32_t metal_dropbear_session_start(uint32_t stream_h, pm_metal_stream_h pty_master,
                                      pm_metal_stream_h pty_slave)
{
  uint32_t id;
  ssh_sess_t *s;
  int sock_fd;
  int pty_m_fd;
  int pty_s_fd;

  if (stream_h == 0u || pty_master == PM_METAL_STREAM_INVALID ||
      pty_slave == PM_METAL_STREAM_INVALID) {
    return 0u;
  }
  for (id = 1u; id < SSH_SESS_MAX; id++) {
    if (g_sess[id].used == 0) {
      break;
    }
  }
  if (id >= SSH_SESS_MAX) {
    return 0u;
  }
  db_once_init();
  sock_fd = metal_db_fd_register_tcp(stream_h);
  pty_m_fd = metal_db_fd_register_stream(pty_master);
  pty_s_fd = metal_db_fd_register_stream(pty_slave);
  if (sock_fd < 0 || pty_m_fd < 0 || pty_s_fd < 0) {
    if (sock_fd >= 0) {
      metal_db_fd_release(sock_fd);
    }
    if (pty_m_fd >= 0) {
      metal_db_fd_release(pty_m_fd);
    }
    if (pty_s_fd >= 0) {
      metal_db_fd_release(pty_s_fd);
    }
    return 0u;
  }
  s = &g_sess[id];
  memset(s, 0, sizeof(*s));
  s->used = 1;
  s->sock_fd = sock_fd;
  s->stream_h = stream_h;
  s->pty_m = pty_master;
  s->pty_s = pty_slave;
  s->pty_m_fd = pty_m_fd;
  s->pty_s_fd = pty_s_fd;

  metal_dropbear_jmp_ready = 0;
  if (sigsetjmp(metal_dropbear_jmp, 0) != 0) {
    metal_dropbear_jmp_ready = 0;
    metal_dropbear_session_reclaim();
    sess_cleanup(id);
    g_active_sess = 0u;
    return 0u;
  }
  metal_dropbear_jmp_ready = 1;
  g_active_sess = id;
  svr_session_setup(sock_fd, -1);
  return id;
}

static void sess_loop_handler(void)
{
  uint8_t buf[256];
  ssh_sess_t *s;

  if (g_active_sess == 0u || g_active_sess >= SSH_SESS_MAX) {
    return;
  }
  s = &g_sess[g_active_sess];
  if (s->used == 0) {
    return;
  }
  pm_metal_net_ip_poll();
  (void)pm_metal_stream_try_read(s->pty_s, buf, sizeof(buf));
  svr_chansess_checksignal();
}

int32_t metal_dropbear_session_poll(uint32_t sess)
{
  ssh_sess_t *s;
  int rc;

  if (sess == 0u || sess >= SSH_SESS_MAX || g_sess[sess].used == 0) {
    return -1;
  }
  s = &g_sess[sess];
  g_active_sess = sess;
  metal_dropbear_jmp_ready = 0;
  if (sigsetjmp(metal_dropbear_jmp, 0) != 0) {
    metal_dropbear_jmp_ready = 0;
    metal_dropbear_session_reclaim();
    sess_cleanup(sess);
    g_active_sess = 0u;
    return -1;
  }
  metal_dropbear_jmp_ready = 1;
  if (metal_db_fd_is_closed(s->sock_fd)) {
    metal_dropbear_session_reclaim();
    sess_cleanup(sess);
    g_active_sess = 0u;
    return -1;
  }
  rc = session_loop_once(sess_loop_handler);
  if (rc != 0) {
    metal_dropbear_session_reclaim();
    sess_cleanup(sess);
    g_active_sess = 0u;
    return -1;
  }
  return 0;
}

void metal_dropbear_session_close(uint32_t sess)
{
  if (sess == 0u || sess >= SSH_SESS_MAX) {
    return;
  }
  if (g_sess[sess].used != 0) {
    metal_dropbear_session_reclaim();
    sess_cleanup(sess);
  }
  if (g_active_sess == sess) {
    g_active_sess = 0u;
  }
}

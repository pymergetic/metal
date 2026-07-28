/*
 * Dropbear session glue: auth, PTY shell pump, coop poll with setjmp exit.
 */
#include "ssh_dropbear.h"

/* Relative to this file so freestanding/clangd see sigsetjmp macros
 * without relying on -I dropbear_stubs ordering. */
#include "dropbear_stubs/setjmp.h"
#include <stdint.h>
#include <string.h>

#include "dropbear_fd.h"

#include <pymergetic/metal/auth/auth.h>
#include <pymergetic/metal/boot/externals.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/ssh/ssh_config.h>
#include <pymergetic/metal/dev/stream/stream.h>
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/shell_cmd.h>

/* Dropbear (compiled with DROPBEAR_METAL) */
extern void svr_session_setup(int sock, int childpipe);
extern int  session_loop_once(void (*loophandler)(void));
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
extern int        metal_dropbear_jmp_ready;

/* From Dropbear runopts — declare minimal. */
struct runopts; /* opaque */
extern void svr_getopts(int argc, char **argv);

#define SSH_SESS_MAX 2u
#define HOSTKEY_PATH "/etc/ssh/dropbear_ed25519_host_key"

typedef struct {
  int32_t                used;
  int                    sock_fd;
  pm_metal_net_ip_sock_h sock;
  pm_metal_stream_h      pty_m;
  pm_metal_stream_h      pty_s;
  int                    pty_m_fd;
  int                    pty_s_fd;
  int32_t                shell_on; /* COM1 mirror + RX inject (same console as UART/UI) */
} ssh_sess_t;

static ssh_sess_t g_sess[SSH_SESS_MAX];
static int32_t    g_db_inited;
static uint32_t   g_active_sess;
static uint32_t   g_mirror_sess; /* sess that owns pm_metal_console_set_mirror */

static void ssh_console_mirror(const void *ptr, uint32_t len, void *ctx)
{
  pm_metal_stream_h pty_s;

  pty_s = (pm_metal_stream_h)(uintptr_t)ctx;
  if (pty_s == PM_METAL_STREAM_INVALID || ptr == NULL || len == 0u) {
    return;
  }
  /* Slave write → master ring → Dropbear channel → SSH client. */
  (void)pm_metal_stream_write(pty_s, ptr, len);
}

static void ssh_console_mirror_clear(uint32_t sess)
{
  if (g_mirror_sess == sess) {
    pm_metal_console_set_mirror(NULL, NULL);
    g_mirror_sess = 0;
  }
}

static void db_once_init(void)
{
  /* -i inetd, -R delay/generate hostkeys (no pre-seeded key file). */
  char *argv[] = { (char *)"dropbear", (char *)"-i", (char *)"-R", NULL };

  if (g_db_inited) {
    return;
  }
  /* Not going through svr-main — install server log/exit hooks ourselves. */
  _dropbear_exit = svr_dropbear_exit;
  _dropbear_log  = svr_dropbear_log;
  crypto_init();
  seedrandom();
  svr_getopts(3, argv);
  load_all_hostkeys();
  g_db_inited = 1;
}

int32_t metal_dropbear_ensure_hostkeys(void)
{
  pm_metal_sshd_cfg_t *cfg;
  const char          *path;
  uint32_t             sz;

  (void)pm_metal_fs_mkdir("/etc");
  (void)pm_metal_fs_mkdir("/etc/ssh");
  cfg  = pm_metal_net_ssh_cfg();
  path = (cfg != NULL && cfg->host_key[0] != '\0') ? cfg->host_key : HOSTKEY_PATH;
  sz   = pm_metal_fs_size(path);
  if (sz > 0u && sz != (uint32_t)-1) {
    pm_metal_logf("sshd: hostkey ready (%s)", path);
    return 0;
  }
  /* Generated on first KEX via dropbear -R (DELAY_HOSTKEY). Keep that off the
   * listen-coro stack — eager gensignkey reboot-looped the guest. */
  pm_metal_logf("sshd: hostkey will be generated on first KEX (%s)", path);
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
  pm_metal_sshd_cfg_t *cfg;

  cfg = pm_metal_net_ssh_cfg();
  return (cfg != NULL && cfg->auth_sslcert && cfg->client_ca[0] != '\0') ? 1 : 0;
}

int metal_dropbear_auth_password(const char *user, const char *pass)
{
  if (!metal_dropbear_auth_passwd_enabled() || user == NULL || pass == NULL) {
    return 0;
  }
  return pm_metal_auth_user_check(user, pass) ? 1 : 0;
}

int metal_dropbear_auth_pubkey(const char          *user,
                               const char          *keyalgo,
                               unsigned int         keyalgolen,
                               const unsigned char *keyblob,
                               unsigned int         keybloblen)
{
  char algo[PM_METAL_AUTH_ALGO_MAX];

  if (!metal_dropbear_auth_pubkey_enabled() || user == NULL || keyblob == NULL ||
      keybloblen == 0u) {
    return 0;
  }
  if (keyalgo != NULL && keyalgolen > 0u && keyalgolen < sizeof(algo)) {
    memcpy(algo, keyalgo, keyalgolen);
    algo[keyalgolen] = '\0';
    return pm_metal_auth_pubkey_check(user, algo, keyblob, keybloblen) ? 1 : 0;
  }
  return pm_metal_auth_pubkey_check(user, NULL, keyblob, keybloblen) ? 1 : 0;
}

int metal_dropbear_auth_sslcert(const char          *requested_user,
                                const unsigned char *cert_der,
                                unsigned int         cert_len,
                                const unsigned char *signed_data,
                                unsigned int         signed_len,
                                const unsigned char *signature,
                                unsigned int         signature_len)
{
  pm_metal_sshd_cfg_t *cfg;
  char                 mapped_user[PM_METAL_AUTH_USER_MAX];

  if (!metal_dropbear_auth_sslcert_enabled() || requested_user == NULL || cert_der == NULL ||
      signed_data == NULL || signature == NULL) {
    return 0;
  }
  cfg = pm_metal_net_ssh_cfg();
  if (cfg == NULL ||
      !pm_metal_auth_sslcert_check(
        cert_der, cert_len, cfg->client_ca, mapped_user, sizeof(mapped_user)) ||
      strcmp(requested_user, mapped_user) != 0) {
    return 0;
  }
  return pm_metal_auth_sslcert_verify(
           cert_der, cert_len, signed_data, signed_len, signature, signature_len)
           ? 1
           : 0;
}

int metal_dropbear_pty_allocate(int *ptyfd, int *ttyfd, char *namebuf, int namebuflen)
{
  ssh_sess_t *s;

  if (g_active_sess == 0 || g_active_sess >= SSH_SESS_MAX) {
    return 0;
  }
  s = &g_sess[g_active_sess];
  if (s->pty_m_fd < 0 || s->pty_s_fd < 0) {
    return 0;
  }
  *ptyfd = s->pty_m_fd;
  *ttyfd = s->pty_s_fd;
  if (namebuf != NULL && namebuflen > 0) {
    strncpy(namebuf, "/dev/pts/metal", (size_t)namebuflen - 1u);
    namebuf[namebuflen - 1] = '\0';
  }
  return 1;
}

int metal_dropbear_spawn_command(void (*exec_fn)(const void *),
                                 const void *exec_data,
                                 int        *ret_writefd,
                                 int        *ret_readfd,
                                 int        *ret_errfd,
                                 int        *ret_pid)
{
  /* Unused — noptycommand uses metal_dropbear_run_cmd on Metal. */
  (void)exec_fn;
  (void)exec_data;
  (void)ret_writefd;
  (void)ret_readfd;
  (void)ret_errfd;
  (void)ret_pid;
  return -1;
}

int metal_dropbear_run_cmd(const char *cmd, int *writefd, int *readfd, int *errfd)
{
  int               outp[2];
  int               inp[2];
  pm_metal_stream_h cap_r;
  pm_metal_stream_h cap_w;
  pm_metal_stream_h old_in;
  pm_metal_stream_h old_out;
  pm_metal_stream_h old_err;
  uint8_t           buf[256];
  uint32_t          n;

  if (writefd == NULL || readfd == NULL) {
    return -1;
  }
  if (metal_db_pipe(outp) != 0 || metal_db_pipe(inp) != 0) {
    return -1;
  }
  if (pm_metal_stream_pipe(&cap_r, &cap_w) != 0) {
    metal_db_close(outp[0]);
    metal_db_close(outp[1]);
    metal_db_close(inp[0]);
    metal_db_close(inp[1]);
    return -1;
  }

  old_in  = pm_metal_stdio_in();
  old_out = pm_metal_stdio_out();
  old_err = pm_metal_stdio_err();
  (void)pm_metal_stdio_attach(cap_r, cap_w, cap_w);
  if (cmd != NULL && cmd[0] != '\0') {
    pm_metal_shell_cmd_dispatch(cmd);
  }
  (void)pm_metal_stdio_attach(old_in, old_out, old_err);

  for (;;) {
    n = pm_metal_stream_try_read(cap_r, buf, sizeof(buf));
    if (n == 0u) {
      break;
    }
    if (metal_db_write(outp[1], buf, n) < 0) {
      break;
    }
  }
  pm_metal_stream_close(cap_r);
  pm_metal_stream_close(cap_w);
  metal_db_close(outp[1]); /* parent readfd sees EOF */
  metal_db_close(inp[0]);

  *readfd  = outp[0];
  *writefd = inp[1];
  if (errfd != NULL) {
    *errfd = -1;
  }
  pm_metal_logf("sshd: ran cmd '%s'", cmd != NULL ? cmd : "");
  return 0;
}

int metal_dropbear_shell_attach(int slave_fd, int master_fd)
{
  ssh_sess_t *s;

  (void)master_fd;
  (void)slave_fd;
  if (g_active_sess == 0 || g_active_sess >= SSH_SESS_MAX) {
    return -1;
  }
  s           = &g_sess[g_active_sess];
  s->shell_on = 1;
  /* Same console as UART/UI: mirror COM1 TX, inject RX into console ring. */
  ssh_console_mirror_clear(g_mirror_sess);
  pm_metal_console_set_mirror(ssh_console_mirror, (void *)(uintptr_t)s->pty_s);
  g_mirror_sess = g_active_sess;
  pm_metal_shell_prompt_dirty();
  pm_metal_logf("sshd: shell attached (console viewport)");
  return 0;
}

static void shell_pump(ssh_sess_t *s)
{
  uint8_t  buf[64];
  uint32_t n;

  if (!s->shell_on) {
    return;
  }
  for (;;) {
    n = pm_metal_stream_try_read(s->pty_s, buf, sizeof(buf));
    if (n == 0u) {
      break;
    }
    (void)pm_metal_console_inject_rx(buf, n);
  }
}

uint32_t metal_dropbear_session_start(pm_metal_net_ip_sock_h sock,
                                      pm_metal_stream_h      pty_master,
                                      pm_metal_stream_h      pty_slave)
{
  uint32_t    id;
  ssh_sess_t *s;
  int         sfd;

  for (id = 1; id < SSH_SESS_MAX; id++) {
    if (!g_sess[id].used) {
      break;
    }
  }
  if (id >= SSH_SESS_MAX) {
    return 0;
  }

  s = &g_sess[id];
  memset(s, 0, sizeof(*s));
  s->used     = 1;
  s->sock     = sock;
  s->pty_m    = pty_master;
  s->pty_s    = pty_slave;
  s->pty_m_fd = -1;
  s->pty_s_fd = -1;
  sfd         = metal_db_fd_register_sock(sock);
  s->pty_m_fd = metal_db_fd_register_stream(pty_master);
  s->pty_s_fd = metal_db_fd_register_stream(pty_slave);
  if (sfd < 0 || s->pty_m_fd < 0 || s->pty_s_fd < 0) {
    metal_dropbear_session_close(id);
    return 0;
  }
  s->sock_fd = sfd;

  /* Arm jmp before seedrandom/crypto so dropbear_exit cannot _exit-spin. */
  g_active_sess            = id;
  metal_dropbear_jmp_ready = 1;
  if (sigsetjmp(metal_dropbear_jmp, 1) != 0) {
    metal_dropbear_jmp_ready = 0;
    g_active_sess            = 0;
    metal_dropbear_session_close(id);
    return 0;
  }

  db_once_init();
  (void)metal_dropbear_ensure_hostkeys();
  svr_session_setup(sfd, -1);
  metal_dropbear_jmp_ready = 0;
  g_active_sess            = id;
  pm_metal_logf("sshd: session %u started fd=%d", (unsigned)id, sfd);
  return id;
}

int32_t metal_dropbear_session_poll(uint32_t sess)
{
  ssh_sess_t *s;
  int         jc;

  if (sess == 0 || sess >= SSH_SESS_MAX || !g_sess[sess].used) {
    return -1;
  }
  s                        = &g_sess[sess];
  g_active_sess            = sess;
  metal_dropbear_jmp_ready = 1;
  jc                       = sigsetjmp(metal_dropbear_jmp, 1);
  if (jc != 0) {
    metal_dropbear_jmp_ready = 0;
    g_active_sess            = 0;
    pm_metal_logf("sshd: session %u dropbear_exit", (unsigned)sess);
    return -1;
  }

  pm_metal_net_ip_poll();
  if (metal_db_fd_is_closed(s->sock_fd)) {
    metal_dropbear_jmp_ready = 0;
    g_active_sess            = 0;
    pm_metal_logf("sshd: session %u peer closed", (unsigned)sess);
    return -1;
  }
  shell_pump(s);
  if (session_loop_once(svr_chansess_checksignal) != 0) {
    metal_dropbear_jmp_ready = 0;
    g_active_sess            = 0;
    pm_metal_logf("sshd: session %u loop end", (unsigned)sess);
    return -1;
  }
  shell_pump(s);

  metal_dropbear_jmp_ready = 0;
  return 0;
}

void metal_dropbear_session_close(uint32_t sess)
{
  ssh_sess_t *s;

  if (sess == 0 || sess >= SSH_SESS_MAX || !g_sess[sess].used) {
    return;
  }
  s = &g_sess[sess];
  ssh_console_mirror_clear(sess);
  metal_dropbear_session_reclaim();
  if (s->sock_fd >= 0) {
    metal_db_fd_release(s->sock_fd);
  }
  if (s->pty_m_fd >= 0) {
    metal_db_fd_release(s->pty_m_fd);
  }
  if (s->pty_s_fd >= 0) {
    metal_db_fd_release(s->pty_s_fd);
  }
  memset(s, 0, sizeof(*s));
  if (g_active_sess == sess) {
    g_active_sess = 0;
  }
}

/* Hand-bumped with DROPBEAR_VERSION / scripts/setup.d/deps/dropbear.sh pin. */
PM_METAL_EXTERNAL(g_pm_metal_ext_dropbear,
                  dropbear,
                  "2024.85",
                  "https://github.com/mkj/dropbear",
                  "SSH server (sshd)");

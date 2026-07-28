/*
 * POSIX stubs for Dropbear on Metal. Compiled with dropbear_stubs -I.
 */
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <setjmp.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <netdb.h>
#include <dirent.h>
#include <libgen.h>

#include "dropbear_fd.h"

#include <pymergetic/metal/auth/auth.h>
#include <pymergetic/metal/dev/random/random.h>
#include <pymergetic/metal/dev/stream/stream.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/async/async.h>

/* From Dropbear Metal session glue (svr-session.c). */
extern int        metal_dropbear_jmp_ready;
extern sigjmp_buf metal_dropbear_jmp;

/* POSIX setjmp/longjmp — see dropbear_stubs/setjmp.h. */
__attribute__((noinline, returns_twice)) int setjmp(jmp_buf env)
{
#if defined(__x86_64__)
  __asm__ volatile("movq %%rbx, 0(%0)\n\t"
                   "movq %%rbp, 8(%0)\n\t"
                   "movq %%r12, 16(%0)\n\t"
                   "movq %%r13, 24(%0)\n\t"
                   "movq %%r14, 32(%0)\n\t"
                   "movq %%r15, 40(%0)\n\t"
                   "leaq 8(%%rsp), %%rdx\n\t"
                   "movq %%rdx, 48(%0)\n\t"
                   "movq (%%rsp), %%rdx\n\t"
                   "movq %%rdx, 56(%0)"
                   :
                   : "r"(env)
                   : "rdx", "memory");
#elif defined(__i386__)
  __asm__ volatile("movl %%ebx, 0(%0)\n\t"
                   "movl %%esi, 4(%0)\n\t"
                   "movl %%edi, 8(%0)\n\t"
                   "movl %%ebp, 12(%0)\n\t"
                   "leal 4(%%esp), %%edx\n\t"
                   "movl %%edx, 16(%0)\n\t"
                   "movl (%%esp), %%edx\n\t"
                   "movl %%edx, 20(%0)"
                   :
                   : "r"(env)
                   : "edx", "memory");
#endif
  return 0;
}

__attribute__((noinline, noreturn)) void longjmp(jmp_buf env, int val)
{
  if (val == 0) {
    val = 1;
  }
#if defined(__x86_64__)
  __asm__ volatile("movl %1, %%eax\n\t"
                   "movq 0(%0), %%rbx\n\t"
                   "movq 8(%0), %%rbp\n\t"
                   "movq 16(%0), %%r12\n\t"
                   "movq 24(%0), %%r13\n\t"
                   "movq 32(%0), %%r14\n\t"
                   "movq 40(%0), %%r15\n\t"
                   "movq 48(%0), %%rsp\n\t"
                   "jmpq *56(%0)"
                   :
                   : "r"(env), "r"(val)
                   : "memory");
#elif defined(__i386__)
  __asm__ volatile("movl %1, %%eax\n\t"
                   "movl 0(%0), %%ebx\n\t"
                   "movl 4(%0), %%esi\n\t"
                   "movl 8(%0), %%edi\n\t"
                   "movl 12(%0), %%ebp\n\t"
                   "movl 16(%0), %%esp\n\t"
                   "jmp *20(%0)"
                   :
                   : "r"(env), "r"(val)
                   : "memory");
#endif
  for (;;) {
  }
}

char **environ;

char *strdup(const char *s)
{
  size_t n;
  char  *p;

  if (s == NULL) {
    return NULL;
  }
  n = strlen(s) + 1u;
  p = (char *)malloc(n);
  if (p == NULL) {
    return NULL;
  }
  memcpy(p, s, n);
  return p;
}

int gettimeofday(struct timeval *tv, void *tz)
{
  uint64_t us;

  (void)tz;
  if (tv == NULL) {
    return -1;
  }
  us          = pm_metal_async_mono_us();
  tv->tv_sec  = (long)(us / 1000000ull);
  tv->tv_usec = (long)(us % 1000000ull);
  return 0;
}

pid_t getpid(void)
{
  return 1;
}
pid_t getppid(void)
{
  return 0;
}
uid_t getuid(void)
{
  return 0;
}
uid_t geteuid(void)
{
  return 0;
}
gid_t getgid(void)
{
  return 0;
}
gid_t getegid(void)
{
  return 0;
}
int setuid(uid_t uid)
{
  (void)uid;
  return 0;
}
int setgid(gid_t gid)
{
  (void)gid;
  return 0;
}
int seteuid(uid_t uid)
{
  (void)uid;
  return 0;
}
int setegid(gid_t gid)
{
  (void)gid;
  return 0;
}
int setreuid(uid_t r, uid_t e)
{
  (void)r;
  (void)e;
  return 0;
}
int setregid(gid_t r, gid_t e)
{
  (void)r;
  (void)e;
  return 0;
}
int initgroups(const char *user, gid_t group)
{
  (void)user;
  (void)group;
  return 0;
}
int getgroups(int size, gid_t list[])
{
  (void)size;
  (void)list;
  errno = ENOSYS;
  return -1;
}

unsigned int sleep(unsigned int seconds)
{
  (void)seconds;
  return 0;
}
int usleep(useconds_t usec)
{
  (void)usec;
  return 0;
}
int chdir(const char *path)
{
  (void)path;
  return 0;
}
char *getcwd(char *buf, size_t size)
{
  if (buf == NULL || size < 2) {
    return NULL;
  }
  buf[0] = '/';
  buf[1] = '\0';
  return buf;
}
int access(const char *pathname, int mode)
{
  (void)pathname;
  (void)mode;
  errno = ENOENT;
  return -1;
}
int isatty(int fd)
{
  (void)fd;
  return 1;
}
int daemon(int n, int m)
{
  (void)n;
  (void)m;
  return -1;
}
void _exit(int status)
{
  if (metal_dropbear_jmp_ready) {
    siglongjmp(metal_dropbear_jmp, status ? status : 1);
  }
  pm_metal_logf("sshd: fatal _exit(%d) with no session jmp", status);
  for (;;) {
  }
}
void exit(int status)
{
  _exit(status);
}

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags)
{
  uint32_t n;

  (void)flags;
  if (buf == NULL || buflen == 0u) {
    return 0;
  }
  n = pm_metal_random(buf, (uint32_t)buflen);
  if (n == 0u) {
    errno = EIO;
    return -1;
  }
  return (ssize_t)n;
}
pid_t fork(void)
{
  errno = ENOSYS;
  return -1;
}
pid_t vfork(void)
{
  return fork();
}
char *getenv(const char *name)
{
  (void)name;
  return NULL;
}
char *strerror(int errnum)
{
  (void)errnum;
  return "error";
}
int execv(const char *path, char *const argv[])
{
  (void)path;
  (void)argv;
  errno = ENOENT;
  return -1;
}
int execve(const char *path, char *const argv[], char *const envp[])
{
  (void)path;
  (void)argv;
  (void)envp;
  errno = ENOENT;
  return -1;
}
int dup2(int oldfd, int newfd)
{
  (void)oldfd;
  return newfd;
}
int open(const char *pathname, int flags, ...)
{
  if (pathname == NULL) {
    errno = EINVAL;
    return -1;
  }
  return metal_db_open_path(pathname, flags);
}
int fcntl(int fd, int cmd, ...)
{
  (void)fd;
  (void)cmd;
  return 0;
}
int fsync(int fd)
{
  (void)fd;
  return 0;
}
int link(const char *oldpath, const char *newpath)
{
  /* Force gensignkey.c non-atomic write fallback (EPERM/EACCES). */
  (void)oldpath;
  (void)newpath;
  errno = EPERM;
  return -1;
}
int unlink(const char *pathname)
{
  (void)pathname;
  errno = ENOENT;
  return -1;
}
int rename(const char *oldpath, const char *newpath)
{
  (void)oldpath;
  (void)newpath;
  errno = ENOSYS;
  return -1;
}

int stat(const char *path, struct stat *buf)
{
  /* Synthetic PTY name from metal_dropbear_pty_allocate — not a VFS node.
   * Dropbear's pty_setowner() stats this path after allocate; it must succeed
   * with uid/gid matching getpwnam() (0/0) so chown is skipped. */
  if (path != NULL && buf != NULL && strcmp(path, "/dev/pts/metal") == 0) {
    memset(buf, 0, sizeof(*buf));
    buf->st_mode = (mode_t)(S_IFCHR | 0620);
    buf->st_uid  = 0;
    buf->st_gid  = 0;
    return 0;
  }
  errno = ENOENT;
  return -1;
}
int fstat(int fd, struct stat *buf)
{
  (void)fd;
  (void)buf;
  errno = EBADF;
  return -1;
}
int lstat(const char *path, struct stat *buf)
{
  return stat(path, buf);
}
int chmod(const char *path, mode_t mode)
{
  (void)path;
  (void)mode;
  return 0;
}
int mkdir(const char *path, mode_t mode)
{
  (void)path;
  (void)mode;
  return -1;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
  (void)pid;
  (void)options;
  if (status) {
    *status = 0;
  }
  errno = ECHILD;
  return -1;
}

int getrlimit(int resource, struct rlimit *rlim)
{
  (void)resource;
  if (rlim) {
    rlim->rlim_cur = 256;
    rlim->rlim_max = 256;
  }
  return 0;
}
int setrlimit(int resource, const struct rlimit *rlim)
{
  (void)resource;
  (void)rlim;
  return 0;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
  ssize_t total;
  int     i;

  if (iov == NULL || iovcnt < 0) {
    errno = EINVAL;
    return -1;
  }
  total = 0;
  for (i = 0; i < iovcnt; i++) {
    ssize_t n;

    if (iov[i].iov_base == NULL && iov[i].iov_len != 0u) {
      errno = EINVAL;
      return -1;
    }
    if (iov[i].iov_len == 0u) {
      continue;
    }
    n = metal_db_write(fd, iov[i].iov_base, iov[i].iov_len);
    if (n < 0) {
      return (total > 0) ? total : -1;
    }
    total += n;
    if ((size_t)n < iov[i].iov_len) {
      break;
    }
  }
  return total;
}
ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
  ssize_t total;
  int     i;

  if (iov == NULL || iovcnt < 0) {
    errno = EINVAL;
    return -1;
  }
  total = 0;
  for (i = 0; i < iovcnt; i++) {
    ssize_t n;

    if (iov[i].iov_base == NULL && iov[i].iov_len != 0u) {
      errno = EINVAL;
      return -1;
    }
    if (iov[i].iov_len == 0u) {
      continue;
    }
    n = metal_db_read(fd, iov[i].iov_base, iov[i].iov_len);
    if (n < 0) {
      return (total > 0) ? total : -1;
    }
    if (n == 0) {
      break;
    }
    total += n;
    if ((size_t)n < iov[i].iov_len) {
      break;
    }
  }
  return total;
}

int socket(int d, int t, int p)
{
  (void)d;
  (void)t;
  (void)p;
  errno = ENOSYS;
  return -1;
}
int bind(int s, const struct sockaddr *a, socklen_t n)
{
  (void)s;
  (void)a;
  (void)n;
  return -1;
}
int listen(int s, int b)
{
  (void)s;
  (void)b;
  return -1;
}
int accept(int s, struct sockaddr *a, socklen_t *n)
{
  (void)s;
  (void)a;
  (void)n;
  return -1;
}
int connect(int s, const struct sockaddr *a, socklen_t n)
{
  (void)s;
  (void)a;
  (void)n;
  return -1;
}
int shutdown(int s, int how)
{
  (void)s;
  (void)how;
  return 0;
}
int setsockopt(int s, int l, int o, const void *v, socklen_t n)
{
  (void)s;
  (void)l;
  (void)o;
  (void)v;
  (void)n;
  return 0;
}
int getsockopt(int s, int l, int o, void *v, socklen_t *n)
{
  (void)s;
  (void)l;
  (void)o;
  (void)v;
  (void)n;
  return 0;
}
int getsockname(int s, struct sockaddr *a, socklen_t *n)
{
  struct sockaddr_in *in;

  (void)s;
  if (a == NULL || n == NULL || *n < sizeof(struct sockaddr_in)) {
    return -1;
  }
  in = (struct sockaddr_in *)a;
  memset(in, 0, sizeof(*in));
  in->sin_family = AF_INET;
  *n             = sizeof(*in);
  return 0;
}
int getpeername(int s, struct sockaddr *a, socklen_t *n)
{
  return getsockname(s, a, n);
}
ssize_t send(int s, const void *b, size_t l, int f)
{
  (void)s;
  (void)b;
  (void)l;
  (void)f;
  return -1;
}
ssize_t recv(int s, void *b, size_t l, int f)
{
  (void)s;
  (void)b;
  (void)l;
  (void)f;
  return -1;
}
ssize_t sendto(int s, const void *b, size_t l, int f, const struct sockaddr *d, socklen_t n)
{
  (void)s;
  (void)b;
  (void)l;
  (void)f;
  (void)d;
  (void)n;
  return -1;
}
ssize_t recvfrom(int s, void *b, size_t l, int f, struct sockaddr *src, socklen_t *n)
{
  (void)s;
  (void)b;
  (void)l;
  (void)f;
  (void)src;
  (void)n;
  return -1;
}

sighandler_t signal(int signum, sighandler_t handler)
{
  (void)signum;
  return handler;
}
int kill(pid_t pid, int sig)
{
  (void)pid;
  (void)sig;
  return 0;
}
unsigned int alarm(unsigned int seconds)
{
  (void)seconds;
  return 0;
}
int sigaction(int s, const struct sigaction *a, struct sigaction *o)
{
  (void)s;
  (void)a;
  (void)o;
  return 0;
}
int sigemptyset(sigset_t *set)
{
  if (set) {
    *set = 0;
  }
  return 0;
}
int sigaddset(sigset_t *set, int signum)
{
  (void)set;
  (void)signum;
  return 0;
}
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
  (void)how;
  (void)set;
  (void)oldset;
  return 0;
}

static void metal_tio_from_stream(struct termios *t, const pm_metal_stream_termios_t *s)
{
  uint32_t i;

  memset(t, 0, sizeof(*t));
  t->c_iflag  = (tcflag_t)s->iflag;
  t->c_oflag  = (tcflag_t)s->oflag;
  t->c_cflag  = (tcflag_t)s->cflag;
  t->c_lflag  = (tcflag_t)s->lflag;
  t->c_ispeed = (speed_t)s->ispeed;
  t->c_ospeed = (speed_t)s->ospeed;
  for (i = 0; i < NCCS && i < PM_METAL_STREAM_NCCS; i++) {
    t->c_cc[i] = (cc_t)s->cc[i];
  }
}

static void metal_tio_to_stream(pm_metal_stream_termios_t *s, const struct termios *t)
{
  uint32_t i;

  memset(s, 0, sizeof(*s));
  s->iflag  = (uint32_t)t->c_iflag;
  s->oflag  = (uint32_t)t->c_oflag;
  s->cflag  = (uint32_t)t->c_cflag;
  s->lflag  = (uint32_t)t->c_lflag;
  s->ispeed = (uint32_t)t->c_ispeed;
  s->ospeed = (uint32_t)t->c_ospeed;
  for (i = 0; i < NCCS && i < PM_METAL_STREAM_NCCS; i++) {
    s->cc[i] = (uint8_t)t->c_cc[i];
  }
}

int tcgetattr(int fd, struct termios *t)
{
  pm_metal_stream_h         sh;
  pm_metal_stream_termios_t st;

  if (t == NULL) {
    errno = EINVAL;
    return -1;
  }

  sh = metal_db_fd_stream(fd);
  if (sh == PM_METAL_STREAM_INVALID || pm_metal_stream_termios_get(sh, &st) != 0) {
    errno = ENOTTY;
    return -1;
  }

  metal_tio_from_stream(t, &st);
  return 0;
}

int tcsetattr(int fd, int act, const struct termios *t)
{
  pm_metal_stream_h         sh;
  pm_metal_stream_termios_t st;

  (void)act;
  if (t == NULL) {
    errno = EINVAL;
    return -1;
  }

  sh = metal_db_fd_stream(fd);
  if (sh == PM_METAL_STREAM_INVALID) {
    errno = ENOTTY;
    return -1;
  }

  metal_tio_to_stream(&st, t);
  if (pm_metal_stream_termios_set(sh, &st) != 0) {
    errno = ENOTTY;
    return -1;
  }

  return 0;
}

speed_t cfgetispeed(const struct termios *t)
{
  return (t != NULL) ? t->c_ispeed : 0;
}

speed_t cfgetospeed(const struct termios *t)
{
  return (t != NULL) ? t->c_ospeed : 0;
}

int cfsetispeed(struct termios *t, speed_t speed)
{
  if (t == NULL) {
    errno = EINVAL;
    return -1;
  }

  t->c_ispeed = speed;
  return 0;
}

int cfsetospeed(struct termios *t, speed_t speed)
{
  if (t == NULL) {
    errno = EINVAL;
    return -1;
  }

  t->c_ospeed = speed;
  return 0;
}

int ioctl(int fd, unsigned long request, ...)
{
  va_list                   ap;
  pm_metal_stream_h         sh;
  pm_metal_stream_winsize_t mw;
  struct winsize           *ws;
  int                      *ip;

  sh = metal_db_fd_stream(fd);
  va_start(ap, request);
  if (request == TIOCGWINSZ) {
    ws = va_arg(ap, struct winsize *);
    va_end(ap);
    if (ws == NULL || sh == PM_METAL_STREAM_INVALID || pm_metal_stream_winsize_get(sh, &mw) != 0) {
      errno = ENOTTY;
      return -1;
    }

    memset(ws, 0, sizeof(*ws));
    ws->ws_row    = mw.row;
    ws->ws_col    = mw.col;
    ws->ws_xpixel = mw.xpixel;
    ws->ws_ypixel = mw.ypixel;
    return 0;
  }

  if (request == TIOCSWINSZ) {
    ws = va_arg(ap, struct winsize *);
    va_end(ap);
    if (ws == NULL || sh == PM_METAL_STREAM_INVALID) {
      errno = ENOTTY;
      return -1;
    }

    memset(&mw, 0, sizeof(mw));
    mw.row    = ws->ws_row;
    mw.col    = ws->ws_col;
    mw.xpixel = ws->ws_xpixel;
    mw.ypixel = ws->ws_ypixel;
    if (pm_metal_stream_winsize_set(sh, &mw) != 0) {
      errno = ENOTTY;
      return -1;
    }

    return 0;
  }

  if (request == TIOCSCTTY) {
    va_end(ap);
    return (sh != PM_METAL_STREAM_INVALID) ? 0 : -1;
  }

  if (request == FIONREAD) {
    ip = va_arg(ap, int *);
    va_end(ap);
    if (ip == NULL) {
      errno = EINVAL;
      return -1;
    }

    *ip = (sh != PM_METAL_STREAM_INVALID) ? (int)pm_metal_stream_pending(sh) : 0;
    return 0;
  }

  va_end(ap);
  errno = ENOTTY;
  return -1;
}

void openlog(const char *ident, int option, int facility)
{
  (void)ident;
  (void)option;
  (void)facility;
}
void closelog(void) {}
void syslog(int priority, const char *format, ...)
{
  char    buf[256];
  va_list ap;

  (void)priority;
  va_start(ap, format);
  vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);
  pm_metal_logf("sshd: %s", buf);
}

int getaddrinfo(const char            *node,
                const char            *service,
                const struct addrinfo *hints,
                struct addrinfo      **res)
{
  (void)node;
  (void)service;
  (void)hints;
  if (res) {
    *res = NULL;
  }
  return EAI_NONAME;
}
void freeaddrinfo(struct addrinfo *res)
{
  (void)res;
}
int getnameinfo(const struct sockaddr *sa,
                socklen_t              salen,
                char                  *host,
                socklen_t              hostlen,
                char                  *serv,
                socklen_t              servlen,
                int                    flags)
{
  (void)sa;
  (void)salen;
  (void)flags;
  if (host && hostlen) {
    strncpy(host, "0.0.0.0", hostlen - 1);
    host[hostlen - 1] = '\0';
  }
  if (serv && servlen) {
    strncpy(serv, "22", servlen - 1);
    serv[servlen - 1] = '\0';
  }
  return 0;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
  const uint8_t *b;

  if (dst == NULL || size < 8 || src == NULL) {
    return NULL;
  }
  if (af == AF_INET) {
    b = (const uint8_t *)src;
    snprintf(
      dst, size, "%u.%u.%u.%u", (unsigned)b[0], (unsigned)b[1], (unsigned)b[2], (unsigned)b[3]);
    return dst;
  }
  strncpy(dst, "::", size - 1);
  return dst;
}
int inet_pton(int af, const char *src, void *dst)
{
  (void)af;
  (void)src;
  (void)dst;
  return 0;
}
int inet_aton(const char *cp, struct in_addr *inp)
{
  (void)cp;
  if (inp) {
    inp->s_addr = 0;
  }
  return 1;
}
char *inet_ntoa(struct in_addr in)
{
  static char    buf[16];
  const uint8_t *b = (const uint8_t *)&in.s_addr;

  snprintf(buf,
           sizeof(buf),
           "%u.%u.%u.%u",
           (unsigned)b[0],
           (unsigned)b[1],
           (unsigned)b[2],
           (unsigned)b[3]);
  return buf;
}

DIR *opendir(const char *name)
{
  (void)name;
  return NULL;
}
struct dirent *readdir(DIR *dirp)
{
  (void)dirp;
  return NULL;
}
int closedir(DIR *dirp)
{
  (void)dirp;
  return 0;
}

char *basename(char *path)
{
  char *s;

  if (path == NULL || path[0] == '\0') {
    return (char *)".";
  }
  s = strrchr(path, '/');
  return (s != NULL) ? (s + 1) : path;
}
char *dirname(char *path)
{
  (void)path;
  return (char *)"/";
}

static struct passwd g_pw;
static char          g_pw_name[32];
static char          g_pw_dir[]    = "/";
static char          g_pw_shell[]  = "/bin/sh";
static char          g_pw_passwd[] = "x";

struct passwd *getpwnam(const char *name)
{
  if (name == NULL || name[0] == '\0') {
    return NULL;
  }
  strncpy(g_pw_name, name, sizeof(g_pw_name) - 1u);
  g_pw_name[sizeof(g_pw_name) - 1u] = '\0';
  g_pw.pw_name                      = g_pw_name;
  g_pw.pw_passwd                    = g_pw_passwd;
  g_pw.pw_uid                       = 0;
  g_pw.pw_gid                       = 0;
  g_pw.pw_gecos                     = g_pw_name;
  g_pw.pw_dir                       = g_pw_dir;
  g_pw.pw_shell                     = g_pw_shell;
  (void)pm_metal_auth_user_check;
  return &g_pw;
}
struct passwd *getpwuid(uid_t uid)
{
  (void)uid;
  return getpwnam("root");
}
struct group *getgrnam(const char *name)
{
  (void)name;
  return NULL;
}
struct group *getgrgid(gid_t gid)
{
  (void)gid;
  return NULL;
}

void explicit_bzero(void *s, size_t n)
{
  volatile unsigned char *p = (volatile unsigned char *)s;
  while (n--) {
    *p++ = 0;
  }
}

clock_t clock(void)
{
  return 0;
}
time_t time(time_t *t)
{
  time_t now = (time_t)(pm_metal_async_mono_us() / 1000000ull);
  if (t) {
    *t = now;
  }
  return now;
}

int fgetc(FILE *f)
{
  (void)f;
  return EOF;
}
int getc(FILE *f)
{
  return fgetc(f);
}
int fputc(int c, FILE *f)
{
  (void)f;
  return c;
}
int putchar(int c)
{
  return fputc(c, stdout);
}
char *fgets(char *s, int size, FILE *f)
{
  (void)f;
  (void)size;
  if (s) {
    s[0] = '\0';
  }
  return NULL;
}
int fileno(FILE *f)
{
  (void)f;
  return -1;
}
void clearerr(FILE *f)
{
  (void)f;
}
int feof(FILE *f)
{
  (void)f;
  return 1;
}
int ferror(FILE *f)
{
  (void)f;
  return 0;
}

static int g_usershell_i;

void setusershell(void)
{
  g_usershell_i = 0;
}

char *getusershell(void)
{
  static char sh[] = "/bin/sh";

  if (g_usershell_i == 0) {
    g_usershell_i = 1;
    return sh;
  }
  return NULL;
}

void endusershell(void)
{
  g_usershell_i = 0;
}

int openpty(
  int *amaster, int *aslave, char *name, const struct termios *termp, const struct winsize *winp)
{
  extern int metal_dropbear_pty_allocate(int *ptyfd, int *ttyfd, char *namebuf, int namebuflen);
  pm_metal_stream_h         sh;
  pm_metal_stream_termios_t st;
  pm_metal_stream_winsize_t mw;

  if (amaster == NULL || aslave == NULL) {
    return -1;
  }

  if (metal_dropbear_pty_allocate(amaster, aslave, name, name ? 64 : 0) != 0) {
    return -1;
  }

  sh = metal_db_fd_stream(*aslave);
  if (sh == PM_METAL_STREAM_INVALID) {
    sh = metal_db_fd_stream(*amaster);
  }

  if (termp != NULL && sh != PM_METAL_STREAM_INVALID) {
    metal_tio_to_stream(&st, termp);
    (void)pm_metal_stream_termios_set(sh, &st);
  }

  if (winp != NULL && sh != PM_METAL_STREAM_INVALID) {
    memset(&mw, 0, sizeof(mw));
    mw.row    = winp->ws_row;
    mw.col    = winp->ws_col;
    mw.xpixel = winp->ws_xpixel;
    mw.ypixel = winp->ws_ypixel;
    (void)pm_metal_stream_winsize_set(sh, &mw);
  }

  return 0;
}
pid_t forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp)
{
  (void)amaster;
  (void)name;
  (void)termp;
  (void)winp;
  errno = ENOSYS;
  return -1;
}

int chown(const char *path, uid_t owner, gid_t group)
{
  (void)path;
  (void)owner;
  (void)group;
  return 0;
}
int fchown(int fd, uid_t owner, gid_t group)
{
  (void)fd;
  (void)owner;
  (void)group;
  return 0;
}
pid_t setsid(void)
{
  return 1;
}
pid_t getsid(pid_t pid)
{
  (void)pid;
  return 1;
}
int setpgid(pid_t pid, pid_t pgid)
{
  (void)pid;
  (void)pgid;
  return 0;
}
pid_t getpgid(pid_t pid)
{
  (void)pid;
  return 1;
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
  (void)format;
  (void)tm;
  if (s == NULL || max == 0) {
    return 0;
  }
  s[0] = '\0';
  return 0;
}

int clearenv(void)
{
  return 0;
}

size_t strlcpy(char *dst, const char *src, size_t siz)
{
  size_t n;
  size_t i;

  if (dst == NULL || siz == 0) {
    return src ? strlen(src) : 0;
  }
  n = src ? strlen(src) : 0;
  for (i = 0; i + 1u < siz && i < n; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
  return n;
}
size_t strlcat(char *dst, const char *src, size_t siz)
{
  size_t dlen;

  if (dst == NULL || siz == 0) {
    return src ? strlen(src) : 0;
  }
  dlen = strlen(dst);
  if (dlen >= siz) {
    return dlen + (src ? strlen(src) : 0);
  }
  return dlen + strlcpy(dst + dlen, src, siz - dlen);
}
struct tm *localtime(const time_t *timer)
{
  static struct tm t;
  (void)timer;
  memset(&t, 0, sizeof(t));
  return &t;
}
struct tm *gmtime(const time_t *timer)
{
  return localtime(timer);
}
int putenv(char *string)
{
  (void)string;
  return 0;
}
int unsetenv(const char *name)
{
  (void)name;
  return 0;
}

/*
 * Dropbear BSD I/O -> Metal net/stream (host-only).
 */
#include "dropbear_fd.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include <pymergetic/metal/net/ip/tcp/__init__.h>
#include <pymergetic/metal/net/ip/__init__.h>
#include <pymergetic/metal/log/__init__.h>
#include <pymergetic/metal/mem/__init__.h>

#define METAL_DB_NO_IO_MACROS 1
#include "dropbear_metal/dropbear_metal_io.h"

/* Match Dropbear fcntl.h stubs (octal). */
#ifndef O_RDONLY
#define O_RDONLY 0
#endif
#ifndef O_WRONLY
#define O_WRONLY 1
#endif
#ifndef O_RDWR
#define O_RDWR 2
#endif
#ifndef O_CREAT
#define O_CREAT 0100
#endif
#ifndef O_EXCL
#define O_EXCL 0200
#endif
#ifndef O_TRUNC
#define O_TRUNC 01000
#endif

#define DB_FILE_MAX 2048u
#define DB_PATH_MAX 160u

typedef enum {
  DB_FD_NONE = 0,
  DB_FD_SOCK,
  DB_FD_STREAM,
  DB_FD_PIPE_R,
  DB_FD_PIPE_W,
  DB_FD_FILE
} db_fd_kind_t;

typedef struct {
  db_fd_kind_t           kind;
  uint32_t                stream_h;
  pm_metal_stream_h      stream;
  int32_t                peer; /* pipe peer fd */
  int32_t                closed;
  /* DB_FD_FILE */
  char    *path;
  uint8_t *data;
  uint32_t data_len;
  uint32_t data_cap;
  uint32_t pos;
  int32_t  writable;
  int32_t  dirty;
} db_fd_t;

static db_fd_t g_fds[METAL_DB_FD_MAX];

static int fd_alloc(db_fd_kind_t kind)
{
  int32_t i;

  for (i = 3; i < METAL_DB_FD_MAX; i++) {
    if (g_fds[i].kind == DB_FD_NONE) {
      memset(&g_fds[i], 0, sizeof(g_fds[i]));
      g_fds[i].kind = kind;
      return (int)i;
    }
  }
  errno = EMFILE;
  return -1;
}

int metal_db_fd_register_tcp(uint32_t stream_h)
{
  int fd;

  if (stream_h == 0u) {
    return -1;
  }
  fd = fd_alloc(DB_FD_SOCK);
  if (fd < 0) {
    return -1;
  }
  g_fds[fd].stream_h = stream_h;
  return fd;
}

int metal_db_fd_register_stream(pm_metal_stream_h stream)
{
  int fd;

  if (stream == PM_METAL_STREAM_INVALID) {
    return -1;
  }
  fd = fd_alloc(DB_FD_STREAM);
  if (fd < 0) {
    return -1;
  }
  g_fds[fd].stream = stream;
  return fd;
}

void metal_db_fd_release(int fd)
{
  if (fd < 0 || fd >= METAL_DB_FD_MAX) {
    return;
  }
  if (g_fds[fd].kind == DB_FD_FILE) {
    if (g_fds[fd].path != NULL) {
      pm_metal_mem_free((uint8_t *)g_fds[fd].path);
    }
    if (g_fds[fd].data != NULL) {
      pm_metal_mem_free(g_fds[fd].data);
    }
  }
  memset(&g_fds[fd], 0, sizeof(g_fds[fd]));
}

void metal_db_fd_release_all(void)
{
  int32_t i;

  for (i = 0; i < METAL_DB_FD_MAX; i++) {
    metal_db_fd_release((int)i);
  }
}

int metal_db_open_path(const char *path, int flags)
{
  int      fd;
  uint32_t sz;
  size_t   plen;
  char    *pcopy;
  uint8_t *buf;
  int32_t  writing;

  if (path == NULL || path[0] == '\0') {
    errno = EINVAL;
    return -1;
  }
  plen = strlen(path);
  if (plen == 0u || plen >= DB_PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }

  writing =
    ((flags & O_WRONLY) == O_WRONLY) || ((flags & O_RDWR) == O_RDWR) || ((flags & O_CREAT) != 0);
  /* W8: no sync fs — hostkeys live in RAM until flushed (DELAY_HOSTKEY). */
  sz = 0u;

  if (!writing) {
    if (sz == 0u) {
      errno = ENOENT;
      return -1;
    }
  } else if ((flags & O_CREAT) != 0 && (flags & O_EXCL) != 0 && sz > 0u) {
    errno = EEXIST;
    return -1;
  }

  if (sz > DB_FILE_MAX) {
    errno = EFBIG;
    return -1;
  }

  fd = fd_alloc(DB_FD_FILE);
  if (fd < 0) {
    return -1;
  }

  pcopy = (char *)pm_metal_mem_alloc((size_t)(plen + 1u));
  buf   = (uint8_t *)pm_metal_mem_alloc((size_t)(DB_FILE_MAX));
  if (pcopy == NULL || buf == NULL) {
    if (pcopy != NULL) {
      pm_metal_mem_free((uint8_t *)pcopy);
    }
    if (buf != NULL) {
      pm_metal_mem_free(buf);
    }
    metal_db_fd_release(fd);
    errno = ENOMEM;
    return -1;
  }
  memcpy(pcopy, path, plen + 1u);
  memset(buf, 0, DB_FILE_MAX);
  g_fds[fd].data_len = 0;

  g_fds[fd].path = pcopy;
  g_fds[fd].data = buf;
  g_fds[fd].data_cap = DB_FILE_MAX;
  g_fds[fd].pos = 0;
  g_fds[fd].writable = writing ? 1 : 0;
  g_fds[fd].dirty = 0;
  return fd;
}

uint32_t metal_db_fd_tcp(int fd)
{
  if (fd < 0 || fd >= METAL_DB_FD_MAX || g_fds[fd].kind != DB_FD_SOCK) {
    return 0u;
  }
  return g_fds[fd].stream_h;
}

pm_metal_stream_h metal_db_fd_stream(int fd)
{
  if (fd < 0 || fd >= METAL_DB_FD_MAX) {
    return PM_METAL_STREAM_INVALID;
  }
  if (g_fds[fd].kind != DB_FD_STREAM && g_fds[fd].kind != DB_FD_PIPE_R &&
      g_fds[fd].kind != DB_FD_PIPE_W) {
    return PM_METAL_STREAM_INVALID;
  }
  return g_fds[fd].stream;
}

void metal_db_fds_zero(fd_set *s)
{
  if (s != NULL) {
    memset(s, 0, sizeof(*s));
  }
}

void metal_db_fds_set(int fd, fd_set *s)
{
  if (s == NULL || fd < 0 || fd >= FD_SETSIZE) {
    return;
  }
  s->bits[(unsigned)fd / (8u * sizeof(unsigned long))] |=
    1ul << ((unsigned)fd % (8u * sizeof(unsigned long)));
}

void metal_db_fds_clr(int fd, fd_set *s)
{
  if (s == NULL || fd < 0 || fd >= FD_SETSIZE) {
    return;
  }
  s->bits[(unsigned)fd / (8u * sizeof(unsigned long))] &=
    ~(1ul << ((unsigned)fd % (8u * sizeof(unsigned long))));
}

int metal_db_fds_isset(int fd, const fd_set *s)
{
  if (s == NULL || fd < 0 || fd >= FD_SETSIZE) {
    return 0;
  }
  return (s->bits[(unsigned)fd / (8u * sizeof(unsigned long))] &
          (1ul << ((unsigned)fd % (8u * sizeof(unsigned long))))) != 0;
}

/* Per-fd 1-byte pushback for select/read probe. */
static int32_t g_push_valid[METAL_DB_FD_MAX];
static uint8_t g_push_byte[METAL_DB_FD_MAX];

static int sock_has_data(int fd)
{
  uint8_t  tmp[1];
  uint32_t n;

  if (g_push_valid[fd]) {
    return 1;
  }
  n = pm_metal_net_ip_tcp_try_read(g_fds[fd].stream_h, tmp, 1);
  if (n == (uint32_t)-1) {
    g_fds[fd].closed = 1;
    return 1;
  }
  if (n == 0) {
    return 0;
  }
  g_push_byte[fd]  = tmp[0];
  g_push_valid[fd] = 1;
  return 1;
}

static int stream_has_data(int fd)
{
  uint8_t  tmp[1];
  uint32_t n;

  if (g_push_valid[fd]) {
    return 1;
  }
  n = pm_metal_stream_try_read(g_fds[fd].stream, tmp, 1);
  if (n == 0) {
    return 0;
  }
  g_push_byte[fd]  = tmp[0];
  g_push_valid[fd] = 1;
  return 1;
}

ssize_t metal_db_read(int fd, void *buf, size_t count)
{
  uint8_t *p;
  uint32_t n;
  uint32_t got;

  if (buf == NULL || count == 0) {
    return 0;
  }
  if (fd < 0 || fd >= METAL_DB_FD_MAX || g_fds[fd].kind == DB_FD_NONE) {
    errno = EBADF;
    return -1;
  }
  if (g_fds[fd].closed) {
    return 0;
  }

  p   = (uint8_t *)buf;
  got = 0;
  if (g_push_valid[fd]) {
    p[0]             = g_push_byte[fd];
    g_push_valid[fd] = 0;
    got              = 1;
    if (count == 1) {
      return 1;
    }
    p++;
    count--;
  }

  if (g_fds[fd].kind == DB_FD_SOCK) {
    n = pm_metal_net_ip_tcp_try_read(g_fds[fd].stream_h, p, (uint32_t)count);
    if (n == (uint32_t)-1) {
      g_fds[fd].closed = 1;
      return (got > 0) ? (int)got : 0;
    }
    if (n == 0 && got == 0) {
      errno = EAGAIN;
      return -1;
    }
    return (int)(got + n);
  }

  if (g_fds[fd].kind == DB_FD_STREAM || g_fds[fd].kind == DB_FD_PIPE_R) {
    n = pm_metal_stream_try_read(g_fds[fd].stream, p, (uint32_t)count);
    if (n == 0 && got == 0) {
      errno = EAGAIN;
      return -1;
    }
    return (int)(got + n);
  }

  if (g_fds[fd].kind == DB_FD_FILE) {
    uint32_t avail;
    uint32_t take;

    if (g_fds[fd].pos >= g_fds[fd].data_len) {
      return (int)got;
    }
    avail = g_fds[fd].data_len - g_fds[fd].pos;
    take  = (uint32_t)count;
    if (take > avail) {
      take = avail;
    }
    memcpy(p, g_fds[fd].data + g_fds[fd].pos, take);
    g_fds[fd].pos += take;
    return (int)(got + take);
  }

  errno = EBADF;
  return -1;
}

ssize_t metal_db_write(int fd, const void *buf, size_t count)
{
  uint32_t n;

  if (buf == NULL || count == 0) {
    return 0;
  }
  if (fd < 0 || fd >= METAL_DB_FD_MAX || g_fds[fd].kind == DB_FD_NONE || g_fds[fd].closed) {
    errno = EBADF;
    return -1;
  }

  if (g_fds[fd].kind == DB_FD_SOCK) {
    pm_metal_net_ip_poll();
    n = pm_metal_net_ip_tcp_try_write(g_fds[fd].stream_h, buf, (uint32_t)count);
    if (n == 0) {
      errno = EAGAIN;
      return -1;
    }
    return (int)n;
  }

  if (g_fds[fd].kind == DB_FD_STREAM || g_fds[fd].kind == DB_FD_PIPE_W) {
    n = pm_metal_stream_write(g_fds[fd].stream, buf, (uint32_t)count);
    if (n == 0) {
      errno = EAGAIN;
      return -1;
    }
    return (int)n;
  }

  if (g_fds[fd].kind == DB_FD_FILE) {
    uint32_t take;
    uint32_t end;

    if (!g_fds[fd].writable || g_fds[fd].data == NULL) {
      errno = EBADF;
      return -1;
    }
    take = (uint32_t)count;
    if (g_fds[fd].pos >= g_fds[fd].data_cap) {
      errno = EFBIG;
      return -1;
    }
    if (take > g_fds[fd].data_cap - g_fds[fd].pos) {
      take = g_fds[fd].data_cap - g_fds[fd].pos;
    }
    if (take == 0u) {
      errno = EFBIG;
      return -1;
    }
    memcpy(g_fds[fd].data + g_fds[fd].pos, buf, take);
    g_fds[fd].pos += take;
    end = g_fds[fd].pos;
    if (end > g_fds[fd].data_len) {
      g_fds[fd].data_len = end;
    }
    g_fds[fd].dirty = 1;
    return (int)take;
  }

  errno = EBADF;
  return -1;
}

int metal_db_fd_is_closed(int fd)
{
  if (fd < 0 || fd >= METAL_DB_FD_MAX || g_fds[fd].kind == DB_FD_NONE) {
    return 1;
  }
  return g_fds[fd].closed ? 1 : 0;
}

int metal_db_close(int fd)
{
  if (fd < 0 || fd >= METAL_DB_FD_MAX) {
    errno = EBADF;
    return -1;
  }
  g_push_valid[fd] = 0;
  if (g_fds[fd].kind == DB_FD_FILE || g_fds[fd].kind == DB_FD_PIPE_R ||
      g_fds[fd].kind == DB_FD_PIPE_W) {
    /* W8: hostkeys stay RAM/in-process (Dropbear -R); no sync fs flush. */
    metal_db_fd_release(fd);
    return 0;
  }
  /* Sock/stream lifetime owned by Metal ssh session — mark closed only. */
  if (g_fds[fd].kind != DB_FD_NONE) {
    g_fds[fd].closed = 1;
  }
  return 0;
}

int metal_db_pipe(int fds[2])
{
  pm_metal_stream_h r;
  pm_metal_stream_h w;
  int               fd_r;
  int               fd_w;

  if (fds == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (pm_metal_stream_pipe(&r, &w) != 0) {
    errno = EMFILE;
    return -1;
  }
  fd_r = fd_alloc(DB_FD_PIPE_R);
  fd_w = fd_alloc(DB_FD_PIPE_W);
  if (fd_r < 0 || fd_w < 0) {
    pm_metal_stream_close(r);
    pm_metal_stream_close(w);
    if (fd_r >= 0) {
      metal_db_fd_release(fd_r);
    }
    if (fd_w >= 0) {
      metal_db_fd_release(fd_w);
    }
    errno = EMFILE;
    return -1;
  }
  g_fds[fd_r].stream = r;
  g_fds[fd_w].stream = w;
  g_fds[fd_r].peer   = fd_w;
  g_fds[fd_w].peer   = fd_r;
  fds[0]             = fd_r;
  fds[1]             = fd_w;
  return 0;
}

int metal_db_select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tv)
{
  fd_set  in_r;
  fd_set  in_w;
  int32_t fd;
  int32_t nready;

  (void)efds;
  (void)tv;
  (void)nfds;

  pm_metal_net_ip_poll();

  if (rfds != NULL) {
    in_r = *rfds;
    metal_db_fds_zero(rfds);
  } else {
    metal_db_fds_zero(&in_r);
  }
  if (wfds != NULL) {
    in_w = *wfds;
    metal_db_fds_zero(wfds);
  } else {
    metal_db_fds_zero(&in_w);
  }

  nready = 0;
  for (fd = 0; fd < METAL_DB_FD_MAX && fd < FD_SETSIZE; fd++) {
    if (rfds != NULL && metal_db_fds_isset(fd, &in_r)) {
      int ok;

      ok = 0;
      if (g_fds[fd].kind == DB_FD_SOCK) {
        ok = sock_has_data(fd) || g_fds[fd].closed;
      } else if (g_fds[fd].kind == DB_FD_STREAM || g_fds[fd].kind == DB_FD_PIPE_R) {
        ok = stream_has_data(fd);
      } else if (g_fds[fd].kind != DB_FD_NONE) {
        ok = 1;
      }
      if (ok) {
        metal_db_fds_set(fd, rfds);
        nready++;
      }
    }
    if (wfds != NULL && metal_db_fds_isset(fd, &in_w)) {
      if (g_fds[fd].kind != DB_FD_NONE && !g_fds[fd].closed) {
        metal_db_fds_set(fd, wfds);
        nready++;
      }
    }
  }

  return (int)nready;
}

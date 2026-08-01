/* Metal Dropbear I/O — include from includes.h AFTER system headers. */
#ifndef DROPBEAR_METAL_IO_H_
#define DROPBEAR_METAL_IO_H_

#include <stddef.h>
#include "../dropbear_stubs/sys/select.h"
#include "../dropbear_stubs/sys/types.h"

ssize_t metal_db_read(int fd, void *buf, size_t count);
ssize_t metal_db_write(int fd, const void *buf, size_t count);
int     metal_db_close(int fd);
int     metal_db_select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tv);
int     metal_db_pipe(int fds[2]);
void    metal_db_fds_zero(fd_set *s);
void    metal_db_fds_set(int fd, fd_set *s);
void    metal_db_fds_clr(int fd, fd_set *s);
int     metal_db_fds_isset(int fd, const fd_set *s);

#ifndef METAL_DB_NO_IO_MACROS
#undef read
#undef write
#undef close
#undef select
#undef pipe
#undef FD_ZERO
#undef FD_SET
#undef FD_CLR
#undef FD_ISSET
#define read(fd, buf, count)      metal_db_read((fd), (buf), (count))
#define write(fd, buf, count)     metal_db_write((fd), (buf), (count))
#define close(fd)                 metal_db_close(fd)
#define select(nfds, r, w, e, tv) metal_db_select((nfds), (r), (w), (e), (tv))
#define pipe(fds)                 metal_db_pipe(fds)
#define FD_ZERO(s)                metal_db_fds_zero(s)
#define FD_SET(fd, s)             metal_db_fds_set((fd), (s))
#define FD_CLR(fd, s)             metal_db_fds_clr((fd), (s))
#define FD_ISSET(fd, s)           metal_db_fds_isset((fd), (s))
#endif

#endif

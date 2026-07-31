/*
 * Metal fd table for Dropbear (sock / stream / pipe rings).
 * Host-only; not a product ABI.
 */
#ifndef PYMERGETIC_METAL_DEV_NET_DROPBEAR_FD_H_
#define PYMERGETIC_METAL_DEV_NET_DROPBEAR_FD_H_

#include <stddef.h>
#include <stdint.h>
/* Relative so clangd / TUs without dropbear_stubs -I still resolve ssize_t. */
#include "dropbear_stubs/sys/types.h"

#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/dev/stream/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METAL_DB_FD_MAX 64

int metal_db_fd_register_sock(pm_metal_net_ip_sock_h sock);
int metal_db_fd_register_stream(pm_metal_stream_h stream);
/** Path-backed hostkey/etc file via pm_metal_fs_* (Dropbear open()). */
int  metal_db_open_path(const char *path, int flags);
void metal_db_fd_release(int fd);
void metal_db_fd_release_all(void);

pm_metal_net_ip_sock_h metal_db_fd_sock(int fd);
pm_metal_stream_h      metal_db_fd_stream(int fd);

/* Declared for Dropbear TU macros; also used from glue. */
ssize_t metal_db_read(int fd, void *buf, size_t count);
ssize_t metal_db_write(int fd, const void *buf, size_t count);
int     metal_db_close(int fd);
int     metal_db_fd_is_closed(int fd);
int     metal_db_pipe(int fds[2]);

#ifdef __cplusplus
}
#endif

#endif

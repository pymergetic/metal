/*
 * Helpers for file.c / shim to recognize metal socket handles.
 * Keep this free of WAMR platform_api_extension.h — that header conflicts
 * with wasi/platform.h's os_file_handle / os_dir_stream typedefs.
 */
#ifndef PM_METAL_WASI_SOCKET_H_
#define PM_METAL_WASI_SOCKET_H_

/* 1 if handle is a metal WASI socket slot, else 0 */
int pm_metal_wasi_socket_is_ours(int handle);
/* 1 tcp, 0 udp, -1 unknown / not ours */
int pm_metal_wasi_socket_is_tcp(int handle);
/* underlying zsock fd or -1 */
int pm_metal_wasi_socket_zfd(int handle);

/* Same contracts as WAMR os_socket_* (bh_socket_t == int here). */
int os_socket_close(int socket);
int os_socket_recv(int socket, void *buf, unsigned int len);
int os_socket_send(int socket, const void *buf, unsigned int len);

/*
 * Shim-facing wrappers (zsock_poll / zsock_ioctl are header static inlines —
 * posix.c cannot call them via a forward decl).
 * pollfds: same layout as struct pollfd / zsock_pollfd (fd, events, revents).
 */
int pm_metal_wasi_socket_poll(void *fds, int nfds, int timeout);
int pm_metal_wasi_socket_ioctl_fionread(int handle, int *avail);

#endif /* PM_METAL_WASI_SOCKET_H_ */

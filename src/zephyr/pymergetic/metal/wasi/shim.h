/*
 * Zephyr shims for WAMR WASI posix.c (file-only slice).
 * header-only — stubs/inlines; no separate .c.
 *
 * Force-included only for WAMR ssp posix.c / blocking_op.c (see
 * src/zephyr/CMakeLists.txt). Avoid <fcntl.h> / <zephyr/posix/poll.h>:
 * those pull kernel/net and redefine util.h KB/MB/GB/MIN (or need
 * NETWORKING). Do not pull in <time.h> / <zephyr/posix/posix_time.h>
 * here — locale_t / itimerspec conflicts under the Zephyr SDK.
 */
#ifndef PM_METAL_WASI_ZEPHYR_SHIM_H_
#define PM_METAL_WASI_ZEPHYR_SHIM_H_

#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned nfds_t;

struct pollfd {
	int fd;
	short events;
	short revents;
};

#ifndef POLLIN
#define POLLIN 0x0001
#endif
#ifndef POLLOUT
#define POLLOUT 0x0004
#endif
#ifndef POLLERR
#define POLLERR 0x0008
#endif
#ifndef POLLHUP
#define POLLHUP 0x0010
#endif
#ifndef POLLNVAL
#define POLLNVAL 0x0020
#endif

static inline uint16_t htons(uint16_t hostshort)
{
	return __builtin_bswap16(hostshort);
}

static inline uint32_t htonl(uint32_t hostlong)
{
	return __builtin_bswap32(hostlong);
}

#if defined(CONFIG_NET_SOCKETS)
#include <stdarg.h> /* ioctl(FIONREAD, int *) varargs below */

int pm_metal_wasi_socket_is_ours(int handle);
int pm_metal_wasi_socket_poll(void *fds, int nfds, int timeout);
int pm_metal_wasi_socket_ioctl_fionread(int handle, int *avail);

#ifndef PM_METAL_WASI_SHIM_POLL_MAX
#define PM_METAL_WASI_SHIM_POLL_MAX 16
#endif

static inline int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	if (fds == NULL && nfds > 0) {
		errno = EFAULT;
		return -1;
	}
	if (nfds > PM_METAL_WASI_SHIM_POLL_MAX) {
		errno = EINVAL;
		return -1;
	}
	/* socket.c filters/translates sockets, pipes, and other synthetic fds. */
	return pm_metal_wasi_socket_poll(fds, (int)nfds, timeout);
}

#else /* !CONFIG_NET_SOCKETS */

static inline int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	(void)fds;
	(void)nfds;
	(void)timeout;
	errno = ENOSYS;
	return -1;
}

#endif /* CONFIG_NET_SOCKETS */

#ifndef FIONREAD
#define FIONREAD 0x541B
#endif

static inline int ioctl(int fd, unsigned long request, ...)
{
#if defined(CONFIG_NET_SOCKETS)
	if (request == FIONREAD && pm_metal_wasi_socket_is_ours(fd)) {
		va_list ap;
		int *avail;

		va_start(ap, request);
		avail = va_arg(ap, int *);
		va_end(ap);
		return pm_metal_wasi_socket_ioctl_fionread(fd, avail);
	}
#else
	(void)fd;
	(void)request;
#endif
	errno = ENOSYS;
	return -1;
}

#endif /* PM_METAL_WASI_ZEPHYR_SHIM_H_ */

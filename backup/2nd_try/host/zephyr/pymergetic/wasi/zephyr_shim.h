/*
 * Zephyr shims for WAMR WASI posix.c (file-only slice).
 */
#ifndef PM_METAL_WASI_ZEPHYR_SHIM_H_
#define PM_METAL_WASI_ZEPHYR_SHIM_H_

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>

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

#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif

static inline uint16_t htons(uint16_t hostshort)
{
	return __builtin_bswap16(hostshort);
}

static inline uint32_t htonl(uint32_t hostlong)
{
	return __builtin_bswap32(hostlong);
}

static inline int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	(void)fds;
	(void)nfds;
	(void)timeout;
	return -1;
}

#ifndef FIONREAD
#define FIONREAD 0x541B
#endif

static inline int ioctl(int fd, unsigned long request, ...)
{
	(void)fd;
	(void)request;
	return -1;
}

#endif /* PM_METAL_WASI_ZEPHYR_SHIM_H_ */

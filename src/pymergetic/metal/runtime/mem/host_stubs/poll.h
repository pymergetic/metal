#ifndef _POLL_H
#define _POLL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long nfds_t;

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

int poll(struct pollfd *pfds, nfds_t nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif

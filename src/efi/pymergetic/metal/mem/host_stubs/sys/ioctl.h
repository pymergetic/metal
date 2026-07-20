#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#ifndef FIONREAD
#define FIONREAD 0x541B
#endif

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif

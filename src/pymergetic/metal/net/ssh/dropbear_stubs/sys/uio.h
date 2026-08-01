#ifndef _METAL_DB_SYS_UIO_H
#define _METAL_DB_SYS_UIO_H
#include "types.h"
#define UIO_MAXIOV 1024
struct iovec {
  void  *iov_base;
  size_t iov_len;
};
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
#endif

#ifndef _METAL_DB_SYS_SOCKET_H
#define _METAL_DB_SYS_SOCKET_H
#include "types.h"
#include <stdint.h>
#define AF_UNSPEC    0
#define AF_UNIX      1
#define AF_INET      2
#define AF_INET6     10
#define PF_UNSPEC    AF_UNSPEC
#define PF_UNIX      AF_UNIX
#define PF_INET      AF_INET
#define PF_INET6     AF_INET6
#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOL_SOCKET   1
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 9
#define SO_ERROR     4
#define SO_PRIORITY  12
#define IPPROTO_TCP  6
#define IPPROTO_IP   0
#define IPPROTO_IPV6 41
#define SHUT_RD      0
#define SHUT_WR      1
#define SHUT_RDWR    2
#define MSG_DONTWAIT 0x40
struct sockaddr {
  uint16_t sa_family;
  char     sa_data[14];
};
struct sockaddr_storage {
  uint16_t ss_family;
  char     __ss[126];
};
struct linger {
  int l_onoff;
  int l_linger;
};
int     socket(int domain, int type, int protocol);
int     bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int     listen(int sockfd, int backlog);
int     accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int     shutdown(int sockfd, int how);
int     setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int     getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int     getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int                    sockfd,
               const void            *buf,
               size_t                 len,
               int                    flags,
               const struct sockaddr *dest,
               socklen_t              addrlen);
ssize_t recvfrom(
  int sockfd, void *buf, size_t len, int flags, struct sockaddr *src, socklen_t *addrlen);
#endif

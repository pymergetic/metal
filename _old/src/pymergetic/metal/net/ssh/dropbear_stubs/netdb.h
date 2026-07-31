#ifndef _METAL_DB_NETDB_H
#define _METAL_DB_NETDB_H
#include "sys/socket.h"
#define AI_PASSIVE     0x01
#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define EAI_NONAME     -2
struct addrinfo {
  int              ai_flags, ai_family, ai_socktype, ai_protocol;
  socklen_t        ai_addrlen;
  struct sockaddr *ai_addr;
  char            *ai_canonname;
  struct addrinfo *ai_next;
};
int         getaddrinfo(const char            *node,
                        const char            *service,
                        const struct addrinfo *hints,
                        struct addrinfo      **res);
void        freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);
int         getnameinfo(const struct sockaddr *sa,
                        socklen_t              salen,
                        char                  *host,
                        socklen_t              hostlen,
                        char                  *serv,
                        socklen_t              servlen,
                        int                    flags);
#endif

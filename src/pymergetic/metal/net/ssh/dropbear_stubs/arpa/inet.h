#ifndef _ARPA_INET_H
#define _ARPA_INET_H
#ifndef _METAL_DB_ARPA_INET_H
#define _METAL_DB_ARPA_INET_H
#include "../netinet/in.h"
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int         inet_pton(int af, const char *src, void *dst);
int         inet_aton(const char *cp, struct in_addr *inp);
char       *inet_ntoa(struct in_addr in);
#endif
#endif

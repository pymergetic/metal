/* Freestanding stub — byte order (impl: runtime/mem/libc_wamr.c). */
#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t htons(uint16_t hostshort);
uint32_t htonl(uint32_t hostlong);
uint16_t ntohs(uint16_t netshort);
uint32_t ntohl(uint32_t netlong);

#ifdef __cplusplus
}
#endif

#endif /* _ARPA_INET_H */

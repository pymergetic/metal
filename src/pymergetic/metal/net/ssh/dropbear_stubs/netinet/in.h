#ifndef _METAL_DB_NETINET_IN_H
#define _METAL_DB_NETINET_IN_H
#include <stdint.h>
typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;
struct in_addr {
  uint32_t s_addr;
};
struct in6_addr {
  uint8_t s6_addr[16];
};
struct sockaddr_in {
  uint16_t       sin_family;
  uint16_t       sin_port;
  struct in_addr sin_addr;
  char           sin_zero[8];
};
struct sockaddr_in6 {
  uint16_t        sin6_family;
  uint16_t        sin6_port;
  uint32_t        sin6_flowinfo;
  struct in6_addr sin6_addr;
  uint32_t        sin6_scope_id;
};
#define INADDR_ANY      ((uint32_t)0)
#define INADDR_LOOPBACK ((uint32_t)0x7f000001u)
#define IN6ADDR_ANY_INIT \
  {                      \
    {                    \
      {                  \
        0                \
      }                  \
    }                    \
  }
/* Functions (not macros) — host_stubs/arpa/inet.h declares the same; macros
 * break those decls when both headers are visible under EFI -I ordering. */
uint16_t htons(uint16_t hostshort);
uint32_t htonl(uint32_t hostlong);
uint16_t ntohs(uint16_t netshort);
uint32_t ntohl(uint32_t netlong);
#endif

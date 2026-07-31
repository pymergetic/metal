#ifndef _METAL_DB_SYS_UN_H
#define _METAL_DB_SYS_UN_H
#include "socket.h"
struct sockaddr_un {
  uint16_t sun_family;
  char     sun_path[108];
};
#endif

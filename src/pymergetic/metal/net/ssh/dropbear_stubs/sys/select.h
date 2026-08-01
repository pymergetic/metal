/* Freestanding Dropbear sys/select — claim glibc guards. */
#ifndef _METAL_DB_SYS_SELECT_H
#define _METAL_DB_SYS_SELECT_H

#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H 1
#endif

#include "time.h"

#ifndef FD_SETSIZE
#define FD_SETSIZE 128
#endif

#ifndef _METAL_DB_FD_SET_DEFINED
#define _METAL_DB_FD_SET_DEFINED
typedef struct metal_db_fd_set {
  unsigned long bits[4];
} fd_set;
#endif

int select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tv);

#endif

#ifndef _METAL_DB_SYS_RANDOM_H
#define _METAL_DB_SYS_RANDOM_H

#include <stddef.h>
#include <stdint.h>
#include "../sys/types.h"

#define GRND_NONBLOCK 1
#define GRND_RANDOM 2

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);

#endif

#ifndef _METAL_DB_PTY_H
#define _METAL_DB_PTY_H
#include "termios.h"
#include "sys/types.h"
int openpty(int *amaster, int *aslave, char *name, const struct termios *termp,
            const struct winsize *winp);
pid_t forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp);
#endif

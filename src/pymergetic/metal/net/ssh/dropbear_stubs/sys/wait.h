#ifndef _METAL_DB_SYS_WAIT_H
#define _METAL_DB_SYS_WAIT_H
#include "types.h"
#define WNOHANG        1
#define WIFEXITED(s)   1
#define WEXITSTATUS(s) ((s) & 0xff)
#define WIFSIGNALED(s) 0
#define WTERMSIG(s)    0
pid_t waitpid(pid_t pid, int *status, int options);
#endif

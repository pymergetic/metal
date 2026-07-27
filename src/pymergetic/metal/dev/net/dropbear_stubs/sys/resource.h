#ifndef _METAL_DB_SYS_RESOURCE_H
#define _METAL_DB_SYS_RESOURCE_H
#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_NOFILE 7
#define RLIMIT_CORE 4
#define RLIMIT_AS 9
struct rlimit { unsigned long rlim_cur; unsigned long rlim_max; };
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);
#endif

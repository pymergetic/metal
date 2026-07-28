#ifndef _METAL_DB_FCNTL_H
#define _METAL_DB_FCNTL_H
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_CREAT    0100
#define O_EXCL     0200
#define O_NOCTTY   0400
#define O_TRUNC    01000
#define O_APPEND   02000
#define O_NONBLOCK 04000
#define S_IRUSR    0400
#define S_IWUSR    0200
#define S_IXUSR    0100
#define S_IRGRP    0040
#define S_IWGRP    0020
#define S_IROTH    0004
#define F_GETFL    3
#define F_SETFL    4
#define F_SETFD    2
#define FD_CLOEXEC 1
int open(const char *pathname, int flags, ...);
int fcntl(int fd, int cmd, ...);
#endif

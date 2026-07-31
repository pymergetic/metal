#ifndef _METAL_DB_SYS_STAT_H
#define _METAL_DB_SYS_STAT_H

#ifndef S_IRWXU
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXU 0700
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXG 0070
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXO 0007
#define S_ISUID 04000
#define S_ISGID 02000
#endif
#include "types.h"
#include <stdint.h>
#define S_IFMT     0170000
#define S_IFREG    0100000
#define S_IFDIR    0040000
#define S_IFCHR    0020000
#define S_IRUSR    0400
#define S_IWUSR    0200
#define S_IXUSR    0100
#define S_IRGRP    0040
#define S_IROTH    0004
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
struct stat {
  mode_t st_mode;
  off_t  st_size;
  uid_t  st_uid;
  gid_t  st_gid;
};
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int chmod(const char *path, mode_t mode);
int mkdir(const char *path, mode_t mode);
#endif

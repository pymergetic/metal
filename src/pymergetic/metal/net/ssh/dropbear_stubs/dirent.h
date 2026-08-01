#ifndef _METAL_DB_DIRENT_H
#define _METAL_DB_DIRENT_H
struct dirent {
  unsigned long d_ino;
  char          d_name[256];
};
typedef struct {
  int _fd;
} DIR;
DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
#endif

#ifndef _METAL_DB_UNISTD_H
#define _METAL_DB_UNISTD_H
#include "sys/types.h"
#include <stdint.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int pipe(int pipefd[2]);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup2(int oldfd, int newfd);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t uid);
int setegid(gid_t gid);
int setreuid(uid_t ruid, uid_t euid);
int setregid(gid_t rgid, gid_t egid);
int initgroups(const char *user, gid_t group);
int getgroups(int size, gid_t list[]);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int access(const char *pathname, int mode);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);
int isatty(int fd);
int daemon(int nochdir, int noclose);
char *crypt(const char *key, const char *salt);
char *getpass(const char *prompt);
void _exit(int status);
pid_t fork(void);
pid_t vfork(void);
int fsync(int fd);
int link(const char *oldpath, const char *newpath);
int unlink(const char *pathname);
int rename(const char *oldpath, const char *newpath);
int execv(const char *path, char *const argv[]);
int execve(const char *path, char *const argv[], char *const envp[]);
void  setusershell(void);
char *getusershell(void);
void  endusershell(void);

int chown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
pid_t setsid(void);
pid_t getsid(pid_t pid);
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgid(pid_t pid);
extern char **environ;
#endif

#ifndef _METAL_DB_SIGNAL_H
#define _METAL_DB_SIGNAL_H
#include "sys/types.h"
#define SIG_DFL  ((void (*)(int))0)
#define SIG_IGN  ((void (*)(int))1)
#define SIG_ERR  ((void (*)(int)) - 1)
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGABRT  6
#define SIGFPE   8
#define SIGKILL  9
#define SIGSEGV  11
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGUSR1  10
#define SIGUSR2  12
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  21
#define SIGTTOU  22
#define SIGWINCH 28
typedef void (*sighandler_t)(int);
struct sigaction {
  sighandler_t sa_handler;
  sigset_t     sa_mask;
  int          sa_flags;
};
#define SA_NOCLDSTOP 1
#define SA_RESTART   2
sighandler_t signal(int signum, sighandler_t handler);
int          kill(pid_t pid, int sig);
unsigned int alarm(unsigned int seconds);
int          sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int          sigemptyset(sigset_t *set);
int          sigaddset(sigset_t *set, int signum);
int          sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#endif

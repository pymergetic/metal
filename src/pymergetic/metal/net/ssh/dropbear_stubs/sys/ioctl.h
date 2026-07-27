#ifndef _METAL_DB_SYS_IOCTL_H
#define _METAL_DB_SYS_IOCTL_H
#ifndef FIONREAD
#define FIONREAD 0x541B
#endif
#ifndef TIOCSCTTY
#define TIOCSCTTY 0x540E
#endif
#ifndef TIOCGWINSZ
#define TIOCGWINSZ 0x5413
#endif
#ifndef TIOCSWINSZ
#define TIOCSWINSZ 0x5414
#endif
int ioctl(int fd, unsigned long request, ...);
#endif

/* Metal freestanding Dropbear config — not from autoconf. */
#ifndef DROPBEAR_CONFIG_H_
#define DROPBEAR_CONFIG_H_

#define BUNDLED_LIBTOM     1
#define DISABLE_LASTLOG    1
#define DISABLE_UTMP       1
#define DISABLE_UTMPX      1
#define DISABLE_WTMP       1
#define DISABLE_WTMPX      1
#define DISABLE_PUTUTLINE  1
#define DISABLE_PUTUTXLINE 1
/* Leave DISABLE_SYSLOG undefined so Dropbear's #ifndef DISABLE_SYSLOG paths compile. */
#define DISABLE_ZLIB    1
#define DROPBEAR_FUZZ   0
#define DROPBEAR_PLUGIN 0

#define HAVE_BASENAME                          1
#define HAVE_CLEARENV                          0
#define HAVE_CLOCK_GETTIME                     0
#define HAVE_CRYPT                             1
#define HAVE_DAEMON                            0
#define HAVE_EXPLICIT_BZERO                    0
#define HAVE_FEXECVE                           0
#define HAVE_FORK                              0
#define HAVE_GETADDRINFO                       1
#define HAVE_GETNAMEINFO                       1
#define HAVE_STRUCT_SOCKADDR_STORAGE           1
#define HAVE_STRUCT_IN6_ADDR                   1
#define HAVE_STRUCT_SOCKADDR_IN6               1
#define HAVE_STRUCT_ADDRINFO                   1
#define HAVE_GETPASS                           0
#define HAVE_GETSPNAM                          0
#define HAVE_GETTIMEOFDAY                      1
#define HAVE_LIBGEN_H                          1
#define HAVE_NETINET_IN_H                      1
#define HAVE_NETINET_TCP_H                     1
#define HAVE_OPENPTY                           0
#define HAVE__GETPTY                           0
#define HAVE_DEV_PTMX                          0
#define HAVE_PTY_H                             0
#define HAVE_STROPTS_H                         0
#define HAVE_LINUX_PKTINFO                     0
#define HAVE_STRUCT_SOCKADDR_STORAGE_SS_FAMILY 1
#define HAVE_SYS_SELECT_H                      1
#define HAVE_UINT16_T                          1
#define HAVE_UINT32_T                          1
#define HAVE_UINT8_T                           1
#define HAVE_U_INT16_T                         0
#define HAVE_U_INT32_T                         0
#define HAVE_U_INT8_T                          0
#define HAVE_INTTYPES_H                        1
#define HAVE_STDINT_H                          1
#define HAVE_STDLIB_H                          1
#define HAVE_STRING_H                          1
#define HAVE_STRINGS_H                         0
#define HAVE_UNISTD_H                          1
#define HAVE_FCNTL_H                           1
#define HAVE_SYS_IOCTL_H                       1
#define HAVE_SYS_PARAM_H                       1
#define HAVE_SYS_RESOURCE_H                    1
#define HAVE_SYS_SOCKET_H                      1
#define HAVE_SYS_STAT_H                        1
#define HAVE_SYS_TIME_H                        1
#define HAVE_SYS_TYPES_H                       1
#define HAVE_SYS_UIO_H                         1
#define HAVE_SYS_UN_H                          1
#define HAVE_SYS_WAIT_H                        1
#define HAVE_ARPA_INET_H                       1
#define HAVE_NETDB_H                           1
#define HAVE_PWD_H                             1
#define HAVE_GRP_H                             1
#define HAVE_SIGNAL_H                          1
#define HAVE_TERMIOS_H                         1
#define HAVE_CTYPE_H                           1
#define HAVE_ERRNO_H                           1
#define HAVE_LIMITS_H                          1
#define HAVE_STDARG_H                          1
#define HAVE_STDIO_H                           1
#define HAVE_TIME_H                            1
#define HAVE_DIRENT_H                          1
#define HAVE_SETJMP_H                          1
#define HAVE_LIBGEN_H                          1
#define HAVE_STRLCAT                           1
#define HAVE_STRLCPY                           1
#define HAVE_GETGROUPS                         1
#define HAVE_FD_SET                            1
#define HAVE_DECL_WRITEV                       1
#define HAVE_WRITEV                            1
#define HAVE_SYS_RANDOM_H                      1
#define HAVE_GETRANDOM                         1
#define HAVE_HSTRERROR                         0
#define HAVE_INET_ATON                         1
#define HAVE_INET_PTON                         1
#define HAVE_INET_NTOP                         1
#define HAVE_SNPRINTF                          1
#define HAVE_VSNPRINTF                         1
#define HAVE_GETUSERSHELL                      0
#define HAVE_ENDUSERSHELL                      0
#define HAVE_SETUSERSHELL                      0
#define HAVE_LOGINREC                          0

#define select                        select
#define HAVE_CONST_GAI_STRERROR_PROTO 0

#ifndef __attribute__
#if !defined(__GNUC__)
#define __attribute__(x)
#endif
#endif

#endif /* DROPBEAR_CONFIG_H_ */

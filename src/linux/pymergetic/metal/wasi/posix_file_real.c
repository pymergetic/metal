/*
 * WAMR posix_file.c with the symbols we override renamed to __real_os_*.
 * Included (not edited) from external/wamr — see docs/SOURCETREE.md.
 */
#define os_realpath __real_os_realpath
#define os_open_preopendir __real_os_open_preopendir
#define os_openat __real_os_openat
#define os_close __real_os_close
#define os_fstat __real_os_fstat
#define os_fstatat __real_os_fstatat
#define os_fdopendir __real_os_fdopendir
#define os_readdir __real_os_readdir
#define os_closedir __real_os_closedir
#define os_rewinddir __real_os_rewinddir
#define os_seekdir __real_os_seekdir
#define os_file_get_access_mode __real_os_file_get_access_mode
#define os_readlinkat __real_os_readlinkat
#define os_mkdirat __real_os_mkdirat
#define os_unlinkat __real_os_unlinkat
#define os_renameat __real_os_renameat
#define os_linkat __real_os_linkat
#define os_symlinkat __real_os_symlinkat
#define os_utimensat __real_os_utimensat

#include "posix_file.c"

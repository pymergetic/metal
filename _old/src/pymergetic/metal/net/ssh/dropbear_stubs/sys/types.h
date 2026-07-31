/* Freestanding Dropbear types — claim glibc guards so host sys/types.h is a no-op. */
#ifndef _METAL_DB_SYS_TYPES_H
#define _METAL_DB_SYS_TYPES_H

#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H 1
#endif

#include <stdint.h>
#include <stddef.h>

typedef int32_t        pid_t;
typedef uint32_t       uid_t;
typedef uint32_t       gid_t;
typedef int32_t        ssize_t;
typedef uint32_t       mode_t;
typedef int64_t        off_t;
typedef uint32_t       useconds_t;
typedef int32_t        suseconds_t;
typedef unsigned long  sigset_t;
typedef uint32_t       socklen_t;
typedef uint8_t        u_int8_t;
typedef uint16_t       u_int16_t;
typedef uint32_t       u_int32_t;
typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
#endif

#endif

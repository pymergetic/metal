/* Freestanding stub for WAMR under Metal EFI/BIOS. */
#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PRIu64
#define PRIu64 "llu"
#endif
#ifndef PRIx64
#define PRIx64 "llx"
#endif
#ifndef PRIX64
#define PRIX64 "llX"
#endif
#ifndef PRId64
#define PRId64 "lld"
#endif

#ifdef __cplusplus
}
#endif

#endif /* _INTTYPES_H */

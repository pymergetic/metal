#ifndef PM_METAL_LIBC_MALLOC_H_
#define PM_METAL_LIBC_MALLOC_H_

/*
 * Freestanding shim: UEFI builds use --target=*-windows-gnu so
 * mpconfigport_common.h picks <malloc.h> for alloca(). Real heap is
 * Metal TLSF via stdlib / MICROPY_WASM_*; this header only covers alloca.
 */
#include <alloca.h>

#endif /* PM_METAL_LIBC_MALLOC_H_ */

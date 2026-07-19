/** @file
  Compile vendored TLSF with EDK2-friendly shims (no host libc link).
**/
#include "tlsf_shim.h"

/* Stop tlsf.c from pulling fortified host stdio (needs __printf_chk). */
#define _STDIO_H
#define _STDIO_H_
#define __STDIO_H

#include "tlsf.c"

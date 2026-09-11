/*
 * Firmware Metal image. GC off, scheduler off. Unix MICROPY_PY_METAL=1
 * keeps GC via mpconfig_unix.h — do not include this header on unix TUs.
 *
 *   #include "extmod/metal/mpconfig_firmware.h"
 */
#ifndef PYMERGETIC_METAL_MPCONFIG_FIRMWARE_H
#define PYMERGETIC_METAL_MPCONFIG_FIRMWARE_H

#include "extmod/wasmmod/ports/freestanding/mpconfig_freestanding.h"

/* Three pieces of the language the MINIMUM ROM level leaves out, which a
 * seat with a REPL cannot do without: `"%s" % x`, `s[1:3]`, and `a, b = f()`.
 * The corner REPL panel runs the same Python here as on unix and in the
 * browser cell — its own module slices the line it was handed — and a person
 * at the UART REPL will reach for all three in the first minute. A few KB
 * of image for a seat that is a Python seat at all. */
#ifndef MICROPY_PY_BUILTINS_STR_OP_MODULO
#define MICROPY_PY_BUILTINS_STR_OP_MODULO (1)
#endif
#ifndef MICROPY_PY_BUILTINS_SLICE
#define MICROPY_PY_BUILTINS_SLICE (1)
#endif
#ifndef MICROPY_COMP_DOUBLE_TUPLE_ASSIGN
#define MICROPY_COMP_DOUBLE_TUPLE_ASSIGN (1)
#endif

#endif /* PYMERGETIC_METAL_MPCONFIG_FIRMWARE_H */

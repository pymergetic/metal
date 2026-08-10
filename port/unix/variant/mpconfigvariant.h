/*
 * metal unix host seat — curl-and-run Linux userspace µPy.
 * VARIANT_DIR is outside ports/unix; include common via TOP-relative path.
 *
 * PORT_INIT is a no-op (imports too early in mp_init; sys is ROM-fixed).
 * Banner / autoexec: micropython -m pymergetic.metal.unix
 *
 * GC heap default (override at build: -DMICROPY_HEAP_SIZE=…, or at run:
 * METAL_HEAPSIZE / MICROPY_HEAPSIZE env, or -X heapsize=).
 */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

/* Seat REPL is the face — Ctrl-D returns to >>>; shutdown()/reboot() leave. */
#define MICROPY_METAL_REPL_IS_SEAT (1)

#ifndef MICROPY_HEAP_SIZE
#define MICROPY_HEAP_SIZE (4 * 1024 * 1024)
#endif

#include "../../../../../ports/unix/variants/mpconfigvariant_common.h"

void metal_unix_port_init(void);
#define MICROPY_PORT_INIT_FUNC metal_unix_port_init()

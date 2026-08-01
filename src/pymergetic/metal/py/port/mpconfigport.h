/* Metal py port — GC off, Metal alloc. Expanded when upy mirror links. */
#ifndef MICROPY_INCLUDED_METAL_MPCONFIGPORT_H
#define MICROPY_INCLUDED_METAL_MPCONFIGPORT_H

/* GC ripped out — Metal owns memory (Locked #3/#5). */
#define MICROPY_ENABLE_GC (0)
#define MICROPY_ENABLE_FINALISER (0)

#endif /* MICROPY_INCLUDED_METAL_MPCONFIGPORT_H */

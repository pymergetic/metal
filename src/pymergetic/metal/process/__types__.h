/* pymergetic.metal.process — table over async task handles. */
#ifndef PYMERGETIC_METAL_PROCESS_TYPES_H
#define PYMERGETIC_METAL_PROCESS_TYPES_H

#include "pymergetic/util/mem/__types__.h"

/* C-only face: the sub-arena behind a pid's budget (NULL when unbudgeted).
 * Not a registry export — the Python bridge cannot compose an arena pointer;
 * in-kernel C consumers (compile bridges) resolve it directly. */
pm_util_mem_arena_t *pm_metal_process_arena(int32_t pid);

#endif /* PYMERGETIC_METAL_PROCESS_TYPES_H */

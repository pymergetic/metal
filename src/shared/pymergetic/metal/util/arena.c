/*
 * Loader for pm_metal_util_arena_* — the real body lives in
 * include/pymergetic/metal/util/arena_impl.h so it compiles unchanged for
 * both the runtime binary (this file) and, later, a modlib loader built by
 * wasi-sdk for guests. See docs/SOURCETREE.md "shared".
 */
#include "pymergetic/metal/util/arena_impl.h" /* IWYU pragma: keep */

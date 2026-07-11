/*
 * Memory — convenience umbrella. Pulls in the shared ops-struct layout
 * plus all three dedicated getters, for callers that want more than one
 * kind (runtime.c, main.c) instead of hand-picking headers. Nothing
 * declared here directly — include ram.h/kheap.h/bytecode.h/ops.h
 * individually if you only need one.
 */
#ifndef PYMERGETIC_METAL_MEMORY_MEMORY_H_
#define PYMERGETIC_METAL_MEMORY_MEMORY_H_

#include "pymergetic/metal/memory/bytecode.h" /* IWYU pragma: export */
#include "pymergetic/metal/memory/kheap.h" /* IWYU pragma: export */
#include "pymergetic/metal/memory/ops.h" /* IWYU pragma: export */
#include "pymergetic/metal/memory/ram.h" /* IWYU pragma: export */

#endif /* PYMERGETIC_METAL_MEMORY_MEMORY_H_ */

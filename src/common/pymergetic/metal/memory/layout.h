/*
 * Server-class Metal memory layout — same preferred sizes on every platform
 * (linux / nuttx / zephyr). Platforms differ only in how the pools are
 * backed (malloc / host mmap / static BSS / MMU map), not in the split.
 *
 * Keep scripts/lib/memory-layout.sh and (on Zephyr) the static/arena pool
 * (≥ kheap + bytecode) in lockstep with these numbers. See docs/MEMORY.md.
 */
#ifndef PYMERGETIC_METAL_MEMORY_LAYOUT_H_
#define PYMERGETIC_METAL_MEMORY_LAYOUT_H_

#include <stdint.h>

/* WAMR Alloc_With_Pool — linear memory (Zephyr) + operand stacks + EMS. */
#define PM_METAL_MEMORY_KHEAP_BYTES (256ull * 1024ull * 1024ull)

/* Raw .wasm buffers (load_file / load_bytes); separate from kheap. */
#define PM_METAL_MEMORY_BYTECODE_BYTES (64ull * 1024ull * 1024ull)

/* Per-instantiate WASM operand stack (wasmtime CPython max-wasm-stack). */
#define PM_METAL_MEMORY_STACK_BYTES (16u * 1024u * 1024u)

#endif /* PYMERGETIC_METAL_MEMORY_LAYOUT_H_ */

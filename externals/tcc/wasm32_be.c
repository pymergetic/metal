/*
 * WASM32 backend wrapper object.
 *
 * Compiles wasm32-gen.c + wasm32-link.c as a standalone compilation unit
 * with prefixed symbols (via wasm32_renames.h), so the object can coexist
 * with the native x86_64 TCC library.
 *
 * Build:
 *   cc -c -std=gnu11 -I$(TCC_DIR) -DTCC_TARGET_WASM32 wasm32_be.c -o wasm32_be.o
 */
#include "wasm32_renames.h"
#include "tcc.h"
#include "wasm32-gen.c"
#include "wasm32-link.c"
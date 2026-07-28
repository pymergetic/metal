/*
 * t8_multimod_lib's own public surface — one export, `add`, consumed
 * directly by mods/tests/t9_multimod_app via WAMR's WASM_ENABLE_MULTI_MODULE
 * (see ../main.c's own file header for why that's interesting: no host
 * round-trip for this call, unlike every WASI import elsewhere in this
 * tree). Packed as the "h@mod.t8_multimod_lib" iface header pack
 * (docs/DOC_IFACE_PLAN.md Part II-E, scripts/build.d/port/efi/embed-iface.sh)
 * — a v1 seed proving a *mod* (not just kernel headers) can publish its
 * own browsable/readable header, `iface cat h@mod.t8_multimod_lib
 * t8_multimod_lib.h`.
 */
#ifndef T8_MULTIMOD_LIB_H_
#define T8_MULTIMOD_LIB_H_

#include <stdint.h>

/* Exported as "add" (see main.c's export_name attribute) — imported by
 * another .wasm module directly, not called through this header (a
 * sub-module import is wired at link time by name+signature, not a C
 * prototype); this declaration documents that shape for a human reader. */
int32_t t8_multimod_lib_add(int32_t a, int32_t b);

#endif /* T8_MULTIMOD_LIB_H_ */

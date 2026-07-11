/*
 * Byte-count formatting — binary-prefix (KiB/MiB/GiB/TiB) human strings.
 * Pure C, zero OS/platform dependency — the narrow exception to "runtime
 * must not include from include/" (see docs/SOURCETREE.md "shared"): safe
 * for both mods and the runtime binary to call.
 *
 * Body lives in size_impl.h, compiled once per binary via a thin loader
 * .c (src/shared/pymergetic/metal/util/size.c for the runtime; a modlib
 * loader will do the same for guests later) — never #include size_impl.h
 * directly, only this contract.
 */
#ifndef PYMERGETIC_METAL_UTIL_SIZE_H_
#define PYMERGETIC_METAL_UTIL_SIZE_H_

#include <stddef.h>
#include <stdint.h>

/* "1023 TiB" + NUL */
#define PM_METAL_UTIL_SIZE_FORMAT_MAX 16U

/* impl: shared — include/pymergetic/metal/util/size_impl.h
 *
 * Format bytes using binary prefixes (KiB/MiB/GiB/TiB) — largest unit with
 * value >= 1, integer division. Returns snprintf-style length, -1 on error. */
int pm_metal_util_size_format(char *out, size_t cap, uint64_t bytes);

/* impl: shared — include/pymergetic/metal/util/size_impl.h
 *
 * Format as "<bytes> (<human>)", e.g. "92946432 (88 MiB)". Returns
 * snprintf-style length, -1 on error. */
int pm_metal_util_size_format_bytes(char *out, size_t cap, uint64_t bytes);

#endif /* PYMERGETIC_METAL_UTIL_SIZE_H_ */

/*
 * IDE/clangd fallback when wasi-sysroot is not on the include path.
 * Real wasm builds resolve <strings.h> from wasi-sdk instead.
 */
#ifndef METAL_DOOM_IDE_STRINGS_H_
#define METAL_DOOM_IDE_STRINGS_H_

#include <stddef.h>
#include <string.h>

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);

#endif /* METAL_DOOM_IDE_STRINGS_H_ */

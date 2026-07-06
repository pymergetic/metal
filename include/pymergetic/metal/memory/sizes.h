/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PM_METAL_MEMORY_SIZES_H_
#define PM_METAL_MEMORY_SIZES_H_

#include <stddef.h>

/* IDE/clangd fallback when autoconf.h is not on the include path (matches overlays). */
#ifndef CONFIG_SRAM_SIZE
#define CONFIG_SRAM_SIZE 98304
#endif

#define PM_METAL_MEMORY_KIB ((size_t)1024)

#endif /* PM_METAL_MEMORY_SIZES_H_ */

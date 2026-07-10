#ifndef PM_METAL_UTIL_SIZE_H
#define PM_METAL_UTIL_SIZE_H

#include <stddef.h>
#include <stdint.h>

/* "1023 TiB" + NUL */
#define PM_METAL_UTIL_SIZE_FORMAT_MAX 16U

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Format byte count using binary prefixes (KiB, MiB, GiB, TiB).
 * Picks the largest unit with value >= 1 (integer division, same as prior splash helpers).
 * Returns snprintf length, or -1 on error.
 */
int pm_metal_util_size_format(char *out, size_t cap, uint64_t bytes);

/*
 * Format byte count as "N (human)" e.g. "92946432 (88 MiB)".
 * Returns snprintf length, or -1 on error.
 */
int pm_metal_util_size_format_bytes(char *out, size_t cap, uint64_t bytes);

#ifdef __cplusplus
}

namespace pm::kernel::util {

struct Size {
    static int format(char *out, size_t cap, uint64_t bytes) { return pm_metal_util_size_format(out, cap, bytes); }
};

inline constexpr Size size{};

} /* namespace pm::kernel::util */
#endif

#endif /* PM_METAL_UTIL_SIZE_H */

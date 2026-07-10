#ifndef PYMERGETIC_METAL_UTIL_VERSION_H_
#define PYMERGETIC_METAL_UTIL_VERSION_H_

#include <pymergetic/metal/util/endian.h>

#include <stddef.h>
#include <stdint.h>

/*
 * Packed uint32 LE wire layout (see endian.h):
 *   bits 24..31  major (uint8)
 *   bits 16..23  minor (uint8)
 *   bits  0..15  patch (uint16)
 *
 * Example: 1.2.3 -> 0x01020003
 */
#define PM_METAL_UTIL_VERSION_MAKE(major, minor, patch)                                              \
	((uint32_t)((((uint32_t)(major) & 0xFFu) << 24) | (((uint32_t)(minor) & 0xFFu) << 16) |     \
		    ((uint32_t)(patch) & 0xFFFFu)))

#define PM_METAL_UTIL_VERSION_MAJOR(v) ((uint8_t)(((v) >> 24) & 0xFFu))
#define PM_METAL_UTIL_VERSION_MINOR(v) ((uint8_t)(((v) >> 16) & 0xFFu))
#define PM_METAL_UTIL_VERSION_PATCH(v) ((uint16_t)((v) & 0xFFFFu))

typedef union pm_metal_util_version {
	uint32_t v;
	struct {
		uint16_t patch;
		uint8_t minor;
		uint8_t major;
	} wire;
} pm_metal_util_version_t;

#ifdef __cplusplus
extern "C" {
#endif

void pm_metal_util_version_from_u32(pm_metal_util_version_t *out, uint32_t v);
uint32_t pm_metal_util_version_to_u32(const pm_metal_util_version_t *version);
void pm_metal_util_version_from_wire_u32(pm_metal_util_version_t *out, const uint8_t wire[4]);
void pm_metal_util_version_to_wire_u32(const pm_metal_util_version_t *version, uint8_t wire[4]);
int pm_metal_util_version_to_string(const pm_metal_util_version_t *version, char *out, size_t out_len);
int pm_metal_util_version_from_string(const char *s, pm_metal_util_version_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_VERSION_H_ */

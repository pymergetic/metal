#ifndef PYMERGETIC_METAL_UTIL_VERSION_H_
#define PYMERGETIC_METAL_UTIL_VERSION_H_

#include <pymergetic/metal/util/endian.h>
#include <pymergetic/metal/export.h>

#include <stddef.h>
#include <stdint.h>

/*
 * Packed uint32 LE wire layout (see endian.h):
 *   bits 24..31  major  (uint8)
 *   bits 16..23  minor  (uint8)
 *   bits  8..15  patch  (uint8)
 *   bits  0..7   stage  (uint8) — release channel, not a fifth semver field
 *
 * Example: 1.2.3 prod -> 0x01020300
 * Example: 1.2.3 dev  -> 0x01020304
 */
typedef enum pm_metal_util_version_stage {
	PM_METAL_UTIL_VERSION_STAGE_PROD = 0,
	PM_METAL_UTIL_VERSION_STAGE_RC = 1,
	PM_METAL_UTIL_VERSION_STAGE_BETA = 2,
	PM_METAL_UTIL_VERSION_STAGE_ALPHA = 3,
	PM_METAL_UTIL_VERSION_STAGE_DEV = 4,
} pm_metal_util_version_stage_t;

#define PM_METAL_UTIL_VERSION_MAKE_FULL(major, minor, patch, stage)                                  \
	((uint32_t)((((uint32_t)(major) & 0xFFu) << 24) | (((uint32_t)(minor) & 0xFFu) << 16) |     \
		    (((uint32_t)(patch) & 0xFFu) << 8) | ((uint32_t)(stage) & 0xFFu)))

#define PM_METAL_UTIL_VERSION_MAKE(major, minor, patch)                                              \
	PM_METAL_UTIL_VERSION_MAKE_FULL(major, minor, patch, PM_METAL_UTIL_VERSION_STAGE_PROD)

#define PM_METAL_UTIL_VERSION_MAJOR(v) ((uint8_t)(((v) >> 24) & 0xFFu))
#define PM_METAL_UTIL_VERSION_MINOR(v) ((uint8_t)(((v) >> 16) & 0xFFu))
#define PM_METAL_UTIL_VERSION_PATCH(v) ((uint8_t)(((v) >> 8) & 0xFFu))
#define PM_METAL_UTIL_VERSION_STAGE(v) ((uint8_t)((v) & 0xFFu))

typedef union pm_metal_util_version {
	uint32_t v;
	struct {
		uint8_t stage;
		uint8_t patch;
		uint8_t minor;
		uint8_t major;
	} wire;
} pm_metal_util_version_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Defined: host/common/pymergetic/metal/util/version.c */
PM_METAL_API(void, pm_metal_util_version_from_u32, (pm_metal_util_version_t *out, uint32_t v));
PM_METAL_API(uint32_t, pm_metal_util_version_to_u32, (const pm_metal_util_version_t *version));
PM_METAL_API(void, pm_metal_util_version_from_wire_u32,
	     (pm_metal_util_version_t *out, const uint8_t wire[4]));
PM_METAL_API(void, pm_metal_util_version_to_wire_u32,
	     (const pm_metal_util_version_t *version, uint8_t wire[4]));
PM_METAL_API(const char *, pm_metal_util_version_stage_label, (uint8_t stage));
PM_METAL_API(int, pm_metal_util_version_stage_from_label, (const char *label, uint8_t *out));
PM_METAL_API(int, pm_metal_util_version_to_string,
	     (const pm_metal_util_version_t *version, char *out, size_t out_len));
PM_METAL_API(int, pm_metal_util_version_from_string,
	     (const char *s, pm_metal_util_version_t *out));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_VERSION_H_ */

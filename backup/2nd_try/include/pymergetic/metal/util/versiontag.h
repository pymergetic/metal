#ifndef PYMERGETIC_METAL_UTIL_VERSIONTAG_H_
#define PYMERGETIC_METAL_UTIL_VERSIONTAG_H_

#include <pymergetic/metal/util/endian.h>
#include <pymergetic/metal/util/version.h>
#include <pymergetic/metal/export.h>

#include <stddef.h>
#include <stdint.h>

#define PM_METAL_UTIL_VERSIONTAG_WIRE_LEN 16u

/*
 * Wire block: packed version u32 (LE) + u32 pad + build timestamp u64 (LE).
 *
 * Layout (16 bytes, see endian.h):
 *   offset  0..3   version
 *   offset  4..7   pad     — reserved zero
 *   offset  8..15  built   — UTC Unix seconds at build/publish time
 *
 * Used standalone or embedded in bigtag after an eightcc magic.
 */
typedef struct pm_metal_util_versiontag {
	pm_metal_util_version_t version;
	uint32_t pad;
	uint64_t built;
} pm_metal_util_versiontag_t;

#define PM_METAL_UTIL_VERSIONTAG(version_u32, built_u64)                                             \
	((pm_metal_util_versiontag_t){                                                              \
		.version = {.v = (version_u32)},                                                      \
		.pad = 0u,                                                                           \
		.built = (built_u64),                                                                \
	})

#ifdef __cplusplus
extern "C" {
#endif

static inline void pm_metal_util_versiontag_set(pm_metal_util_versiontag_t *out, uint32_t version_u32,
						uint64_t built_u64)
{
	if (out == NULL) {
		return;
	}
	out->version.v = version_u32;
	out->pad = 0u;
	out->built = built_u64;
}

static inline int pm_metal_util_versiontag_valid(const pm_metal_util_versiontag_t *tag,
						 uint32_t version_u32)
{
	return tag != NULL && tag->version.v == version_u32 && tag->pad == 0u;
}

static inline int pm_metal_util_versiontag_eq(const pm_metal_util_versiontag_t *a,
					      const pm_metal_util_versiontag_t *b)
{
	return a != NULL && b != NULL && a->version.v == b->version.v && a->pad == b->pad &&
	       a->built == b->built;
}

/* Defined: host/common/pymergetic/metal/util/versiontag.c */
PM_METAL_API(void, pm_metal_util_versiontag_from_wire_bytes,
	     (const uint8_t wire[PM_METAL_UTIL_VERSIONTAG_WIRE_LEN], pm_metal_util_versiontag_t *out));
PM_METAL_API(void, pm_metal_util_versiontag_to_wire_bytes,
	     (const pm_metal_util_versiontag_t *tag,
	      uint8_t wire[PM_METAL_UTIL_VERSIONTAG_WIRE_LEN]));
PM_METAL_API(int, pm_metal_util_versiontag_to_string,
	     (const pm_metal_util_versiontag_t *tag, char *out, size_t out_len));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_VERSIONTAG_H_ */

/*
 * FourCC tags — semantic BE u32 + LE wire bytes. See endian.h.
 *
 * impl: common — src/common/pymergetic/metal/util/fourcc.c
 */
#ifndef PYMERGETIC_METAL_UTIL_FOURCC_H_
#define PYMERGETIC_METAL_UTIL_FOURCC_H_

#include <pymergetic/metal/util/endian.h>
#include <pymergetic/metal/export.h>

#include <stdint.h>

#define PM_METAL_UTIL_FOURCC_LEN 4

/* Big-endian FourCC (semantic tag value); matches pm_metal_util_fourcc_from_string(). */
#define PM_METAL_UTIL_FOURCC(a, b, c, d)                                                              \
	(((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | ((uint32_t)(uint8_t)(c) << 8) | \
	 (uint32_t)(uint8_t)(d))

/* LE wire value — byte0..3 spell the tag in on-wire blobs (see endian.h). */
#define PM_METAL_UTIL_FOURCC_LE(a, b, c, d)                                                           \
	((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) |       \
	 ((uint32_t)(uint8_t)(d) << 24))

typedef uint8_t pm_metal_util_fourcc_wire_t[PM_METAL_UTIL_FOURCC_LEN];

typedef union pm_metal_util_fourcc {
	uint32_t v;
	pm_metal_util_fourcc_wire_t wire;
} pm_metal_util_fourcc_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Defined: src/common/pymergetic/metal/util/fourcc.c */
PM_METAL_API(void, pm_metal_util_fourcc_from_u32, (pm_metal_util_fourcc_t *out, uint32_t v));
PM_METAL_API(uint32_t, pm_metal_util_fourcc_to_u32, (const pm_metal_util_fourcc_t *tag));
PM_METAL_API(void, pm_metal_util_fourcc_from_wire_bytes,
	     (const pm_metal_util_fourcc_wire_t bytes, pm_metal_util_fourcc_t *out));
PM_METAL_API(void, pm_metal_util_fourcc_to_wire_bytes,
	     (const pm_metal_util_fourcc_t *tag, pm_metal_util_fourcc_wire_t out));
PM_METAL_API(void, pm_metal_util_fourcc_from_bytes,
	     (const uint8_t bytes[PM_METAL_UTIL_FOURCC_LEN], pm_metal_util_fourcc_t *out));
PM_METAL_API(void, pm_metal_util_fourcc_to_bytes,
	     (const pm_metal_util_fourcc_t *tag, uint8_t out[PM_METAL_UTIL_FOURCC_LEN]));
PM_METAL_API(int, pm_metal_util_fourcc_from_string, (const char *s, pm_metal_util_fourcc_t *out));
PM_METAL_API(int, pm_metal_util_fourcc_to_string,
	     (const pm_metal_util_fourcc_t *tag, char out[PM_METAL_UTIL_FOURCC_LEN + 1]));
PM_METAL_API(int, pm_metal_util_fourcc_label,
	     (uint32_t magic, char out[PM_METAL_UTIL_FOURCC_LEN + 1]));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_FOURCC_H_ */

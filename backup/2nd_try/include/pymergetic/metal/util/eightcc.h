#ifndef PYMERGETIC_METAL_UTIL_EIGHTCC_H_
#define PYMERGETIC_METAL_UTIL_EIGHTCC_H_

#include <pymergetic/metal/util/endian.h>
#include <pymergetic/metal/export.h>

#include <stdint.h>

#define PM_METAL_UTIL_EIGHTCC_LEN 8

/* Big-endian eightcc (semantic tag value); matches pm_metal_util_eightcc_from_string(). */
#define PM_METAL_UTIL_EIGHTCC(a, b, c, d, e, f, g, h)                                                \
	(((uint64_t)(uint8_t)(a) << 56) | ((uint64_t)(uint8_t)(b) << 48) |                           \
	 ((uint64_t)(uint8_t)(c) << 40) | ((uint64_t)(uint8_t)(d) << 32) |                           \
	 ((uint64_t)(uint8_t)(e) << 24) | ((uint64_t)(uint8_t)(f) << 16) |                           \
	 ((uint64_t)(uint8_t)(g) << 8) | (uint64_t)(uint8_t)(h))

/* LE wire value — byte0..7 spell the tag in on-wire blobs (see endian.h). */
#define PM_METAL_UTIL_EIGHTCC_LE(a, b, c, d, e, f, g, h)                                             \
	((uint64_t)(uint8_t)(a) | ((uint64_t)(uint8_t)(b) << 8) |                                    \
	 ((uint64_t)(uint8_t)(c) << 16) | ((uint64_t)(uint8_t)(d) << 24) |                           \
	 ((uint64_t)(uint8_t)(e) << 32) | ((uint64_t)(uint8_t)(f) << 40) |                           \
	 ((uint64_t)(uint8_t)(g) << 48) | ((uint64_t)(uint8_t)(h) << 56))

typedef uint8_t pm_metal_util_eightcc_wire_t[PM_METAL_UTIL_EIGHTCC_LEN];

typedef union pm_metal_util_eightcc {
	uint64_t v;
	pm_metal_util_eightcc_wire_t wire;
} pm_metal_util_eightcc_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Defined: host/common/pymergetic/metal/util/eightcc.c */
PM_METAL_API(void, pm_metal_util_eightcc_from_u64, (pm_metal_util_eightcc_t *out, uint64_t v));
PM_METAL_API(uint64_t, pm_metal_util_eightcc_to_u64, (const pm_metal_util_eightcc_t *tag));
PM_METAL_API(void, pm_metal_util_eightcc_from_wire_bytes,
	     (const pm_metal_util_eightcc_wire_t bytes, pm_metal_util_eightcc_t *out));
PM_METAL_API(void, pm_metal_util_eightcc_to_wire_bytes,
	     (const pm_metal_util_eightcc_t *tag, pm_metal_util_eightcc_wire_t out));
PM_METAL_API(void, pm_metal_util_eightcc_from_bytes,
	     (const uint8_t bytes[PM_METAL_UTIL_EIGHTCC_LEN], pm_metal_util_eightcc_t *out));
PM_METAL_API(void, pm_metal_util_eightcc_to_bytes,
	     (const pm_metal_util_eightcc_t *tag, uint8_t out[PM_METAL_UTIL_EIGHTCC_LEN]));
PM_METAL_API(int, pm_metal_util_eightcc_from_string, (const char *s, pm_metal_util_eightcc_t *out));
PM_METAL_API(int, pm_metal_util_eightcc_to_string,
	     (const pm_metal_util_eightcc_t *tag, char out[PM_METAL_UTIL_EIGHTCC_LEN + 1]));
PM_METAL_API(int, pm_metal_util_eightcc_label,
	     (uint64_t magic, char out[PM_METAL_UTIL_EIGHTCC_LEN + 1]));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_EIGHTCC_H_ */

#ifndef PYMERGETIC_METAL_UTIL_WIRETAG_H_
#define PYMERGETIC_METAL_UTIL_WIRETAG_H_

#include <pymergetic/metal/util/endian.h>
#include <pymergetic/metal/util/fourcc.h>
#include <pymergetic/metal/util/version.h>
#include <pymergetic/metal/export.h>

#include <stddef.h>
#include <stdint.h>

#define PM_METAL_UTIL_WIRETAG_WIRE_LEN 8u

/*
 * Wire header: FourCC magic (LE) + packed version u32 (LE).
 * Eight bytes at blob offset 0 — see endian.h.
 */
typedef struct pm_metal_util_wiretag {
	pm_metal_util_fourcc_t magic;
	pm_metal_util_version_t version;
} pm_metal_util_wiretag_t;

#define PM_METAL_UTIL_WIRETAG(magic_le, version_u32)                                                 \
	((pm_metal_util_wiretag_t){                                                                  \
		.magic = {.v = (magic_le)},                                                          \
		.version = {.v = (version_u32)},                                                      \
	})

#ifdef __cplusplus
extern "C" {
#endif

static inline void pm_metal_util_wiretag_set(pm_metal_util_wiretag_t *out, uint32_t magic_le,
					     uint32_t version_u32)
{
	if (out == NULL) {
		return;
	}
	out->magic.v = magic_le;
	out->version.v = version_u32;
}

static inline int pm_metal_util_wiretag_valid(const pm_metal_util_wiretag_t *tag, uint32_t magic_le,
					      uint32_t version_u32)
{
	return tag != NULL && tag->magic.v == magic_le && tag->version.v == version_u32;
}

static inline int pm_metal_util_wiretag_eq(const pm_metal_util_wiretag_t *a,
					   const pm_metal_util_wiretag_t *b)
{
	return a != NULL && b != NULL && a->magic.v == b->magic.v && a->version.v == b->version.v;
}

/* Defined: host/common/pymergetic/metal/util/wiretag.c */
PM_METAL_API(void, pm_metal_util_wiretag_from_wire_bytes,
	     (const uint8_t wire[PM_METAL_UTIL_WIRETAG_WIRE_LEN], pm_metal_util_wiretag_t *out));
PM_METAL_API(void, pm_metal_util_wiretag_to_wire_bytes,
	     (const pm_metal_util_wiretag_t *tag, uint8_t wire[PM_METAL_UTIL_WIRETAG_WIRE_LEN]));
PM_METAL_API(int, pm_metal_util_wiretag_to_string,
	     (const pm_metal_util_wiretag_t *tag, char *out, size_t out_len));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_WIRETAG_H_ */

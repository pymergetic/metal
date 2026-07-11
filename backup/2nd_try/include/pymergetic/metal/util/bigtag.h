#ifndef PYMERGETIC_METAL_UTIL_BIGTAG_H_
#define PYMERGETIC_METAL_UTIL_BIGTAG_H_

#include <pymergetic/metal/util/eightcc.h>
#include <pymergetic/metal/util/versiontag.h>
#include <pymergetic/metal/export.h>

#include <stddef.h>
#include <stdint.h>

#define PM_METAL_UTIL_BIGTAG_WIRE_LEN                                                              \
	(PM_METAL_UTIL_EIGHTCC_LEN + PM_METAL_UTIL_VERSIONTAG_WIRE_LEN)

/*
 * Wire header: eightcc magic (LE u64) + versiontag (version + pad + built).
 *
 * Layout (24 bytes, see endian.h / versiontag.h):
 *   offset  0..7    magic  (eightcc)
 *   offset  8..23   versiontag
 *
 * Pair of wiretag (fourcc + version, 8 bytes).
 */
typedef struct pm_metal_util_bigtag {
	pm_metal_util_eightcc_t magic;
	pm_metal_util_versiontag_t versiontag;
} pm_metal_util_bigtag_t;

#define PM_METAL_UTIL_BIGTAG(magic_le, version_u32, built_u64)                                       \
	((pm_metal_util_bigtag_t){                                                                  \
		.magic = {.v = (magic_le)},                                                          \
		.versiontag = PM_METAL_UTIL_VERSIONTAG(version_u32, built_u64),                      \
	})

#ifdef __cplusplus
extern "C" {
#endif

static inline void pm_metal_util_bigtag_set(pm_metal_util_bigtag_t *out, uint64_t magic_le,
					    uint32_t version_u32, uint64_t built_u64)
{
	if (out == NULL) {
		return;
	}
	out->magic.v = magic_le;
	pm_metal_util_versiontag_set(&out->versiontag, version_u32, built_u64);
}

static inline int pm_metal_util_bigtag_valid(const pm_metal_util_bigtag_t *tag, uint64_t magic_le,
					     uint32_t version_u32)
{
	return tag != NULL && tag->magic.v == magic_le &&
	       pm_metal_util_versiontag_valid(&tag->versiontag, version_u32);
}

static inline int pm_metal_util_bigtag_eq(const pm_metal_util_bigtag_t *a,
					  const pm_metal_util_bigtag_t *b)
{
	return a != NULL && b != NULL && a->magic.v == b->magic.v &&
	       pm_metal_util_versiontag_eq(&a->versiontag, &b->versiontag);
}

/* Defined: host/common/pymergetic/metal/util/bigtag.c */
PM_METAL_API(void, pm_metal_util_bigtag_from_wire_bytes,
	     (const uint8_t wire[PM_METAL_UTIL_BIGTAG_WIRE_LEN], pm_metal_util_bigtag_t *out));
PM_METAL_API(void, pm_metal_util_bigtag_to_wire_bytes,
	     (const pm_metal_util_bigtag_t *tag, uint8_t wire[PM_METAL_UTIL_BIGTAG_WIRE_LEN]));
PM_METAL_API(int, pm_metal_util_bigtag_to_string,
	     (const pm_metal_util_bigtag_t *tag, char *out, size_t out_len));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_BIGTAG_H_ */

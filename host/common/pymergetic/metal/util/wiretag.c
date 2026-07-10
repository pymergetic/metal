#include <pymergetic/metal/util/wiretag.h>

#include <stdio.h>

void pm_metal_util_wiretag_from_wire_bytes(const uint8_t wire[PM_METAL_UTIL_WIRETAG_WIRE_LEN],
					   pm_metal_util_wiretag_t *out)
{
	if (wire == NULL || out == NULL) {
		return;
	}
	pm_metal_util_fourcc_from_wire_bytes(wire, &out->magic);
	pm_metal_util_version_from_wire_u32(&out->version, wire + PM_METAL_UTIL_FOURCC_LEN);
}

void pm_metal_util_wiretag_to_wire_bytes(const pm_metal_util_wiretag_t *tag,
					 uint8_t wire[PM_METAL_UTIL_WIRETAG_WIRE_LEN])
{
	if (tag == NULL || wire == NULL) {
		return;
	}
	pm_metal_util_fourcc_to_wire_bytes(&tag->magic, wire);
	pm_metal_util_version_to_wire_u32(&tag->version, wire + PM_METAL_UTIL_FOURCC_LEN);
}

int pm_metal_util_wiretag_to_string(const pm_metal_util_wiretag_t *tag, char *out, size_t out_len)
{
	char magic[PM_METAL_UTIL_FOURCC_LEN + 1];
	char version[32];
	int n;

	if (tag == NULL || out == NULL || out_len == 0U) {
		return -1;
	}
	if (pm_metal_util_fourcc_to_string(&tag->magic, magic) != 0) {
		return -1;
	}
	if (pm_metal_util_version_to_string(&tag->version, version, sizeof(version)) != 0) {
		return -1;
	}

	n = snprintf(out, out_len, "%s/%s", magic, version);
	if (n < 0 || (size_t)n >= out_len) {
		return -1;
	}
	return 0;
}

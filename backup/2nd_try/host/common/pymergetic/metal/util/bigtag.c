#include <pymergetic/metal/util/bigtag.h>

#include <stdio.h>

void pm_metal_util_bigtag_from_wire_bytes(const uint8_t wire[PM_METAL_UTIL_BIGTAG_WIRE_LEN],
					  pm_metal_util_bigtag_t *out)
{
	if (wire == NULL || out == NULL) {
		return;
	}
	pm_metal_util_eightcc_from_wire_bytes(wire, &out->magic);
	pm_metal_util_versiontag_from_wire_bytes(wire + PM_METAL_UTIL_EIGHTCC_LEN, &out->versiontag);
}

void pm_metal_util_bigtag_to_wire_bytes(const pm_metal_util_bigtag_t *tag,
					uint8_t wire[PM_METAL_UTIL_BIGTAG_WIRE_LEN])
{
	if (tag == NULL || wire == NULL) {
		return;
	}
	pm_metal_util_eightcc_to_wire_bytes(&tag->magic, wire);
	pm_metal_util_versiontag_to_wire_bytes(&tag->versiontag,
					       wire + PM_METAL_UTIL_EIGHTCC_LEN);
}

int pm_metal_util_bigtag_to_string(const pm_metal_util_bigtag_t *tag, char *out, size_t out_len)
{
	char magic[PM_METAL_UTIL_EIGHTCC_LEN + 1];
	char versiontag[48];
	int n;

	if (tag == NULL || out == NULL || out_len == 0U) {
		return -1;
	}
	if (pm_metal_util_eightcc_to_string(&tag->magic, magic) != 0) {
		return -1;
	}
	if (pm_metal_util_versiontag_to_string(&tag->versiontag, versiontag, sizeof(versiontag)) != 0) {
		return -1;
	}

	n = snprintf(out, out_len, "%s/%s", magic, versiontag);
	if (n < 0 || (size_t)n >= out_len) {
		return -1;
	}
	return 0;
}

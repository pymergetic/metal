#include <pymergetic/metal/util/versiontag.h>

#include <stdio.h>

void pm_metal_util_versiontag_from_wire_bytes(
	const uint8_t wire[PM_METAL_UTIL_VERSIONTAG_WIRE_LEN], pm_metal_util_versiontag_t *out)
{
	if (wire == NULL || out == NULL) {
		return;
	}
	pm_metal_util_version_from_wire_u32(&out->version, wire);
	out->pad = pm_metal_util_endian_load_u32_le(wire + sizeof(uint32_t));
	out->built = pm_metal_util_endian_load_u64_le(wire + sizeof(uint32_t) + sizeof(uint32_t));
}

void pm_metal_util_versiontag_to_wire_bytes(const pm_metal_util_versiontag_t *tag,
					    uint8_t wire[PM_METAL_UTIL_VERSIONTAG_WIRE_LEN])
{
	if (tag == NULL || wire == NULL) {
		return;
	}
	pm_metal_util_version_to_wire_u32(&tag->version, wire);
	pm_metal_util_endian_store_u32_le(wire + sizeof(uint32_t), tag->pad);
	pm_metal_util_endian_store_u64_le(wire + sizeof(uint32_t) + sizeof(uint32_t), tag->built);
}

int pm_metal_util_versiontag_to_string(const pm_metal_util_versiontag_t *tag, char *out,
				       size_t out_len)
{
	char version[32];
	int n;

	if (tag == NULL || out == NULL || out_len == 0U) {
		return -1;
	}
	if (pm_metal_util_version_to_string(&tag->version, version, sizeof(version)) != 0) {
		return -1;
	}

	n = snprintf(out, out_len, "%s@%llu", version, (unsigned long long)tag->built);
	if (n < 0 || (size_t)n >= out_len) {
		return -1;
	}
	return 0;
}

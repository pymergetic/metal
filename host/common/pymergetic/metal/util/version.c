#include <pymergetic/metal/util/version.h>

#include <stdio.h>
#include <string.h>

void pm_metal_util_version_from_u32(pm_metal_util_version_t *out, uint32_t v)
{
	if (out == NULL) {
		return;
	}
	out->v = v;
}

uint32_t pm_metal_util_version_to_u32(const pm_metal_util_version_t *version)
{
	return version != NULL ? version->v : 0U;
}

void pm_metal_util_version_from_wire_u32(pm_metal_util_version_t *out, const uint8_t wire[4])
{
	if (out == NULL || wire == NULL) {
		return;
	}
	out->v = pm_metal_util_endian_load_u32_le(wire);
}

void pm_metal_util_version_to_wire_u32(const pm_metal_util_version_t *version, uint8_t wire[4])
{
	if (version == NULL || wire == NULL) {
		return;
	}
	pm_metal_util_endian_store_u32_le(wire, version->v);
}

int pm_metal_util_version_to_string(const pm_metal_util_version_t *version, char *out, size_t out_len)
{
	int n;

	if (version == NULL || out == NULL || out_len == 0U) {
		return -1;
	}

	n = snprintf(out, out_len, "%u.%u.%u",
		     (unsigned)PM_METAL_UTIL_VERSION_MAJOR(version->v),
		     (unsigned)PM_METAL_UTIL_VERSION_MINOR(version->v),
		     (unsigned)PM_METAL_UTIL_VERSION_PATCH(version->v));
	if (n < 0 || (size_t)n >= out_len) {
		return -1;
	}
	return 0;
}

int pm_metal_util_version_from_string(const char *s, pm_metal_util_version_t *out)
{
	unsigned major;
	unsigned minor;
	unsigned patch;
	int n;

	if (s == NULL || out == NULL) {
		return -1;
	}

	n = sscanf(s, "%u.%u.%u", &major, &minor, &patch);
	if (n < 1) {
		return -1;
	}
	if (n == 1) {
		minor = 0U;
		patch = 0U;
	} else if (n == 2) {
		patch = 0U;
	}
	if (major > 0xFFu || minor > 0xFFu || patch > 0xFFFFu) {
		return -1;
	}

	out->v = PM_METAL_UTIL_VERSION_MAKE((uint8_t)major, (uint8_t)minor, (uint16_t)patch);
	return 0;
}

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

const char *pm_metal_util_version_stage_label(uint8_t stage)
{
	switch (stage) {
	case PM_METAL_UTIL_VERSION_STAGE_PROD:
		return "";
	case PM_METAL_UTIL_VERSION_STAGE_RC:
		return "rc";
	case PM_METAL_UTIL_VERSION_STAGE_BETA:
		return "beta";
	case PM_METAL_UTIL_VERSION_STAGE_ALPHA:
		return "alpha";
	case PM_METAL_UTIL_VERSION_STAGE_DEV:
		return "dev";
	default:
		return "?";
	}
}

int pm_metal_util_version_stage_from_label(const char *label, uint8_t *out)
{
	if (label == NULL || out == NULL) {
		return -1;
	}
	if (label[0] == '\0') {
		*out = PM_METAL_UTIL_VERSION_STAGE_PROD;
		return 0;
	}
	if (strcmp(label, "rc") == 0) {
		*out = PM_METAL_UTIL_VERSION_STAGE_RC;
		return 0;
	}
	if (strcmp(label, "beta") == 0) {
		*out = PM_METAL_UTIL_VERSION_STAGE_BETA;
		return 0;
	}
	if (strcmp(label, "alpha") == 0) {
		*out = PM_METAL_UTIL_VERSION_STAGE_ALPHA;
		return 0;
	}
	if (strcmp(label, "dev") == 0) {
		*out = PM_METAL_UTIL_VERSION_STAGE_DEV;
		return 0;
	}
	return -1;
}

int pm_metal_util_version_to_string(const pm_metal_util_version_t *version, char *out, size_t out_len)
{
	const char *stage;
	int n;

	if (version == NULL || out == NULL || out_len == 0U) {
		return -1;
	}

	stage = pm_metal_util_version_stage_label(PM_METAL_UTIL_VERSION_STAGE(version->v));
	if (stage[0] == '\0') {
		n = snprintf(out, out_len, "%u.%u.%u",
			     (unsigned)PM_METAL_UTIL_VERSION_MAJOR(version->v),
			     (unsigned)PM_METAL_UTIL_VERSION_MINOR(version->v),
			     (unsigned)PM_METAL_UTIL_VERSION_PATCH(version->v));
	} else {
		n = snprintf(out, out_len, "%u.%u.%u-%s",
			     (unsigned)PM_METAL_UTIL_VERSION_MAJOR(version->v),
			     (unsigned)PM_METAL_UTIL_VERSION_MINOR(version->v),
			     (unsigned)PM_METAL_UTIL_VERSION_PATCH(version->v), stage);
	}
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
	char stage_label[16];
	uint8_t stage;
	int n;

	if (s == NULL || out == NULL) {
		return -1;
	}

	stage_label[0] = '\0';
	n = sscanf(s, "%u.%u.%u-%15s", &major, &minor, &patch, stage_label);
	if (n < 1) {
		return -1;
	}
	if (n == 1) {
		minor = 0U;
		patch = 0U;
	} else if (n == 2) {
		patch = 0U;
	}
	if (major > 0xFFu || minor > 0xFFu || patch > 0xFFu) {
		return -1;
	}
	if (pm_metal_util_version_stage_from_label(stage_label, &stage) != 0) {
		return -1;
	}

	out->v = PM_METAL_UTIL_VERSION_MAKE_FULL((uint8_t)major, (uint8_t)minor, (uint8_t)patch, stage);
	return 0;
}

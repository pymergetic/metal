#include <pymergetic/metal/util/fourcc.h>

#include <string.h>

static uint32_t pack_be(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

void pm_metal_util_fourcc_from_u32(pm_metal_util_fourcc_t *out, uint32_t v)
{
	if (out == NULL) {
		return;
	}
	out->v = v;
}

uint32_t pm_metal_util_fourcc_to_u32(const pm_metal_util_fourcc_t *tag)
{
	return tag != NULL ? tag->v : 0U;
}

void pm_metal_util_fourcc_from_wire_bytes(const pm_metal_util_fourcc_wire_t bytes,
					  pm_metal_util_fourcc_t *out)
{
	if (bytes == NULL || out == NULL) {
		return;
	}
	out->v = pm_metal_util_endian_load_u32_le(bytes);
}

void pm_metal_util_fourcc_to_wire_bytes(const pm_metal_util_fourcc_t *tag,
					pm_metal_util_fourcc_wire_t out)
{
	if (tag == NULL || out == NULL) {
		return;
	}
	pm_metal_util_endian_store_u32_le(out, tag->v);
}

void pm_metal_util_fourcc_to_bytes(const pm_metal_util_fourcc_t *tag, uint8_t out[PM_METAL_UTIL_FOURCC_LEN])
{
	if (tag == NULL || out == NULL) {
		return;
	}
	out[0] = (uint8_t)(tag->v >> 24);
	out[1] = (uint8_t)(tag->v >> 16);
	out[2] = (uint8_t)(tag->v >> 8);
	out[3] = (uint8_t)tag->v;
}

void pm_metal_util_fourcc_from_bytes(const uint8_t bytes[PM_METAL_UTIL_FOURCC_LEN],
				     pm_metal_util_fourcc_t *out)
{
	if (bytes == NULL || out == NULL) {
		return;
	}
	pm_metal_util_fourcc_from_u32(out, pack_be(bytes[0], bytes[1], bytes[2], bytes[3]));
}

int pm_metal_util_fourcc_to_string(const pm_metal_util_fourcc_t *tag, char out[PM_METAL_UTIL_FOURCC_LEN + 1])
{
	pm_metal_util_fourcc_wire_t wire;

	if (tag == NULL || out == NULL) {
		return -1;
	}
	pm_metal_util_fourcc_to_wire_bytes(tag, wire);
	memcpy(out, wire, PM_METAL_UTIL_FOURCC_LEN);
	out[PM_METAL_UTIL_FOURCC_LEN] = '\0';
	return 0;
}

int pm_metal_util_fourcc_from_string(const char *s, pm_metal_util_fourcc_t *out)
{
	pm_metal_util_fourcc_wire_t wire;

	if (s == NULL || out == NULL) {
		return -1;
	}
	if (strlen(s) != PM_METAL_UTIL_FOURCC_LEN) {
		return -1;
	}
	memcpy(wire, s, PM_METAL_UTIL_FOURCC_LEN);
	pm_metal_util_fourcc_from_wire_bytes(wire, out);
	return 0;
}

int pm_metal_util_fourcc_label(uint32_t magic, char out[PM_METAL_UTIL_FOURCC_LEN + 1])
{
	pm_metal_util_fourcc_t tag;

	if (out == NULL) {
		return -1;
	}
	pm_metal_util_fourcc_from_u32(&tag, magic);
	return pm_metal_util_fourcc_to_string(&tag, out);
}

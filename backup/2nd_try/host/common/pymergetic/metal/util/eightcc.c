#include <pymergetic/metal/util/eightcc.h>

#include <string.h>

static uint64_t pack_be8(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f,
			 uint8_t g, uint8_t h)
{
	return ((uint64_t)a << 56) | ((uint64_t)b << 48) | ((uint64_t)c << 40) |
	       ((uint64_t)d << 32) | ((uint64_t)e << 24) | ((uint64_t)f << 16) |
	       ((uint64_t)g << 8) | (uint64_t)h;
}

void pm_metal_util_eightcc_from_u64(pm_metal_util_eightcc_t *out, uint64_t v)
{
	if (out == NULL) {
		return;
	}
	out->v = v;
}

uint64_t pm_metal_util_eightcc_to_u64(const pm_metal_util_eightcc_t *tag)
{
	return tag != NULL ? tag->v : 0ULL;
}

void pm_metal_util_eightcc_from_wire_bytes(const pm_metal_util_eightcc_wire_t bytes,
					   pm_metal_util_eightcc_t *out)
{
	if (bytes == NULL || out == NULL) {
		return;
	}
	out->v = pm_metal_util_endian_load_u64_le(bytes);
}

void pm_metal_util_eightcc_to_wire_bytes(const pm_metal_util_eightcc_t *tag,
					 pm_metal_util_eightcc_wire_t out)
{
	if (tag == NULL || out == NULL) {
		return;
	}
	pm_metal_util_endian_store_u64_le(out, tag->v);
}

void pm_metal_util_eightcc_to_bytes(const pm_metal_util_eightcc_t *tag,
				    uint8_t out[PM_METAL_UTIL_EIGHTCC_LEN])
{
	if (tag == NULL || out == NULL) {
		return;
	}
	out[0] = (uint8_t)(tag->v >> 56);
	out[1] = (uint8_t)(tag->v >> 48);
	out[2] = (uint8_t)(tag->v >> 40);
	out[3] = (uint8_t)(tag->v >> 32);
	out[4] = (uint8_t)(tag->v >> 24);
	out[5] = (uint8_t)(tag->v >> 16);
	out[6] = (uint8_t)(tag->v >> 8);
	out[7] = (uint8_t)tag->v;
}

void pm_metal_util_eightcc_from_bytes(const uint8_t bytes[PM_METAL_UTIL_EIGHTCC_LEN],
				      pm_metal_util_eightcc_t *out)
{
	if (bytes == NULL || out == NULL) {
		return;
	}
	pm_metal_util_eightcc_from_u64(out,
				       pack_be8(bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
						bytes[5], bytes[6], bytes[7]));
}

int pm_metal_util_eightcc_to_string(const pm_metal_util_eightcc_t *tag,
				    char out[PM_METAL_UTIL_EIGHTCC_LEN + 1])
{
	pm_metal_util_eightcc_wire_t wire;

	if (tag == NULL || out == NULL) {
		return -1;
	}
	pm_metal_util_eightcc_to_wire_bytes(tag, wire);
	memcpy(out, wire, PM_METAL_UTIL_EIGHTCC_LEN);
	out[PM_METAL_UTIL_EIGHTCC_LEN] = '\0';
	return 0;
}

int pm_metal_util_eightcc_from_string(const char *s, pm_metal_util_eightcc_t *out)
{
	pm_metal_util_eightcc_wire_t wire;

	if (s == NULL || out == NULL) {
		return -1;
	}
	if (strlen(s) != PM_METAL_UTIL_EIGHTCC_LEN) {
		return -1;
	}
	memcpy(wire, s, PM_METAL_UTIL_EIGHTCC_LEN);
	pm_metal_util_eightcc_from_wire_bytes(wire, out);
	return 0;
}

int pm_metal_util_eightcc_label(uint64_t magic, char out[PM_METAL_UTIL_EIGHTCC_LEN + 1])
{
	pm_metal_util_eightcc_t tag;

	if (out == NULL) {
		return -1;
	}
	pm_metal_util_eightcc_from_u64(&tag, magic);
	return pm_metal_util_eightcc_to_string(&tag, out);
}

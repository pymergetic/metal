/*
 * util/endian — host-endian probe fallback (see util/endian.h).
 * Most targets get an inline pm_metal_util_endian_host_is_le() from the header.
 */
#include <pymergetic/metal/util/endian.h>

#ifndef PM_METAL_UTIL_ENDIAN_HOST_IS_LE
int pm_metal_util_endian_host_is_le(void)
{
	static int cached = -1;
	union {
		uint16_t u16;
		uint8_t u8[2];
	} probe;

	if (cached >= 0) {
		return cached;
	}

	probe.u16 = 0x0102u;
	cached = (probe.u8[0] == 0x02u) ? 1 : 0;
	return cached;
}
#endif

/*
 * pm_metal_util_size_* bodies. #include this from exactly one loader .c
 * per binary — never from more than one TU, and never as a substitute for
 * size.h in ordinary callers (they only need the contract, not the body).
 */
#ifndef PYMERGETIC_METAL_UTIL_SIZE_IMPL_H_
#define PYMERGETIC_METAL_UTIL_SIZE_IMPL_H_

#include "pymergetic/metal/util/size.h"

#include <inttypes.h>
#include <stdio.h>

typedef struct pm_metal_util_size_unit {
	uint64_t bytes;
	const char *suffix;
} pm_metal_util_size_unit_t;

int pm_metal_util_size_format(char *out, size_t cap, uint64_t bytes)
{
	if (!out || cap == 0) {
		return -1;
	}

	static const pm_metal_util_size_unit_t units[] = {
		{1024ULL * 1024ULL * 1024ULL * 1024ULL, " TiB"},
		{1024ULL * 1024ULL * 1024ULL, " GiB"},
		{1024ULL * 1024ULL, " MiB"},
		{1024ULL, " KiB"},
	};
	size_t i;

	for (i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
		if (bytes >= units[i].bytes) {
			return snprintf(out, cap, "%" PRIu64 "%s", bytes / units[i].bytes,
					 units[i].suffix);
		}
	}

	return snprintf(out, cap, "%" PRIu64 " B", bytes);
}

int pm_metal_util_size_format_bytes(char *out, size_t cap, uint64_t bytes)
{
	char human[PM_METAL_UTIL_SIZE_FORMAT_MAX];

	if (!out || cap == 0) {
		return -1;
	}
	if (pm_metal_util_size_format(human, sizeof(human), bytes) < 0) {
		return -1;
	}

	return snprintf(out, cap, "%" PRIu64 " (%s)", bytes, human);
}

#endif /* PYMERGETIC_METAL_UTIL_SIZE_IMPL_H_ */

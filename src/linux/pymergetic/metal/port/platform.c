/*
 * Port — linux bind implementations. Memory ops live in
 * src/linux/pymergetic/metal/memory/ instead — see platform.h.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include "pymergetic/metal/memory/bytecode.h"
#include "pymergetic/metal/port/platform.h"

int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len)
{
	FILE *f = fopen(host_path, "rb");
	if (!f) {
		return -1;
	}

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	long size = ftell(f);
	if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	/* bytecode->alloc takes uint32_t; reject before truncating so fread
	 * length can never diverge from the allocated buffer size. */
	if ((unsigned long)size > UINT32_MAX) {
		fclose(f);
		return -1;
	}

	uint32_t len = (uint32_t)size;
	const pm_metal_memory_ops_t *bytecode = pm_metal_memory_bytecode_ops();
	uint8_t *buf = bytecode->alloc(len);
	if (!buf) {
		fclose(f);
		return -1;
	}

	size_t n = fread(buf, 1, (size_t)len, f);
	fclose(f);
	if (n != (size_t)len) {
		bytecode->free(buf);
		return -1;
	}

	*out_buf = buf;
	*out_len = len;
	return 0;
}

int pm_metal_port_file_exists(const char *host_path)
{
	struct stat st;

	return stat(host_path, &st) == 0 && S_ISREG(st.st_mode);
}

int pm_metal_port_write_file(const char *host_path, const uint8_t *data, uint32_t len)
{
	FILE *f;
	size_t n;

	if (!host_path || !host_path[0] || (!data && len > 0)) {
		return -1;
	}
	f = fopen(host_path, "wb");
	if (!f) {
		return -1;
	}
	n = len ? fwrite(data, 1, len, f) : 0;
	if (fclose(f) != 0) {
		return -1;
	}
	return (n == (size_t)len) ? 0 : -1;
}

int pm_metal_port_mkdir(const char *host_path)
{
	char path[PATH_MAX];
	size_t len;
	size_t i;
	struct stat st;

	if (!host_path || !host_path[0]) {
		return -1;
	}
	len = strlen(host_path);
	if (len + 1 > sizeof(path)) {
		return -1;
	}
	memcpy(path, host_path, len + 1);

	/* Walk each '/' boundary and mkdir the prefix (mkdir -p). Skip the
	 * empty component before a leading '/'. */
	for (i = 1; i <= len; i++) {
		if (path[i] != '/' && path[i] != '\0') {
			continue;
		}
		char saved = path[i];

		path[i] = '\0';
		if (stat(path, &st) == 0) {
			if (!S_ISDIR(st.st_mode)) {
				path[i] = saved;
				return -1;
			}
		} else if (mkdir(path, 0755) != 0 && errno != EEXIST) {
			path[i] = saved;
			return -1;
		}
		path[i] = saved;
	}
	return 0;
}

uint64_t pm_metal_port_monotonic_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

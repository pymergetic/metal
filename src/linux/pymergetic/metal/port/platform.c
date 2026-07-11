/*
 * Port — linux bind implementations. Memory ops live in
 * src/linux/pymergetic/metal/memory/ instead — see platform.h.
 */
#include <stdint.h>
#include <stdio.h>

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

	const pm_metal_memory_ops_t *bytecode = pm_metal_memory_bytecode_ops();
	uint8_t *buf = bytecode->alloc((uint32_t)size);
	if (!buf) {
		fclose(f);
		return -1;
	}

	size_t n = fread(buf, 1, (size_t)size, f);
	fclose(f);
	if (n != (size_t)size) {
		bytecode->free(buf);
		return -1;
	}

	*out_buf = buf;
	*out_len = (uint32_t)size;
	return 0;
}

/*
 * Port — zephyr bind implementations.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

#include "pymergetic/metal/memory/bytecode.h"
#include "pymergetic/metal/port/platform.h"

int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len)
{
	struct fs_file_t file;
	struct fs_dirent entry;
	const pm_metal_memory_ops_t *bytecode;
	uint8_t *buf;
	ssize_t n;
	int rc;

	if (!host_path || !out_buf || !out_len) {
		return -1;
	}

	rc = fs_stat(host_path, &entry);
	if (rc < 0 || entry.type != FS_DIR_ENTRY_FILE) {
		return -1;
	}

	bytecode = pm_metal_memory_bytecode_ops();
	buf = bytecode->alloc((uint32_t)entry.size);
	if (!buf && entry.size > 0) {
		return -1;
	}

	fs_file_t_init(&file);
	rc = fs_open(&file, host_path, FS_O_READ);
	if (rc < 0) {
		if (buf) {
			bytecode->free(buf);
		}
		return -1;
	}

	n = entry.size ? fs_read(&file, buf, entry.size) : 0;
	fs_close(&file);
	if (n < 0 || (uint32_t)n != entry.size) {
		if (buf) {
			bytecode->free(buf);
		}
		return -1;
	}

	*out_buf = buf;
	*out_len = (uint32_t)entry.size;
	return 0;
}

int pm_metal_port_file_exists(const char *host_path)
{
	struct fs_dirent entry;

	if (!host_path) {
		return 0;
	}
	return fs_stat(host_path, &entry) == 0 && entry.type == FS_DIR_ENTRY_FILE;
}

int pm_metal_port_write_file(const char *host_path, const uint8_t *data, uint32_t len)
{
	struct fs_file_t file;
	ssize_t n;
	int rc;

	if (!host_path || !host_path[0] || (!data && len > 0)) {
		return -1;
	}

	fs_file_t_init(&file);
	rc = fs_open(&file, host_path, FS_O_CREATE | FS_O_WRITE);
	if (rc < 0) {
		return -1;
	}
	n = len ? fs_write(&file, data, len) : 0;
	fs_close(&file);
	return (n >= 0 && (uint32_t)n == len) ? 0 : -1;
}

int pm_metal_port_mkdir(const char *host_path)
{
	char path[512];
	size_t len;
	size_t i;
	struct fs_dirent entry;
	int rc;

	if (!host_path || !host_path[0]) {
		return -1;
	}
	len = strlen(host_path);
	if (len + 1 > sizeof(path)) {
		return -1;
	}
	memcpy(path, host_path, len + 1);

	for (i = 1; i <= len; i++) {
		if (path[i] != '/' && path[i] != '\0') {
			continue;
		}
		char saved = path[i];

		path[i] = '\0';
		rc = fs_stat(path, &entry);
		if (rc == 0) {
			if (entry.type != FS_DIR_ENTRY_DIR) {
				path[i] = saved;
				return -1;
			}
		} else {
			rc = fs_mkdir(path);
			if (rc < 0 && rc != -EEXIST) {
				path[i] = saved;
				return -1;
			}
		}
		path[i] = saved;
	}
	return 0;
}

uint64_t pm_metal_port_monotonic_ms(void)
{
	return (uint64_t)k_uptime_get();
}

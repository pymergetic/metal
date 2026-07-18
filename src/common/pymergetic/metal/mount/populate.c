/*
 * Boot-time populate — impl: common. See populate.h, docs/MOUNT.md Phase 4.
 */
#include "pymergetic/metal/mount/populate.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/memory/bytecode.h"
#include "pymergetic/metal/mount/table.h"
#include "pymergetic/metal/port/platform.h"
#include "pymergetic/metal/util/lz4.h"
#include "pymergetic/metal/util/tar.h"

#define PM_METAL_MOUNT_POPULATE_MAX 8
#define PM_METAL_MOUNT_POPULATE_CHUNK 4096

typedef struct pm_metal_mount_populate_entry {
	int used;
	const uint8_t *blob;
	size_t blob_len;
	size_t uncompressed_len;
	unsigned flags;
} pm_metal_mount_populate_entry_t;

static pm_metal_mount_populate_entry_t g_pm_metal_mount_populate_table[PM_METAL_MOUNT_POPULATE_MAX];

int pm_metal_mount_populate_register(const uint8_t *blob, size_t blob_len, size_t uncompressed_len,
				      unsigned flags)
{
	int i;

	if (!blob || blob_len == 0) {
		return -1;
	}
	if ((flags & PM_METAL_MOUNT_POPULATE_FLAG_LZ4) && uncompressed_len == 0) {
		return -1;
	}
	for (i = 0; i < PM_METAL_MOUNT_POPULATE_MAX; i++) {
		if (!g_pm_metal_mount_populate_table[i].used) {
			g_pm_metal_mount_populate_table[i].used = 1;
			g_pm_metal_mount_populate_table[i].blob = blob;
			g_pm_metal_mount_populate_table[i].blob_len = blob_len;
			g_pm_metal_mount_populate_table[i].uncompressed_len = uncompressed_len;
			g_pm_metal_mount_populate_table[i].flags = flags;
			return 0;
		}
	}
	fprintf(stderr, "pm_metal_mount: populate: registry full\n");
	return -1;
}

void pm_metal_mount_populate_clear(void)
{
	memset(g_pm_metal_mount_populate_table, 0, sizeof(g_pm_metal_mount_populate_table));
}

/* Build guest path from a tar name; reject ".." (zip-slip) via table normalize. */
static int pm_metal_mount_populate_guest_path(const char *tar_name, char *out, size_t out_cap)
{
	if (!tar_name || !tar_name[0]) {
		return -1;
	}
	if (pm_metal_mount_normalize(tar_name, out, out_cap) != 0) {
		return -1;
	}
	/* bare "/" — nothing to extract */
	if (out[0] == '/' && out[1] == '\0') {
		return -1;
	}
	return 0;
}

static int pm_metal_mount_populate_dirname(const char *host_path, char *out, size_t out_cap)
{
	const char *slash = strrchr(host_path, '/');
	size_t len;

	if (!slash || slash == host_path) {
		/* "/file" → "/" ; no parent to create beyond root */
		if (out_cap < 2) {
			return -1;
		}
		out[0] = '/';
		out[1] = '\0';
		return 0;
	}
	len = (size_t)(slash - host_path);
	if (len + 1 > out_cap) {
		return -1;
	}
	memcpy(out, host_path, len);
	out[len] = '\0';
	return 0;
}

static int pm_metal_mount_populate_extract_tar(const uint8_t *tar, size_t tar_len)
{
	pm_metal_util_tar_iter_t it = { 0 };
	char guest[PM_METAL_MOUNT_GUEST_PATH_MAX];
	char host[PM_METAL_MOUNT_HOST_PATH_MAX];
	char parent[PM_METAL_MOUNT_HOST_PATH_MAX];
	uint8_t chunk[PM_METAL_MOUNT_POPULATE_CHUNK];

	pm_metal_util_tar_iter_init(&it, tar, tar_len);

	for (;;) {
		int rc = pm_metal_util_tar_iter_next(&it);
		char name[PM_METAL_UTIL_TAR_NAME_MAX];
		int is_dir;
		uint64_t size;
		uint64_t got;

		if (rc == 0) {
			pm_metal_util_tar_iter_close(&it);
			return 0;
		}
		if (rc < 0) {
			fprintf(stderr, "pm_metal_mount: populate: malformed tar entry, skipping rest of archive\n");
			pm_metal_util_tar_iter_close(&it);
			return -1;
		}
		if (pm_metal_util_tar_iter_name(&it, name, sizeof(name)) < 0) {
			fprintf(stderr, "pm_metal_mount: populate: bad entry name, skipped\n");
			continue;
		}
		if (pm_metal_mount_populate_guest_path(name, guest, sizeof(guest)) != 0) {
			continue;
		}
		is_dir = pm_metal_util_tar_iter_is_dir(&it);
		if (is_dir < 0) {
			continue;
		}
		if (pm_metal_mount_resolve(guest, host, sizeof(host)) != 0) {
			fprintf(stderr, "pm_metal_mount: populate: resolve failed for %s\n", guest);
			continue;
		}
		if (is_dir) {
			if (pm_metal_port_mkdir(host) != 0) {
				fprintf(stderr, "pm_metal_mount: populate: mkdir failed: %s\n", guest);
			}
			continue;
		}

		size = pm_metal_util_tar_iter_size(&it);
		if (size > UINT32_MAX) {
			fprintf(stderr, "pm_metal_mount: populate: entry too large: %s\n", guest);
			continue;
		}
		if (pm_metal_mount_populate_dirname(host, parent, sizeof(parent)) != 0
		    || pm_metal_port_mkdir(parent) != 0) {
			fprintf(stderr, "pm_metal_mount: populate: parent mkdir failed: %s\n", guest);
			continue;
		}

		/* Assemble the file in one buffer when small enough for a single
		 * write_file(); otherwise stream chunks into a bytecode-arena
		 * buffer then write once — write_file is whole-file today. */
		{
			const pm_metal_memory_ops_t *bytecode = pm_metal_memory_bytecode_ops();
			uint8_t *buf = NULL;
			uint32_t len = (uint32_t)size;

			if (len > 0) {
				buf = bytecode->alloc(len);
				if (!buf) {
					fprintf(stderr, "pm_metal_mount: populate: OOM for %s\n", guest);
					continue;
				}
			}
			got = 0;
			while (got < size) {
				size_t want = (size_t)(size - got);
				int n;

				if (want > sizeof(chunk)) {
					want = sizeof(chunk);
				}
				n = pm_metal_util_tar_iter_read(&it, chunk, want);
				if (n <= 0) {
					fprintf(stderr, "pm_metal_mount: populate: truncated entry: %s\n", guest);
					if (buf) {
						bytecode->free(buf);
					}
					buf = NULL;
					break;
				}
				memcpy(buf + got, chunk, (size_t)n);
				got += (uint64_t)n;
			}
			if (!buf && len > 0) {
				continue;
			}
			/* Zero-length files: write_file must not see a NULL buf. */
			if (len == 0) {
				static const uint8_t empty;
				if (pm_metal_port_write_file(host, &empty, 0) != 0) {
					fprintf(stderr, "pm_metal_mount: populate: write failed: %s\n", guest);
				}
			} else if (pm_metal_port_write_file(host, buf, len) != 0) {
				fprintf(stderr, "pm_metal_mount: populate: write failed: %s\n", guest);
			}
			if (buf) {
				bytecode->free(buf);
			}
		}
	}
}

int pm_metal_mount_populate_extract(const uint8_t *blob, size_t blob_len, size_t uncompressed_len,
				     unsigned flags)
{
	const pm_metal_memory_ops_t *bytecode = pm_metal_memory_bytecode_ops();
	const uint8_t *tar;
	size_t tar_len;
	uint8_t *decomp = NULL;

	if (!blob || blob_len == 0) {
		return -1;
	}
	if (flags & PM_METAL_MOUNT_POPULATE_FLAG_LZ4) {
		int n;

		if (uncompressed_len == 0 || uncompressed_len > UINT32_MAX) {
			fprintf(stderr, "pm_metal_mount: populate: lz4 uncompressed bad/too large\n");
			return -1;
		}
		decomp = bytecode->alloc((uint32_t)uncompressed_len);
		if (!decomp) {
			fprintf(stderr, "pm_metal_mount: populate: OOM decompressing archive\n");
			return -1;
		}
		n = pm_metal_util_lz4_decompress(blob, blob_len, decomp, uncompressed_len);
		if (n < 0 || (size_t)n != uncompressed_len) {
			fprintf(stderr, "pm_metal_mount: populate: lz4 decompress failed\n");
			bytecode->free(decomp);
			return -1;
		}
		tar = decomp;
		tar_len = uncompressed_len;
	} else {
		tar = blob;
		tar_len = blob_len;
	}

	pm_metal_mount_populate_extract_tar(tar, tar_len);

	if (decomp) {
		bytecode->free(decomp);
	}
	return 0;
}

int pm_metal_mount_populate_all(void)
{
	int i;

	for (i = 0; i < PM_METAL_MOUNT_POPULATE_MAX; i++) {
		const pm_metal_mount_populate_entry_t *e = &g_pm_metal_mount_populate_table[i];

		if (!e->used) {
			continue;
		}
		(void)pm_metal_mount_populate_extract(e->blob, e->blob_len, e->uncompressed_len, e->flags);
	}
	return 0;
}

/*
 * T3 — exercises util/{size,log,arena,lz4,tar}.h's wasi-style imports: on
 * wasm32-wasip1 none of these have a local body in this .wasm at all (see
 * their own headers) — every call below is resolved against each host
 * module's own native registration (src/common/pymergetic/metal/util/
 * {size,arena,log,lz4,tar}.c, each under its own PM_METAL_UTIL_{SIZE,ARENA,
 * LOG,LZ4,TAR}_WASI_MODULE import name, see util/wasi.h) at instantiate()
 * time. `arena_init()` is called over this mod's *own* stack buffer, and
 * lz4_compress()/decompress() round-trip through two more of this mod's
 * own stack buffers, to prove the host really is reading/writing this
 * guest's linear memory through the app<->native address translation,
 * not some unrelated host-side buffer. The tar+lz4 block at the bottom
 * combines both: build a small archive with util/tar.h's writer, lz4-
 * compress it, then decompress + walk it back with util/tar.h's iter —
 * the two headers never call into each other, only this mod composes
 * them, exactly per their own "independent leaf utils" contract.
 */
#include <pymergetic/metal/util/arena.h>
#include <pymergetic/metal/util/log.h>
#include <pymergetic/metal/util/lz4.h>
#include <pymergetic/metal/util/size.h>
#include <pymergetic/metal/util/tar.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	char human[PM_METAL_UTIL_SIZE_FORMAT_MAX];
	pm_metal_util_size_format(human, sizeof(human), 92946432ULL);
	printf("t3_util_native: size=%s\n", human);

	pm_metal_util_log_set_level(PM_METAL_LOG_WARN);
	char name[8];
	pm_metal_util_log_level_name(pm_metal_util_log_get_level(), name, sizeof(name));
	printf("t3_util_native: level=%s\n", name);

	/* Below the floor just set — must be a silent no-op, not an error.
	 * The ERROR line is intentional (proves the floor); not a failure. */
	pm_metal_util_log_write(PM_METAL_LOG_STREAM_STDOUT, PM_METAL_LOG_INFO, "should not appear");
	pm_metal_util_log_write(PM_METAL_LOG_STREAM_STDOUT, PM_METAL_LOG_ERROR,
				"t3_util_native: at/above floor (expected)");
	pm_metal_util_log_write_raw(PM_METAL_LOG_STREAM_STDOUT, "t3_util_native: raw, unfiltered");

	static unsigned char buf[256];
	pm_metal_util_arena_t *arena = pm_metal_util_arena_init(buf, sizeof(buf));
	if (!arena) {
		printf("t3_util_native: arena_init failed\n");
		return 1;
	}

	void *a = pm_metal_util_arena_alloc(arena, 32);
	void *b = pm_metal_util_arena_alloc(arena, 32);
	if (!a || !b) {
		printf("t3_util_native: arena_alloc failed\n");
		return 1;
	}

	/* Prove the host really did write through into *this* module's own
	 * memory — not into some unrelated host-side buffer. */
	memset(a, 0xAB, 32);
	printf("t3_util_native: a[0]=0x%02x used=%zu\n", ((unsigned char *)a)[0],
	       pm_metal_util_arena_used(arena));

	pm_metal_util_arena_free(arena, a);
	pm_metal_util_arena_free(arena, b);
	printf("t3_util_native: used_after_free=%zu\n", pm_metal_util_arena_used(arena));

	static const char plain[] = "the quick brown fox jumps over the lazy dog, "
				     "the quick brown fox jumps over the lazy dog";
	size_t bound = pm_metal_util_lz4_compress_bound(sizeof(plain));
	static unsigned char packed[256];
	if (bound == 0 || bound > sizeof(packed)) {
		printf("t3_util_native: lz4_compress_bound failed\n");
		return 1;
	}

	int packed_len = pm_metal_util_lz4_compress(plain, sizeof(plain), packed, sizeof(packed));
	if (packed_len < 0) {
		printf("t3_util_native: lz4_compress failed\n");
		return 1;
	}
	printf("t3_util_native: lz4 %zu -> %d bytes\n", sizeof(plain), packed_len);

	static char unpacked[sizeof(plain)];
	int unpacked_len =
		pm_metal_util_lz4_decompress(packed, (size_t)packed_len, unpacked, sizeof(unpacked));
	if (unpacked_len != (int)sizeof(plain) || memcmp(unpacked, plain, sizeof(plain)) != 0) {
		printf("t3_util_native: lz4_decompress mismatch\n");
		return 1;
	}
	printf("t3_util_native: lz4 round-trip ok\n");

	/* Build a 2-entry archive (one dir, one file — the file's data split
	 * across two put_data() calls, proving the chunked-write path). */
	static unsigned char archive[4096];
	pm_metal_util_tar_writer_t w;
	pm_metal_util_tar_writer_init(&w, archive, sizeof(archive));

	if (pm_metal_util_tar_writer_put_header(&w, "data/", 0, 1) != 0) {
		printf("t3_util_native: tar put_header(dir) failed\n");
		return 1;
	}

	static const char half1[] = "the quick brown fox jumps over the lazy dog, ";
	static const char half2[] = "the quick brown fox jumps over the lazy dog";
	size_t file_len = sizeof(half1) - 1 + sizeof(half2) - 1;

	if (pm_metal_util_tar_writer_put_header(&w, "data/quote.txt", file_len, 0) != 0
	    || pm_metal_util_tar_writer_put_data(&w, half1, sizeof(half1) - 1) != 0
	    || pm_metal_util_tar_writer_put_data(&w, half2, sizeof(half2) - 1) != 0) {
		printf("t3_util_native: tar put_data failed\n");
		return 1;
	}

	int64_t archive_len = pm_metal_util_tar_writer_finish(&w);
	if (archive_len <= 0 || (size_t)archive_len > sizeof(archive)) {
		printf("t3_util_native: tar writer_finish failed\n");
		return 1;
	}
	printf("t3_util_native: tar wrote %" PRId64 " bytes (2 entries)\n", archive_len);

	/* lz4-compress the archive, same as any other buffer — tar.h has no
	 * idea lz4.h exists, this mod is the only thing composing them. */
	size_t archive_bound = pm_metal_util_lz4_compress_bound((size_t)archive_len);
	static unsigned char archive_packed[4096];
	if (archive_bound == 0 || archive_bound > sizeof(archive_packed)) {
		printf("t3_util_native: tar archive compress_bound failed\n");
		return 1;
	}
	int archive_packed_len =
		pm_metal_util_lz4_compress(archive, (size_t)archive_len, archive_packed, sizeof(archive_packed));
	if (archive_packed_len < 0) {
		printf("t3_util_native: tar archive compress failed\n");
		return 1;
	}
	printf("t3_util_native: tar archive lz4 %" PRId64 " -> %d bytes\n", archive_len,
	       archive_packed_len);

	static unsigned char archive_unpacked[4096];
	int archive_unpacked_len = pm_metal_util_lz4_decompress(
		archive_packed, (size_t)archive_packed_len, archive_unpacked, sizeof(archive_unpacked));
	if (archive_unpacked_len != (int)archive_len
	    || memcmp(archive_unpacked, archive, (size_t)archive_len) != 0) {
		printf("t3_util_native: tar archive decompress mismatch\n");
		return 1;
	}

	/* Walk the decompressed archive back with the iterator side. */
	pm_metal_util_tar_iter_t it;
	pm_metal_util_tar_iter_init(&it, archive_unpacked, (size_t)archive_unpacked_len);

	int rc;
	int entries = 0;
	char entry_name[PM_METAL_UTIL_TAR_NAME_MAX];
	static char entry_data[512];

	while ((rc = pm_metal_util_tar_iter_next(&it)) == 1) {
		entries++;
		if (pm_metal_util_tar_iter_name(&it, entry_name, sizeof(entry_name)) < 0) {
			printf("t3_util_native: tar iter_name failed\n");
			return 1;
		}
		uint64_t entry_size = pm_metal_util_tar_iter_size(&it);
		int is_dir = pm_metal_util_tar_iter_is_dir(&it);

		printf("t3_util_native: tar entry name=%s size=%" PRIu64 " is_dir=%d\n", entry_name,
		       entry_size, is_dir);

		if (!is_dir) {
			if (entry_size > sizeof(entry_data)) {
				printf("t3_util_native: tar entry too big\n");
				return 1;
			}
			size_t got = 0;
			while (got < entry_size) {
				int n = pm_metal_util_tar_iter_read(&it, entry_data + got,
								     sizeof(entry_data) - got);
				if (n <= 0) {
					printf("t3_util_native: tar iter_read failed\n");
					return 1;
				}
				got += (size_t)n;
			}
			if (got != file_len || memcmp(entry_data, half1, sizeof(half1) - 1) != 0
			    || memcmp(entry_data + sizeof(half1) - 1, half2, sizeof(half2) - 1) != 0) {
				printf("t3_util_native: tar entry data mismatch\n");
				return 1;
			}
		}
	}
	if (rc != 0 || entries != 2) {
		printf("t3_util_native: tar iteration failed (rc=%d entries=%d)\n", rc, entries);
		return 1;
	}
	printf("t3_util_native: tar+lz4 round-trip ok\n");

	return 0;
}

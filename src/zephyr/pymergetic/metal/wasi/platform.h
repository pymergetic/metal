/*
 * Zephyr WASI file platform types — os_file_handle is int (fd index) to match
 * WAMR platform_internal.h / libc-wasi ABI.
 * header-only — types/inlines; no separate .c (consumed by wasi/file.c).
 */
#ifndef PM_METAL_ZEPHYR_WASI_PLATFORM_H_
#define PM_METAL_ZEPHYR_WASI_PLATFORM_H_

#include <stdbool.h>
#include <stdint.h>

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#include <unistd.h>
#include <zephyr/fs/fs.h>

#ifndef CONFIG_ZVFS_OPEN_MAX
#define CONFIG_ZVFS_OPEN_MAX 16
#endif

#ifndef MAX_FILE_NAME
#define MAX_FILE_NAME 255
#endif

#ifndef CONFIG_WASI_MAX_OPEN_FILES
#define CONFIG_WASI_MAX_OPEN_FILES CONFIG_ZVFS_OPEN_MAX
#endif

typedef struct zephyr_fs_desc {
	char *path;
	union {
		struct fs_file_t file;
		struct fs_dir_t dir;
	};
	bool is_dir;
	bool used;
	bool is_sock;
	bool dir_opened; /* true after successful fs_opendir; skip double-open */
	uint32_t dir_index;
} zephyr_fs_desc;

typedef int os_file_handle;
typedef int os_dir_stream;
typedef int os_raw_file_handle;

#ifndef OS_DIR_STREAM_INVALID
#define OS_DIR_STREAM_INVALID (-1)
#endif

static inline os_file_handle
os_get_invalid_handle(void)
{
	return -1;
}

typedef uint8_t wasi_libc_file_access_mode;
#define WASI_LIBC_ACCESS_MODE_READ_ONLY 0
#define WASI_LIBC_ACCESS_MODE_WRITE_ONLY 1
#define WASI_LIBC_ACCESS_MODE_READ_WRITE 2

#endif /* PM_METAL_ZEPHYR_WASI_PLATFORM_H_ */

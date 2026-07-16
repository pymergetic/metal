/*
 * Copyright (C) 2024 Grenoble INP - ESISAR.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "pymergetic/metal/wasi/platform.h"

#include "platform_wasi_types.h"
#include "libc_errno.h"

/* Declared by -DBH_MALLOC/-DBH_FREE=wasm_runtime_* from the WAMR build. */
void *wasm_runtime_malloc(unsigned int size);
void wasm_runtime_free(void *ptr);

__wasi_errno_t os_fsync(os_file_handle handle);

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_interface.h>
#include <zephyr/fs/fs_sys.h>
#include <zephyr/posix/unistd.h>

#include "pymergetic/metal/mount/proc.h"
#include "pymergetic/metal/port/pipe_zephyr.h"
#include "pymergetic/metal/wasi/socket.h"

/* Notes:
 * This is the implementation of a POSIX-like file system interface for Zephyr.
 * To manage our file descriptors, we created a struct `zephyr_fs_desc` that
 * represent a zephyr file descriptor and hold useful informations.
 * We also created a file descriptor table to keep track of all the file
 * descriptors.
 *
 * To pass the file descriptor reference to the higher level abstraction, we
 * pass the index of the fd table to an `os_file_handle` struct.
 * Then in the WASI implementation layer we can retrieve the file descriptor
 * reference.
 *
 * We also fake the stdin, stdout and stderr file descriptors.
 * We do not handle write on stdin and read on stdin, stdout and stderr.
 */

// No OS API wrapper (Zephyr):
// file:
//     off_t fs_tell(struct fs_file_t *zfp)
// file system:
//     int fs_mount(struct fs_mount_t *mp)
//     int fs_unmount(struct fs_mount_t *mp
//     int fs_readmount(int *index, const char **name)
//     int fs_statvfs(const char *path, struct fs_statvfs *stat)
//     int fs_mkfs(int fs_type, uintptr_t dev_id, void *cfg, int flags)
//     int fs_register(int type, const struct fs_file_system_t *fs)
//     int fs_unregister(int type, const struct fs_file_system_t *fs)

/* CONFIG_WASI_MAX_OPEN_FILES comes from platform.h (→ CONFIG_ZVFS_OPEN_MAX). */

static inline bool
os_is_virtual_fd(int fd)
{
    switch (fd) {
        case STDIN_FILENO:
        case STDOUT_FILENO:
        case STDERR_FILENO:
            return true;
        default:
            return false;
    };
}

// Array to keep track of file system descriptors.
static struct zephyr_fs_desc desc_array[CONFIG_WASI_MAX_OPEN_FILES];

// mutex to protect the file descriptor array
K_MUTEX_DEFINE(desc_array_mutex);

/* Acquire a live (non-closing) slot for an op. Pair with pm_metal_desc_release.
 * Virtual stdio fds set *out = NULL and return 0 (same as legacy macro). */
static int
pm_metal_desc_acquire(os_file_handle fd, struct zephyr_fs_desc **out)
{
	struct zephyr_fs_desc *ptr;

	if (out == NULL) {
		return -1;
	}
	if (os_is_virtual_fd(fd)) {
		*out = NULL;
		return 0;
	}
	if (fd < 3 || fd >= CONFIG_WASI_MAX_OPEN_FILES + 3) {
		return -1;
	}
	k_mutex_lock(&desc_array_mutex, K_FOREVER);
	ptr = &desc_array[(int)fd - 3];
	if (!ptr->used || ptr->closing) {
		k_mutex_unlock(&desc_array_mutex);
		return -1;
	}
	ptr->refs++;
	*out = ptr;
	k_mutex_unlock(&desc_array_mutex);
	return 0;
}

/* Finish FS close + free path after the slot was cleared under the mutex. */
static void
pm_metal_desc_teardown(char *path, int is_dir, int dir_opened,
		       struct fs_file_t *file, struct fs_dir_t *dir)
{
	if (is_dir) {
		if (dir_opened && dir != NULL) {
			(void)fs_closedir(dir);
		}
	} else if (file != NULL) {
		(void)fs_close(file);
	}
	if (path != NULL) {
		BH_FREE(path);
	}
}

static void
pm_metal_desc_release(struct zephyr_fs_desc *ptr)
{
	char *path = NULL;
	int is_dir = 0;
	int dir_opened = 0;
	int do_teardown = 0;
	struct fs_file_t file;
	struct fs_dir_t dir;

	if (ptr == NULL) {
		return;
	}
	k_mutex_lock(&desc_array_mutex, K_FOREVER);
	if (ptr->refs > 0) {
		ptr->refs--;
	}
	if (ptr->closing && ptr->refs == 0) {
		path = ptr->path;
		is_dir = ptr->is_dir;
		dir_opened = ptr->dir_opened;
		if (is_dir) {
			dir = ptr->dir;
		} else {
			file = ptr->file;
		}
		ptr->path = NULL;
		ptr->dir_opened = false;
		ptr->closing = false;
		ptr->used = false;
		ptr->is_dir = false;
		do_teardown = 1;
	}
	k_mutex_unlock(&desc_array_mutex);
	if (do_teardown) {
		pm_metal_desc_teardown(path, is_dir, dir_opened, &file, &dir);
	}
}

/* Mark closing; tear down now if no in-flight ops (else last release does). */
static __wasi_errno_t
pm_metal_desc_close(os_file_handle fd)
{
	struct zephyr_fs_desc *ptr;
	char *path = NULL;
	int is_dir = 0;
	int dir_opened = 0;
	int do_teardown = 0;
	struct fs_file_t file;
	struct fs_dir_t dir;

	if (fd < 3 || fd >= CONFIG_WASI_MAX_OPEN_FILES + 3) {
		return __WASI_EBADF;
	}
	k_mutex_lock(&desc_array_mutex, K_FOREVER);
	ptr = &desc_array[(int)fd - 3];
	if (!ptr->used || ptr->closing) {
		k_mutex_unlock(&desc_array_mutex);
		return __WASI_EBADF;
	}
	ptr->closing = true;
	if (ptr->refs == 0) {
		path = ptr->path;
		is_dir = ptr->is_dir;
		dir_opened = ptr->dir_opened;
		if (is_dir) {
			dir = ptr->dir;
		} else {
			file = ptr->file;
		}
		ptr->path = NULL;
		ptr->dir_opened = false;
		ptr->closing = false;
		ptr->used = false;
		ptr->is_dir = false;
		do_teardown = 1;
	}
	k_mutex_unlock(&desc_array_mutex);
	if (do_teardown) {
		/* Surface fs_close/fs_closedir errors; path is always freed. */
		int rc = 0;

		if (is_dir) {
			if (dir_opened) {
				rc = fs_closedir(&dir);
			}
		} else {
			rc = fs_close(&file);
		}
		if (path != NULL) {
			BH_FREE(path);
		}
		if (rc < 0) {
			return convert_errno(-rc);
		}
	}
	return __WASI_ESUCCESS;
}

// fd's 0-2 are reserved for standard streams, hence the by-3 offsets.
#define GET_FILE_SYSTEM_DESCRIPTOR(fd, ptr)                   \
    do {                                                      \
        if (pm_metal_desc_acquire((fd), &(ptr)) != 0) {       \
            return __WASI_EBADF;                              \
        }                                                     \
    } while (0)

#define RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr) pm_metal_desc_release(ptr)

static char prestat_dir[MAX_FILE_NAME + 1];

/*
 * Guest paths (even with a leading '/') are VFS-relative. Host root is
 * /RAM: (FAT volume), not /, so never treat a leading slash as a host
 * absolute path — join under the dirfd base / first preopen instead.
 */
static bool
join_host_path(const char *base, const char *path, char *abs_path, size_t abs_path_len)
{
	const char *rel = path;
	size_t len1;
	size_t len2;

	if (!path || !abs_path || abs_path_len == 0) {
		return false;
	}
	while (rel[0] == '/') {
		rel++;
	}
	if (!base || base[0] == '\0') {
		abs_path[0] = '\0';
		return false;
	}
	if (rel[0] == '\0') {
		len1 = strlen(base);
		if (len1 + 1 > abs_path_len) {
			abs_path[0] = '\0';
			return false;
		}
		memcpy(abs_path, base, len1 + 1);
		return true;
	}

	len1 = strlen(base);
	len2 = strlen(rel);
	if (len1 + 1 + len2 + 1 > abs_path_len) {
		abs_path[0] = '\0';
		return false;
	}
	if (base[len1 - 1] == '/') {
		snprintf(abs_path, abs_path_len, "%s%s", base, rel);
	} else {
		snprintf(abs_path, abs_path_len, "%s/%s", base, rel);
	}
	return true;
}

bool
build_absolute_path(char *abs_path, size_t abs_path_len, const char *path)
{
	return join_host_path(prestat_dir, path, abs_path, abs_path_len);
}

static bool
pm_metal_wasi_is_proc_sentinel(const char *path)
{
	return path
	       && (pm_metal_mount_proc_is_sentinel(path)
		   || strcmp(path, "pm-metal:proc/self") == 0);
}

static void
pm_metal_wasi_fill_dir_stat(struct __wasi_filestat_t *buf)
{
	buf->st_dev = 0;
	buf->st_ino = 1;
	buf->st_filetype = __WASI_FILETYPE_DIRECTORY;
	buf->st_nlink = 1;
	buf->st_size = 0;
	buf->st_atim = 0;
	buf->st_mtim = 0;
	buf->st_ctim = 0;
}

/*
 * Resolve a path relative to a /proc or /proc/self sentinel for fstatat.
 * Never concatenate onto the sentinel — it is not a host filesystem path.
 */
static __wasi_errno_t
pm_metal_wasi_fstatat_proc(const char *parent_path, const char *path,
			   struct __wasi_filestat_t *buf)
{
	pm_metal_mount_proc_hook_fn fn = NULL;
	int is_self;
	const char *node = path;
	char *gen;
	size_t len = 0;

	if (!parent_path || !buf) {
		return __WASI_EINVAL;
	}
	is_self = (strcmp(parent_path, "pm-metal:proc/self") == 0);
	if (!node) {
		return __WASI_EINVAL;
	}
	while (node[0] == '/') {
		node++;
	}
	if (node[0] == '\0' || strcmp(node, ".") == 0) {
		pm_metal_wasi_fill_dir_stat(buf);
		return __WASI_ESUCCESS;
	}
	if (!is_self && strcmp(node, "self") == 0) {
		pm_metal_wasi_fill_dir_stat(buf);
		return __WASI_ESUCCESS;
	}
	if (!is_self && strncmp(node, "self/", 5) == 0) {
		node += 5;
		is_self = 1;
		if (node[0] == '\0' || strcmp(node, ".") == 0) {
			pm_metal_wasi_fill_dir_stat(buf);
			return __WASI_ESUCCESS;
		}
	}
	if (is_self) {
		if (strcmp(node, "cmdline") == 0) {
			fn = pm_metal_mount_proc_generate_cmdline;
		} else if (strcmp(node, "environ") == 0) {
			fn = pm_metal_mount_proc_generate_environ;
		}
	} else if (pm_metal_mount_proc_lookup(node, &fn) != 0) {
		fn = NULL;
	}
	if (!fn) {
		return __WASI_ENOENT;
	}

	gen = BH_MALLOC(PM_METAL_MOUNT_PROC_CONTENT_MAX);
	if (!gen) {
		return __WASI_ENOMEM;
	}
	if (fn(gen, PM_METAL_MOUNT_PROC_CONTENT_MAX, &len) != 0) {
		BH_FREE(gen);
		return __WASI_EIO;
	}
	BH_FREE(gen);

	buf->st_dev = 0;
	buf->st_ino = 1;
	buf->st_filetype = __WASI_FILETYPE_REGULAR_FILE;
	buf->st_nlink = 1;
	buf->st_size = (__wasi_filesize_t)len;
	buf->st_atim = 0;
	buf->st_mtim = 0;
	buf->st_ctim = 0;
	return __WASI_ESUCCESS;
}

static bool
resolve_open_path(os_file_handle handle, const char *path, char *abs_path, size_t abs_path_len)
{
	char base_buf[MAX_FILE_NAME + 1];
	const char *base = prestat_dir;

	if (path == NULL) {
		abs_path[0] = '\0';
		return false;
	}

	if (handle >= 3 && !os_is_virtual_fd(handle)) {
		struct zephyr_fs_desc *parent = NULL;
		int fd = handle;

		if (fd >= 3 && fd < CONFIG_WASI_MAX_OPEN_FILES + 3) {
			k_mutex_lock(&desc_array_mutex, K_FOREVER);
			parent = &desc_array[fd - 3];
			if (parent->used && parent->path != NULL) {
				strncpy(base_buf, parent->path, MAX_FILE_NAME);
				base_buf[MAX_FILE_NAME] = '\0';
				base = base_buf;
			}
			k_mutex_unlock(&desc_array_mutex);
		}
	}

	return join_host_path(base, path, abs_path, abs_path_len);
}

static struct zephyr_fs_desc *
zephyr_fs_alloc_obj(bool is_dir, const char *path, int *index)
{
    struct zephyr_fs_desc *ptr = NULL;

    if (index == NULL) {
        return NULL;
    }
    *index = -1; // give a default value to index in case table is full
    if (path == NULL) {
        return NULL;
    }

    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    for (int i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        if (desc_array[i].used == false) {
            ptr = &desc_array[i];
            ptr->used = true;
            ptr->is_dir = is_dir;
            ptr->dir_opened = false;
            ptr->closing = false;
            ptr->refs = 0;
            ptr->dir_index = 0;
            size_t path_len = strlen(path) + 1;
            ptr->path = BH_MALLOC(path_len);
            if (ptr->path == NULL) {
                ptr->used = false;
                k_mutex_unlock(&desc_array_mutex);
                return NULL;
            }
            strcpy(ptr->path, path);
            *index = i + 3;
            break;
        }
    }

    k_mutex_unlock(&desc_array_mutex);

    if (ptr == NULL) {
        printk("Error: all file descriptor slots are in use (max = %d)\n",
               CONFIG_WASI_MAX_OPEN_FILES);
    }

    return ptr;
}

/* Immediate free for failed-open cleanup (slot never handed to a guest op). */
static inline void
zephyr_fs_free_obj(struct zephyr_fs_desc *ptr)
{
    BH_FREE(ptr->path);
    ptr->path = NULL;
    ptr->dir_opened = false;
    ptr->closing = false;
    ptr->refs = 0;
    ptr->used = false;
}

/* /!\ Needed for socket to work */
__wasi_errno_t
os_fstat(os_file_handle handle, struct __wasi_filestat_t *buf)
{
	struct zephyr_fs_desc *ptr = NULL;
	int rc;

	if (os_is_virtual_fd(handle)) {
		buf->st_filetype = __WASI_FILETYPE_CHARACTER_DEVICE;
		buf->st_size = 0;
		buf->st_atim = 0;
		buf->st_mtim = 0;
		buf->st_ctim = 0;
		return __WASI_ESUCCESS;
	}

	if (pm_metal_wasi_socket_is_ours(handle)) {
		int is_tcp = pm_metal_wasi_socket_is_tcp(handle);

		memset(buf, 0, sizeof(*buf));
		buf->st_filetype = (is_tcp == 0) ? __WASI_FILETYPE_SOCKET_DGRAM
						 : __WASI_FILETYPE_SOCKET_STREAM;
		buf->st_nlink = 1;
		return __WASI_ESUCCESS;
	}

	GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

	if (pm_metal_wasi_is_proc_sentinel(ptr->path)) {
		pm_metal_wasi_fill_dir_stat(buf);
		RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
		return __WASI_ESUCCESS;
	}

	struct fs_dirent entry;

	rc = fs_stat(ptr->path, &entry);
	if (rc < 0) {
		RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
		return convert_errno(-rc);
	}

	buf->st_dev = 0;
	buf->st_ino = 0;
	buf->st_filetype = entry.type == FS_DIR_ENTRY_DIR ? __WASI_FILETYPE_DIRECTORY
							 : __WASI_FILETYPE_REGULAR_FILE;
	buf->st_nlink = 1;
	buf->st_size = entry.size;
	buf->st_atim = 0;
	buf->st_mtim = 0;
	buf->st_ctim = 0;

	RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
	return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fstatat(os_file_handle handle, const char *path,
           struct __wasi_filestat_t *buf, __wasi_lookupflags_t lookup_flags)
{
    struct fs_dirent entry;
    int rc;
    char abs_path[MAX_FILE_NAME + 1];

    (void)lookup_flags;

    if (handle < 0) {
        return __WASI_EBADF;
    }

	/* Virtual /proc — never join remainder onto the sentinel for fs_stat. */
	if (handle >= 3 && !os_is_virtual_fd(handle)
	    && handle < CONFIG_WASI_MAX_OPEN_FILES + 3) {
		char parent_path[MAX_FILE_NAME + 1];
		int is_proc = 0;

		k_mutex_lock(&desc_array_mutex, K_FOREVER);
		{
			struct zephyr_fs_desc *parent = &desc_array[handle - 3];

			if (parent->used && parent->path
			    && pm_metal_wasi_is_proc_sentinel(parent->path)) {
				strncpy(parent_path, parent->path, MAX_FILE_NAME);
				parent_path[MAX_FILE_NAME] = '\0';
				is_proc = 1;
			}
		}
		k_mutex_unlock(&desc_array_mutex);
		if (is_proc) {
			return pm_metal_wasi_fstatat_proc(parent_path, path, buf);
		}
	}

    if (!resolve_open_path(handle, path, abs_path, sizeof(abs_path))) {
        return __WASI_ENOMEM;
    }

    if (pm_metal_wasi_is_proc_sentinel(abs_path)) {
	pm_metal_wasi_fill_dir_stat(buf);
	return __WASI_ESUCCESS;
    }

    // Get file information using Zephyr's fs_stat function
    rc = fs_stat(abs_path, &entry);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    // Fill in the __wasi_filestat_t structure
    buf->st_dev = 0; // Zephyr's fs_stat doesn't provide a device ID
    // DSK: setting this to 0, in addition to d_ino = 1 causes failures with
    // readdir() So, here's a hack to to avoid this.
    buf->st_ino = 1; // Zephyr's fs_stat doesn't provide an inode number.
    buf->st_filetype = entry.type == FS_DIR_ENTRY_DIR
                           ? __WASI_FILETYPE_DIRECTORY
                           : __WASI_FILETYPE_REGULAR_FILE;
    buf->st_nlink = 1; // Zephyr's fs_stat doesn't provide a link count
    buf->st_size = entry.size;
    buf->st_atim = 0; // Zephyr's fs_stat doesn't provide timestamps
    buf->st_mtim = 0;
    buf->st_ctim = 0;

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_file_get_fdflags(os_file_handle handle, __wasi_fdflags_t *flags)
{
    struct zephyr_fs_desc *ptr = NULL;

    if (os_is_virtual_fd(handle)) {
        *flags = 0;
        return __WASI_ESUCCESS;
    }

    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    if ((ptr->file.flags & FS_O_APPEND) != 0) {
        *flags |= __WASI_FDFLAG_APPEND;
    }
    /* Others flags:
     *     - __WASI_FDFLAG_DSYNC
     *     - __WASI_FDFLAG_RSYNC
     *     - __WASI_FDFLAG_SYNC
     *     - __WASI_FDFLAG_NONBLOCK
     * Have no equivalent in Zephyr.
     */
    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_file_set_fdflags(os_file_handle handle, __wasi_fdflags_t flags)
{
    if (os_is_virtual_fd(handle)) {
        return __WASI_ESUCCESS;
    }

    struct zephyr_fs_desc *ptr = NULL;

    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    if ((flags & __WASI_FDFLAG_APPEND) != 0) {
        ptr->file.flags |= FS_O_APPEND;
    }
    /* Same as above */
    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fdatasync(os_file_handle handle)
{
    return os_fsync(handle);
}

__wasi_errno_t
os_fsync(os_file_handle handle)
{
    if (os_is_virtual_fd(handle)) {
        return __WASI_ESUCCESS;
    }

    struct zephyr_fs_desc *ptr = NULL;
    int rc = 0;

    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    if (ptr->is_dir) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_EISDIR;
    }

    rc = fs_sync(&ptr->file);
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }

    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_open_preopendir(const char *path, os_file_handle *out)
{
	int rc, index;
	struct zephyr_fs_desc *ptr;

	if (path == NULL || out == NULL) {
		return __WASI_EINVAL;
	}

	if (pm_metal_wasi_is_proc_sentinel(path)) {
		ptr = zephyr_fs_alloc_obj(true, path, &index);
		if (ptr == NULL) {
			return __WASI_EMFILE;
		}
		*out = index;
		return __WASI_ESUCCESS;
	}

	ptr = zephyr_fs_alloc_obj(true, path, &index);
	if (ptr == NULL) {
		return __WASI_EMFILE;
	}

	fs_dir_t_init(&ptr->dir);

	rc = fs_opendir(&ptr->dir, path);
	if (rc < 0) {
		zephyr_fs_free_obj(ptr);
		return convert_errno(-rc);
	}
	ptr->dir_opened = true;

	*out = index;

	/* Keep first preopen as fallback base; each desc stores its own path. */
	k_mutex_lock(&desc_array_mutex, K_FOREVER);
	if (prestat_dir[0] == '\0') {
		strncpy(prestat_dir, path, MAX_FILE_NAME + 1);
		prestat_dir[MAX_FILE_NAME] = '\0';
	}
	k_mutex_unlock(&desc_array_mutex);

	return __WASI_ESUCCESS;
}

static int
wasi_flags_to_zephyr(__wasi_oflags_t oflags, __wasi_fdflags_t fd_flags,
                     __wasi_lookupflags_t lookup_flags,
                     wasi_libc_file_access_mode access_mode)
{
    int mode = 0;

    // Convert open flags.
    if ((oflags & __WASI_O_CREAT) != 0) {
        mode |= FS_O_CREATE;
    }
    if ((oflags & __WASI_O_TRUNC) != 0) {
        mode |= FS_O_TRUNC;
    }
    if (((oflags & __WASI_O_EXCL) != 0) || ((oflags & __WASI_O_DIRECTORY) != 0)) {
        /* Zephyr FS has no EXCL; DIRECTORY must not open as a file. */
        // TODO: log warning / reject DIRECTORY opens
    }

    // Convert file descriptor flags.
    if ((fd_flags & __WASI_FDFLAG_APPEND) != 0) {
        mode |= FS_O_APPEND;
    }
    if (((fd_flags & __WASI_FDFLAG_DSYNC) != 0)
        || ((fd_flags & __WASI_FDFLAG_RSYNC) != 0)
        || ((fd_flags & __WASI_FDFLAG_SYNC) != 0)
        || ((fd_flags & __WASI_FDFLAG_NONBLOCK) != 0)) {
        /* Zephyr is not POSIX no equivalent for these flags */
        // TODO: log warning
    }

	/* Zephyr FAT has no symlink open semantics; ignore lookup flags. */
	(void)lookup_flags;

	switch (access_mode) {
	case WASI_LIBC_ACCESS_MODE_READ_WRITE:
		mode |= FS_O_RDWR;
		break;
	case WASI_LIBC_ACCESS_MODE_READ_ONLY:
		mode |= FS_O_READ;
		break;
	case WASI_LIBC_ACCESS_MODE_WRITE_ONLY:
		mode |= FS_O_WRITE;
		break;
	default:
		mode |= FS_O_READ;
		break;
	}

	if (mode == 0) {
		mode = FS_O_READ;
	}

	return mode;
}

__wasi_errno_t
os_openat(os_file_handle handle, const char *path, __wasi_oflags_t oflags,
          __wasi_fdflags_t fd_flags, __wasi_lookupflags_t lookup_flags,
          wasi_libc_file_access_mode access_mode, os_file_handle *out)
{
    /*
     * `handle` will be unused because zephyr doesn't expose an openat
     * function and don't seem to have the concept of relative path.
     * We fill `out` with a new file descriptor.
     */
    int rc, index;
    struct zephyr_fs_desc *ptr = NULL;
    char abs_path[MAX_FILE_NAME + 1];

	/* Virtual /proc — parent is root or /proc/self sentinel. */
	if (handle >= 3 && !os_is_virtual_fd(handle)) {
		struct zephyr_fs_desc *parent = NULL;
		int pfd = handle;
		if (pfd >= 3 && pfd < CONFIG_WASI_MAX_OPEN_FILES + 3) {
			int is_self = 0;
			k_mutex_lock(&desc_array_mutex, K_FOREVER);
			parent = &desc_array[pfd - 3];
			if (parent->used && parent->path
			    && (pm_metal_mount_proc_is_sentinel(parent->path)
				|| strcmp(parent->path, "pm-metal:proc/self") == 0)) {
				pm_metal_mount_proc_hook_fn fn = NULL;
				is_self = (strcmp(parent->path, "pm-metal:proc/self") == 0);
				k_mutex_unlock(&desc_array_mutex);
				if (path && (strcmp(path, ".") == 0 || path[0] == '\0')) {
					return os_open_preopendir(
						is_self ? "pm-metal:proc/self" : PM_METAL_MOUNT_PROC_SENTINEL,
						out);
				}
				if (!is_self && path && strcmp(path, "self") == 0) {
					return os_open_preopendir("pm-metal:proc/self", out);
				}
				if (!is_self && path && strncmp(path, "self/", 5) == 0) {
					path = path + 5;
					is_self = 1;
				}
				if (is_self) {
					if (path && strcmp(path, "cmdline") == 0) {
						fn = pm_metal_mount_proc_generate_cmdline;
					} else if (path && strcmp(path, "environ") == 0) {
						fn = pm_metal_mount_proc_generate_environ;
					}
				} else if (path) {
					pm_metal_mount_proc_lookup(path, &fn);
				}
				if (!fn) {
					return __WASI_ENOENT;
				}
				{
					static uint32_t proc_tmp_seq;
					char *buf;
					size_t len = 0;
					char tmpath[64];
					struct fs_file_t file;
					int rc2;

					buf = BH_MALLOC(PM_METAL_MOUNT_PROC_CONTENT_MAX);
					if (!buf) {
						return __WASI_ENOMEM;
					}
					if (fn(buf, PM_METAL_MOUNT_PROC_CONTENT_MAX, &len) != 0) {
						BH_FREE(buf);
						return __WASI_EIO;
					}
					snprintf(tmpath, sizeof(tmpath), "/RAM:/.pm_proc_%u",
						 ++proc_tmp_seq);
					fs_file_t_init(&file);
					rc2 = fs_open(&file, tmpath,
						      FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
					if (rc2 < 0) {
						BH_FREE(buf);
						return convert_errno(-rc2);
					}
					fs_write(&file, buf, len);
					fs_close(&file);
					BH_FREE(buf);
					ptr = zephyr_fs_alloc_obj(false, tmpath, &index);
					if (!ptr) {
						fs_unlink(tmpath);
						return __WASI_EMFILE;
					}
					fs_file_t_init(&ptr->file);
					rc2 = fs_open(&ptr->file, tmpath, FS_O_READ);
					if (rc2 < 0) {
						zephyr_fs_free_obj(ptr);
						fs_unlink(tmpath);
						return convert_errno(-rc2);
					}
					*out = index;
					return __WASI_ESUCCESS;
				}
			}
			k_mutex_unlock(&desc_array_mutex);
		}
	}

	if (!resolve_open_path(handle, path, abs_path, sizeof(abs_path))) {
		return __WASI_ENOMEM;
	}

    // Treat directories as a special case
    bool is_dir = oflags & __WASI_O_DIRECTORY;

    ptr = zephyr_fs_alloc_obj(is_dir, abs_path, &index);
    if (ptr == NULL) {
        return __WASI_EMFILE;
    }

    if (is_dir) {
        fs_dir_t_init(&ptr->dir);
		rc = fs_opendir(&ptr->dir, abs_path);
		if (rc < 0) {
			zephyr_fs_free_obj(ptr);
			return convert_errno(-rc);
		}
		ptr->dir_opened = true;
    }
    else {
        // Is a file
        int zmode =
            wasi_flags_to_zephyr(oflags, fd_flags, lookup_flags, access_mode);
        fs_file_t_init(&ptr->file);
		rc = fs_open(&ptr->file, abs_path, zmode);

		if (rc < 0) {
			zephyr_fs_free_obj(ptr);
			return convert_errno(-rc);
		}
    }

    *out = index;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_file_get_access_mode(os_file_handle handle,
                        wasi_libc_file_access_mode *access_mode)
{

    if (handle == STDIN_FILENO) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_ONLY;
        return __WASI_ESUCCESS;
    }
    else if (handle == STDOUT_FILENO || handle == STDERR_FILENO) {
        *access_mode = WASI_LIBC_ACCESS_MODE_WRITE_ONLY;
        return __WASI_ESUCCESS;
    }

    if (pm_metal_wasi_socket_is_ours(handle)) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;
        return __WASI_ESUCCESS;
    }

    struct zephyr_fs_desc *ptr = NULL;

    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    if (ptr->is_dir) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ESUCCESS;
    }

    if ((ptr->file.flags & FS_O_RDWR) != 0) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;
    }
    else if ((ptr->file.flags & FS_O_READ) != 0) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_ONLY;
    }
    else if ((ptr->file.flags & FS_O_WRITE) != 0) {
        *access_mode = WASI_LIBC_ACCESS_MODE_WRITE_ONLY;
    }
    else {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;
    }
    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_close(os_file_handle handle, bool is_stdio)
{
    if (pm_metal_port_pipe_is_ours(handle)) {
        pm_metal_port_pipe_close_fd(handle);
        return __WASI_ESUCCESS;
    }

    if (pm_metal_wasi_socket_is_ours(handle)) {
        if (os_socket_close(handle) != 0) {
            return convert_errno(errno);
        }
        return __WASI_ESUCCESS;
    }

    if (is_stdio)
        return __WASI_ESUCCESS;

    return pm_metal_desc_close(handle);
}

__wasi_errno_t
os_preadv(os_file_handle handle, const struct __wasi_iovec_t *iov, int iovcnt,
          __wasi_filesize_t offset, size_t *nread)
{
    if (os_is_virtual_fd(handle)) {
        return __WASI_ESPIPE;
    }

    struct zephyr_fs_desc *ptr = NULL;
    int rc;
    ssize_t total_read = 0;

    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    // Seek to the offset
    rc = fs_seek(&ptr->file, offset, FS_SEEK_SET);
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }

    // Read data into each buffer
    for (int i = 0; i < iovcnt; i++) {
        ssize_t bytes_read = fs_read(&ptr->file, iov[i].buf, iov[i].buf_len);
        if (bytes_read < 0) {
            RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
            return convert_errno(-bytes_read);
        }

        total_read += bytes_read;

        /*  If we read less than we asked for, stop reading
            bytes_read being a non-negative value was already checked */
        if ((size_t)bytes_read < iov[i].buf_len) {
            break;
        }
    }

    *nread = total_read;

    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_pwritev(os_file_handle handle, const struct __wasi_ciovec_t *iov, int iovcnt,
           __wasi_filesize_t offset, size_t *nwritten)
{
    if (os_is_virtual_fd(handle)) {
        return __WASI_ESPIPE;
    }

    struct zephyr_fs_desc *ptr = NULL;
    ssize_t total_written = 0;

    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    // Seek to the offset
    int rc = fs_seek(&ptr->file, offset, FS_SEEK_SET);
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }

    // Write data from each buffer
    for (int i = 0; i < iovcnt; i++) {
        ssize_t bytes_written =
            fs_write(&ptr->file, iov[i].buf, iov[i].buf_len);
        if (bytes_written < 0) {
            RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
            return convert_errno(-bytes_written);
        }

        total_written += bytes_written;

        /*  If we wrote less than we asked for, stop writing
            bytes_read being a non-negative value was already checked */
        if ((size_t)bytes_written < iov[i].buf_len) {
            break;
        }
    }

    *nwritten = total_written;

    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_readv(os_file_handle handle, const struct __wasi_iovec_t *iov, int iovcnt,
         size_t *nread)
{
    struct zephyr_fs_desc *ptr = NULL;
    ssize_t total_read = 0;

    if (os_is_virtual_fd(handle)) {
        return __WASI_ENOSYS;
    }

    if (pm_metal_port_pipe_is_ours(handle)) {
        for (int i = 0; i < iovcnt; i++) {
            ssize_t bytes_read = pm_metal_port_pipe_read(handle, iov[i].buf, iov[i].buf_len);
            if (bytes_read < 0) {
                return __WASI_EIO;
            }
            total_read += bytes_read;
            if ((size_t)bytes_read < iov[i].buf_len) {
                break;
            }
        }
        *nread = total_read;
        return __WASI_ESUCCESS;
    }

    if (pm_metal_wasi_socket_is_ours(handle)) {
        for (int i = 0; i < iovcnt; i++) {
            int bytes_read =
                os_socket_recv(handle, iov[i].buf, (unsigned int)iov[i].buf_len);
            if (bytes_read < 0) {
                /* Report progress from earlier iovecs (WASI readv semantics). */
                if (total_read > 0) {
                    *nread = total_read;
                    return __WASI_ESUCCESS;
                }
                return convert_errno(errno);
            }
            total_read += bytes_read;
            if ((size_t)bytes_read < iov[i].buf_len) {
                break;
            }
        }
        *nread = total_read;
        return __WASI_ESUCCESS;
    }

    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    // Read data into each buffer
    for (int i = 0; i < iovcnt; i++) {
        ssize_t bytes_read = fs_read(&ptr->file, iov[i].buf, iov[i].buf_len);
        if (bytes_read < 0) {
            // If an error occurred, return it
            RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
            return convert_errno(-bytes_read);
        }

        total_read += bytes_read;

        /*  If we read less than we asked for, stop reading
            bytes_read being a non-negative value was already checked */
        if ((size_t)bytes_read < iov[i].buf_len) {
            break;
        }
    }

    *nread = total_read;

    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

/* With wasi-libc we need to redirect write on stdout/err to printf */
// TODO: handle write on stdin
__wasi_errno_t
os_writev(os_file_handle handle, const struct __wasi_ciovec_t *iov, int iovcnt,
          size_t *nwritten)
{
    ssize_t total_written = 0;

    if (os_is_virtual_fd(handle)) {
        FILE *fd = (handle == STDERR_FILENO) ? stderr : stdout;
        for (int i = 0; i < iovcnt; i++) {
            ssize_t bytes_written = fwrite(iov[i].buf, 1, iov[i].buf_len, fd);
            if (bytes_written < 0)
                return convert_errno(-bytes_written);
            total_written += bytes_written;
        }

        *nwritten = total_written;
        return __WASI_ESUCCESS;
    }

    if (pm_metal_port_pipe_is_ours(handle)) {
        for (int i = 0; i < iovcnt; i++) {
            ssize_t bytes_written = pm_metal_port_pipe_write(handle, iov[i].buf, iov[i].buf_len);
            if (bytes_written < 0) {
                return __WASI_EIO;
            }
            total_written += bytes_written;
            if ((size_t)bytes_written < iov[i].buf_len) {
                break;
            }
        }
        *nwritten = total_written;
        return __WASI_ESUCCESS;
    }

    if (pm_metal_wasi_socket_is_ours(handle)) {
        for (int i = 0; i < iovcnt; i++) {
            int bytes_written = os_socket_send(
                handle, iov[i].buf, (unsigned int)iov[i].buf_len);
            if (bytes_written < 0) {
                /* Report progress from earlier iovecs (WASI writev semantics). */
                if (total_written > 0) {
                    *nwritten = total_written;
                    return __WASI_ESUCCESS;
                }
                return convert_errno(errno);
            }
            total_written += bytes_written;
            if ((size_t)bytes_written < iov[i].buf_len) {
                break;
            }
        }
        *nwritten = total_written;
        return __WASI_ESUCCESS;
    }

    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    // Write data from each buffer
    for (int i = 0; i < iovcnt; i++) {
        ssize_t bytes_written =
            fs_write(&ptr->file, iov[i].buf, iov[i].buf_len);
        if (bytes_written < 0) {
            RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
            return convert_errno(-bytes_written);
        }

        total_written += bytes_written;

        /*  If we wrote less than we asked for, stop writing
            bytes_read being a non-negative value was already checked */
        if ((size_t)bytes_written < iov[i].buf_len) {
            break;
        }
    }

    *nwritten = total_written;

    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fallocate(os_file_handle handle, __wasi_filesize_t offset,
             __wasi_filesize_t length)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_ftruncate(os_file_handle handle, __wasi_filesize_t size)
{

    if (os_is_virtual_fd(handle)) {
        return __WASI_EINVAL;
    }

    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    int rc = fs_truncate(&ptr->file, (off_t)size);
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }

    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_futimens(os_file_handle handle, __wasi_timestamp_t access_time,
            __wasi_timestamp_t modification_time, __wasi_fstflags_t fstflags)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_utimensat(os_file_handle handle, const char *path,
             __wasi_timestamp_t access_time,
             __wasi_timestamp_t modification_time, __wasi_fstflags_t fstflags,
             __wasi_lookupflags_t lookup_flags)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_readlinkat(os_file_handle handle, const char *path, char *buf,
              size_t bufsize, size_t *nread)
{
	(void)handle;
	(void)path;
	(void)buf;
	(void)bufsize;
	if (nread != NULL) {
		*nread = 0;
	}
	return __WASI_EINVAL;
}

__wasi_errno_t
os_linkat(os_file_handle from_handle, const char *from_path,
          os_file_handle to_handle, const char *to_path,
          __wasi_lookupflags_t lookup_flags)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_symlinkat(const char *old_path, os_file_handle handle, const char *new_path)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_mkdirat(os_file_handle handle, const char *path)
{
    int rc;
    char abs_path[MAX_FILE_NAME + 1];

    if (handle < 0) {
        return __WASI_EINVAL; // Or another appropriate error code
    }

    if (!build_absolute_path(abs_path, sizeof(abs_path), path)) {
        return __WASI_ENOMEM;
    }

    /* mkdir needs no open fd — do not allocate a desc_array slot (that
     * would leak one CONFIG_WASI_MAX_OPEN_FILES entry per successful mkdir). */
    rc = fs_mkdir(abs_path);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    return __WASI_ESUCCESS;
}

// DSK: Somewhere along the WASI libc implementation path, the knowledge
// was lost that `old_handle` and `new_handle` refer to directories that
// contain the files to be renamed, rather than the file fds themselves:
//
// __wasilibc_nocwd_renameat(old_dirfd, old_relative_path,
//                           new_dirfd, new_relative_path);
//
// Therefore we won't mess with the supplied fd's, and work only off
// of the supplied paths. Note: this will change when more than one
// pre-opened dir is supported in the future.
__wasi_errno_t
os_renameat(os_file_handle old_handle, const char *old_path,
            os_file_handle new_handle, const char *new_path)
{
    // directories, safe to ignore
    (void)old_handle;
    (void)new_handle;

    char abs_old_path[MAX_FILE_NAME + 1];
    char abs_new_path[MAX_FILE_NAME + 1];

    if (!build_absolute_path(abs_old_path, sizeof(abs_old_path), old_path)) {
        return __WASI_ENOMEM;
    }

    if (!build_absolute_path(abs_new_path, sizeof(abs_new_path), new_path)) {
        return __WASI_ENOMEM;
    }

    // Attempt to perform the rename
    int rc = fs_rename(abs_old_path, abs_new_path);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    // If there is an allocated fd in our table, update the descriptor table
    // entry DSK: better approach here?
    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    for (int i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        struct zephyr_fs_desc *ptr = &desc_array[i];
        if (ptr->used && ptr->path && strcmp(ptr->path, abs_old_path) == 0) {
            size_t new_path_len = strlen(abs_new_path) + 1;
            char *new_path_copy = BH_MALLOC(new_path_len);
            if (new_path_copy != NULL) {
                strcpy(new_path_copy, abs_new_path);
                BH_FREE(ptr->path);
                ptr->path = new_path_copy;
            }
            else {
                k_mutex_unlock(&desc_array_mutex);
                return __WASI_ENOMEM;
            }
            break; // Only one descriptor should match
        }
    }
    k_mutex_unlock(&desc_array_mutex);

    return __WASI_ESUCCESS;
}

// DSK: Same thing as renameat: `handle` refers to the containing directory,
// not the file handle to unlink. We ignore the handle and use the path
// exclusively.
//
// TODO: is there anything we need to do in case is_dir=true?
__wasi_errno_t
os_unlinkat(os_file_handle handle, const char *path, bool is_dir)
{
    (void)handle;

    char abs_path[MAX_FILE_NAME + 1];

    if (!build_absolute_path(abs_path, sizeof(abs_path), path)) {
        return __WASI_ENOMEM;
    }

    int rc = fs_unlink(abs_path);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    /* POSIX unlink-while-open: remove the directory entry only. Any still-
     * open desc_array entry for this path stays valid until os_close —
     * do not fs_close/free here (that would leak the Zephyr handle mid-
     * reuse and invalidate a live guest fd). */

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_lseek(os_file_handle handle, __wasi_filedelta_t offset,
         __wasi_whence_t whence, __wasi_filesize_t *new_offset)
{

    if (os_is_virtual_fd(handle)) {
        return __WASI_ESPIPE; // Seeking not supported on character streams
    }

    struct zephyr_fs_desc *ptr = NULL;
    int zwhence;

    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);

    // They have the same value but this is more explicit
    switch (whence) {
        case __WASI_WHENCE_SET:
            zwhence = FS_SEEK_SET;
            break;
        case __WASI_WHENCE_CUR:
            zwhence = FS_SEEK_CUR;
            break;
        case __WASI_WHENCE_END:
            zwhence = FS_SEEK_END;
            break;
        default:
            RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
            return __WASI_EINVAL;
    }

    off_t rc = fs_seek(&ptr->file, (off_t)offset, zwhence);
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }

    *new_offset = (__wasi_filesize_t)rc;

    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fadvise(os_file_handle handle, __wasi_filesize_t offset,
           __wasi_filesize_t length, __wasi_advice_t advice)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_isatty(os_file_handle handle)
{
    if (os_is_virtual_fd(handle)) {
        return __WASI_ESUCCESS;
    }

    return __WASI_ENOTTY;
}

os_file_handle
os_convert_stdin_handle(os_raw_file_handle raw_stdin)
{
	int fd = (int)raw_stdin;
	if (pm_metal_port_pipe_is_ours(fd)) {
		return fd;
	}
	return STDIN_FILENO;
}

os_file_handle
os_convert_stdout_handle(os_raw_file_handle raw_stdout)
{
	int fd = (int)raw_stdout;
	if (pm_metal_port_pipe_is_ours(fd)) {
		return fd;
	}
	return STDOUT_FILENO;
}

os_file_handle
os_convert_stderr_handle(os_raw_file_handle raw_stderr)
{
	int fd = (int)raw_stderr;
	if (pm_metal_port_pipe_is_ours(fd)) {
		return fd;
	}
	return STDERR_FILENO;
}

__wasi_errno_t
os_fdopendir(os_file_handle handle, os_dir_stream *dir_stream)
{
    /* Here we assume that either mdkdir or preopendir was called
     * before otherwise function will fail.
     */
    struct zephyr_fs_desc *ptr = NULL;

    GET_FILE_SYSTEM_DESCRIPTOR(handle, ptr);
    if (!ptr->is_dir) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ENOTDIR;
    }

    if (pm_metal_wasi_is_proc_sentinel(ptr->path)) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ENOTSUP;
    }

    if (!ptr->dir_opened) {
        int rc = fs_opendir(&ptr->dir, ptr->path);
        if (rc < 0) {
            RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
            return convert_errno(-rc);
        }
        ptr->dir_opened = true;
    }

    /* we store the fd in the `os_dir_stream` to use other function */
    *dir_stream = handle;

    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

// DSK: simple open and close to rewind index.
__wasi_errno_t
os_rewinddir(os_dir_stream dir_stream)
{
    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(dir_stream, ptr);

    if (!ptr->is_dir) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ENOTDIR;
    }

    if (pm_metal_wasi_is_proc_sentinel(ptr->path)) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ENOTSUP;
    }

    int rc = fs_closedir(&ptr->dir); // Close current stream
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }
    ptr->dir_opened = false;

    rc = fs_opendir(&ptr->dir, ptr->path); // Reopen from start
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }
    ptr->dir_opened = true;

    ptr->dir_index = 0; // Reset virtual position tracker
    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

// DSK: start from 0 and linear seek since there's no cookies in the zephyr fs
// TODO: duplicated code with rewinddir
__wasi_errno_t
os_seekdir(os_dir_stream dir_stream, __wasi_dircookie_t position)
{
    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(dir_stream, ptr);

    if (!ptr->is_dir) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ENOTDIR;
    }

    if (pm_metal_wasi_is_proc_sentinel(ptr->path)) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ENOTSUP;
    }

    int rc = fs_closedir(&ptr->dir);
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }
    ptr->dir_opened = false;

    rc = fs_opendir(&ptr->dir, ptr->path);
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }
    ptr->dir_opened = true;

    // Emulate seek by re-reading entries up to 'position'
    struct fs_dirent tmp;
    for (__wasi_dircookie_t i = 0; i < position; i++) {
        rc = fs_readdir(&ptr->dir, &tmp);
        if (rc < 0) {
            RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
            return convert_errno(-rc);
        }
        if (tmp.name[0] == '\0')
            break; // End of directory
    }

    ptr->dir_index = position;
    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_readdir(os_dir_stream dir_stream, __wasi_dirent_t *entry,
           const char **d_name)
{
    struct fs_dirent fs_entry;
    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(dir_stream, ptr);
    if (!ptr->is_dir) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ENOTDIR;
    }

    if (pm_metal_wasi_is_proc_sentinel(ptr->path)) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ENOTSUP;
    }

    int rc = fs_readdir(&ptr->dir, &fs_entry);
    if (rc < 0) {
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return convert_errno(-rc);
    }

    if (fs_entry.name[0] == '\0') {
        // DSK: the caller expects the name buffer to be null
        // when we've reached the end of the directory.
        *d_name = NULL;
        RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
        return __WASI_ESUCCESS;
    }

    // DSK: emulated increasing value for rewinddir and seekdir
    entry->d_next = ++ptr->dir_index;

    // DSK: A hack to get readdir working. This needs to be non-zero along with
    // st_ino for the libc side of readdir to work correctly.
    entry->d_ino = 1 + ptr->dir_index;

    entry->d_namlen = strlen(fs_entry.name);
    entry->d_type = fs_entry.type == FS_DIR_ENTRY_DIR
                        ? __WASI_FILETYPE_DIRECTORY
                        : __WASI_FILETYPE_REGULAR_FILE;

    // DSK: name exists in fs_entry and we need to return it
    static char name_buf[MAX_FILE_NAME + 1];
    strncpy(name_buf, fs_entry.name, MAX_FILE_NAME);
    name_buf[MAX_FILE_NAME] = '\0';
    *d_name = name_buf;

    RELEASE_FILE_SYSTEM_DESCRIPTOR(ptr);
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_closedir(os_dir_stream dir_stream)
{
	struct zephyr_fs_desc *ptr;
	int is_dir;
	int is_proc = 0;

	if (dir_stream < 3 || dir_stream >= CONFIG_WASI_MAX_OPEN_FILES + 3) {
		return __WASI_EBADF;
	}
	k_mutex_lock(&desc_array_mutex, K_FOREVER);
	ptr = &desc_array[(int)dir_stream - 3];
	if (!ptr->used || ptr->closing) {
		k_mutex_unlock(&desc_array_mutex);
		return __WASI_EBADF;
	}
	is_dir = ptr->is_dir;
	if (ptr->path != NULL && pm_metal_wasi_is_proc_sentinel(ptr->path)) {
		is_proc = 1;
	}
	k_mutex_unlock(&desc_array_mutex);
	if (!is_dir) {
		return __WASI_ENOTDIR;
	}
	if (is_proc) {
		return __WASI_ENOTSUP;
	}
	return pm_metal_desc_close(dir_stream);
}

os_dir_stream
os_get_invalid_dir_stream()
{
    return OS_DIR_STREAM_INVALID;
}

bool
os_is_dir_stream_valid(os_dir_stream *dir_stream)
{
    struct zephyr_fs_desc *ptr;
    int fd;
    bool ok;

    if (dir_stream == NULL) {
        return false;
    }
    fd = *dir_stream;
    if (fd < 3 || fd >= CONFIG_WASI_MAX_OPEN_FILES + 3) {
        return false;
    }
    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    ptr = &desc_array[fd - 3];
    ok = ptr->used && ptr->is_dir;
    k_mutex_unlock(&desc_array_mutex);
    return ok;
}

bool
os_is_handle_valid(os_file_handle *handle)
{
    return handle != NULL && *handle > -1;
}

char *
os_realpath(const char *path, char *resolved_path)
{
    /* In fact we could implement a path resolving method, because every paths
     * are at one point put into memory.
     * We could then maintain a 'tree' to represent the file system.
     *    --> The file system root is easily accessable with:
     *            * (fs_dir_t) dir.mp->mnt_point
     *            * (fs_file_t) file.mp->mnt_point
     * But we will just use absolute path for now.
     */
    if ((!path) || (!resolved_path) || (strlen(path) >= PATH_MAX)) {
        // Reject paths that cannot fit with a trailing NUL in PATH_MAX bytes
        return NULL;
    }

    /* strlen < PATH_MAX ⇒ strncpy copies the NUL within PATH_MAX bytes. */
    return strncpy(resolved_path, path, PATH_MAX);
}

bool
os_compare_file_handle(os_file_handle handle1, os_file_handle handle2)
{
    return handle1 == handle2;
}

bool
os_is_stdin_handle(os_file_handle handle)
{
    return (handle == STDIN_FILENO);
}

bool
os_is_stdout_handle(os_file_handle handle)
{
    return (handle == STDOUT_FILENO);
}

bool
os_is_stderr_handle(os_file_handle handle)
{
    return (handle == STDERR_FILENO);
}

#include "pymergetic/metal/wasi/file.h"

int pm_metal_wasi_file_init(const char *vfs_root)
{
	(void)vfs_root;
	/* FAT root is DT-automounted before main; nothing else to init. */
	return 0;
}

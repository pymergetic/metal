/*
 * Linux WASI file wraps — live mount-table resolve + virtual proc.
 * Untagged handles delegate to stock posix via __real_os_* (posix_file_real.c).
 * See docs/MOUNT.md live remount / Phase 6b.
 */
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pthread.h>

#include "platform_api_extension.h"
#include "pymergetic/metal/mount/proc.h"
#include "pymergetic/metal/mount/table.h"
#include "pymergetic/metal/port/lock.h"

#define PM_METAL_VFD_MAX 64
#define PM_METAL_PROC_DIR_MAGIC 0x504d4452u /* 'PMDR' */

typedef enum pm_metal_vfd_kind {
	PM_METAL_VFD_NONE = 0,
	PM_METAL_VFD_HOST, /* hostdir / tmpfs directory fd */
	PM_METAL_VFD_PROC_ROOT,
	PM_METAL_VFD_PROC_SELF,
} pm_metal_vfd_kind_t;

typedef struct pm_metal_vfd {
	int used;
	int fd;
	pm_metal_vfd_kind_t kind;
	char guest_prefix[PM_METAL_MOUNT_GUEST_PATH_MAX];
} pm_metal_vfd_t;

typedef struct pm_metal_proc_dirstream {
	unsigned magic;
	pm_metal_vfd_kind_t kind;
	int index;
	char name_buf[PM_METAL_MOUNT_PROC_NAME_MAX];
} pm_metal_proc_dirstream_t;

static pm_metal_vfd_t g_pm_metal_vfds[PM_METAL_VFD_MAX];
static pm_metal_port_mutex_t g_pm_metal_vfd_lock;
static pthread_once_t g_pm_metal_vfd_once = PTHREAD_ONCE_INIT;

static void pm_metal_vfd_lock_init_once(void)
{
	pm_metal_port_mutex_init(&g_pm_metal_vfd_lock);
}

static void pm_metal_vfd_lock(void)
{
	pthread_once(&g_pm_metal_vfd_once, pm_metal_vfd_lock_init_once);
	pm_metal_port_mutex_lock(&g_pm_metal_vfd_lock);
}

static void pm_metal_vfd_unlock(void)
{
	pm_metal_port_mutex_unlock(&g_pm_metal_vfd_lock);
}

char *__real_os_realpath(const char *path, char *resolved_path);
__wasi_errno_t __real_os_open_preopendir(const char *path, os_file_handle *out);
__wasi_errno_t __real_os_openat(os_file_handle handle, const char *path, __wasi_oflags_t oflags,
				__wasi_fdflags_t fs_flags, __wasi_lookupflags_t lookup_flags,
				wasi_libc_file_access_mode read_write_mode, os_file_handle *out);
__wasi_errno_t __real_os_close(os_file_handle handle, bool is_stdio);
__wasi_errno_t __real_os_fstat(os_file_handle handle, struct __wasi_filestat_t *buf);
__wasi_errno_t __real_os_fstatat(os_file_handle handle, const char *path, struct __wasi_filestat_t *buf,
				  __wasi_lookupflags_t lookup_flags);
__wasi_errno_t __real_os_fdopendir(os_file_handle handle, os_dir_stream *dir_stream);
__wasi_errno_t __real_os_readdir(os_dir_stream dir_stream, __wasi_dirent_t *entry, const char **d_name);
__wasi_errno_t __real_os_closedir(os_dir_stream dir_stream);
__wasi_errno_t __real_os_rewinddir(os_dir_stream dir_stream);
__wasi_errno_t __real_os_seekdir(os_dir_stream dir_stream, __wasi_dircookie_t position);
__wasi_errno_t __real_os_readlinkat(os_file_handle handle, const char *path, char *buf, size_t bufsize,
				     size_t *bufused);
__wasi_errno_t __real_os_file_get_access_mode(os_file_handle handle, wasi_libc_file_access_mode *access_mode);
__wasi_errno_t __real_os_mkdirat(os_file_handle handle, const char *path);
__wasi_errno_t __real_os_unlinkat(os_file_handle handle, const char *path, bool is_dir);
__wasi_errno_t __real_os_renameat(os_file_handle old_handle, const char *old_path, os_file_handle new_handle,
				   const char *new_path);
__wasi_errno_t __real_os_linkat(os_file_handle from_handle, const char *from_path, os_file_handle to_handle,
				 const char *to_path, __wasi_lookupflags_t lookup_flags);
__wasi_errno_t __real_os_symlinkat(const char *old_path, os_file_handle handle, const char *new_path);
__wasi_errno_t __real_os_utimensat(os_file_handle handle, const char *path, __wasi_timestamp_t access_time,
				    __wasi_timestamp_t modification_time, __wasi_fstflags_t fstflags,
				    __wasi_lookupflags_t lookup_flags);

static pm_metal_vfd_t *pm_metal_vfd_find_unlocked(int fd)
{
	int i;

	if (fd < 0) {
		return NULL;
	}
	for (i = 0; i < PM_METAL_VFD_MAX; i++) {
		if (g_pm_metal_vfds[i].used && g_pm_metal_vfds[i].fd == fd) {
			return &g_pm_metal_vfds[i];
		}
	}
	return NULL;
}

/* Copy a tagged entry under the table lock — callers must not retain a
 * pointer into g_pm_metal_vfds across any unlock (tag/untag can reuse slots). */
static int pm_metal_vfd_lookup(int fd, pm_metal_vfd_t *out)
{
	pm_metal_vfd_t *v;

	pm_metal_vfd_lock();
	v = pm_metal_vfd_find_unlocked(fd);
	if (!v) {
		pm_metal_vfd_unlock();
		return 0;
	}
	*out = *v;
	pm_metal_vfd_unlock();
	return 1;
}

static int pm_metal_vfd_is_proc(const pm_metal_vfd_t *v)
{
	return v && (v->kind == PM_METAL_VFD_PROC_ROOT || v->kind == PM_METAL_VFD_PROC_SELF);
}

static __wasi_errno_t pm_metal_vfd_tag(int fd, pm_metal_vfd_kind_t kind, const char *guest_prefix)
{
	int i;
	__wasi_errno_t err = __WASI_EMFILE;

	if (fd < 0 || !guest_prefix) {
		return __WASI_EINVAL;
	}
	pm_metal_vfd_lock();
	if (pm_metal_vfd_find_unlocked(fd)) {
		pm_metal_vfd_unlock();
		return __WASI_ESUCCESS;
	}
	for (i = 0; i < PM_METAL_VFD_MAX; i++) {
		if (!g_pm_metal_vfds[i].used) {
			g_pm_metal_vfds[i].used = 1;
			g_pm_metal_vfds[i].fd = fd;
			g_pm_metal_vfds[i].kind = kind;
			snprintf(g_pm_metal_vfds[i].guest_prefix, sizeof(g_pm_metal_vfds[i].guest_prefix), "%s",
				 guest_prefix);
			err = __WASI_ESUCCESS;
			break;
		}
	}
	pm_metal_vfd_unlock();
	return err;
}

static void pm_metal_vfd_untag(int fd)
{
	pm_metal_vfd_t *v;

	pm_metal_vfd_lock();
	v = pm_metal_vfd_find_unlocked(fd);
	if (v) {
		memset(v, 0, sizeof(*v));
	}
	pm_metal_vfd_unlock();
}

static __wasi_errno_t pm_metal_proc_vfd_alloc(pm_metal_vfd_kind_t kind, const char *guest_prefix,
					      os_file_handle *out)
{
	int fd;
	__wasi_errno_t err;

	fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return __WASI_EIO;
	}
	err = pm_metal_vfd_tag(fd, kind, guest_prefix);
	if (err != __WASI_ESUCCESS) {
		close(fd);
		return err;
	}
	*out = fd;
	return __WASI_ESUCCESS;
}

static void pm_metal_proc_vfd_free(int fd)
{
	pm_metal_vfd_t v;

	if (!pm_metal_vfd_lookup(fd, &v) || !pm_metal_vfd_is_proc(&v)) {
		return;
	}
	/* Untag before close so a recycled fd number cannot inherit a stale proc tag. */
	pm_metal_vfd_untag(fd);
	close(v.fd);
}

static __wasi_errno_t pm_metal_proc_open_memfd(pm_metal_mount_proc_hook_fn fn, os_file_handle *out)
{
	char *buf;
	size_t len = 0;
	int fd;
	ssize_t n;
	size_t off;

	if (!fn || !out) {
		return __WASI_EINVAL;
	}
	buf = malloc(PM_METAL_MOUNT_PROC_CONTENT_MAX);
	if (!buf) {
		return __WASI_ENOMEM;
	}
	if (fn(buf, PM_METAL_MOUNT_PROC_CONTENT_MAX, &len) != 0) {
		free(buf);
		return __WASI_EIO;
	}
	fd = memfd_create("pm-metal-proc", MFD_CLOEXEC);
	if (fd < 0) {
		free(buf);
		return __WASI_EIO;
	}
	off = 0;
	while (off < len) {
		n = write(fd, buf + off, len - off);
		if (n < 0) {
			close(fd);
			free(buf);
			return __WASI_EIO;
		}
		off += (size_t)n;
	}
	free(buf);
	if (lseek(fd, 0, SEEK_SET) < 0) {
		close(fd);
		return __WASI_EIO;
	}
	*out = fd;
	return __WASI_ESUCCESS;
}

static void pm_metal_proc_fill_dir_stat(struct __wasi_filestat_t *buf)
{
	memset(buf, 0, sizeof(*buf));
	buf->st_filetype = __WASI_FILETYPE_DIRECTORY;
	buf->st_nlink = 1;
}

static int pm_metal_proc_is_dirstream(os_dir_stream dir_stream)
{
	pm_metal_proc_dirstream_t *ds = (pm_metal_proc_dirstream_t *)dir_stream;

	return ds && ds->magic == PM_METAL_PROC_DIR_MAGIC;
}

static int pm_metal_join_guest(const char *prefix, const char *rel, char *out, size_t out_cap)
{
	const char *node = rel ? rel : "";
	int n;

	while (node[0] == '/') {
		node++;
	}
	if (node[0] == '\0' || strcmp(node, ".") == 0) {
		n = snprintf(out, out_cap, "%s", prefix);
	} else if (strcmp(prefix, "/") == 0) {
		n = snprintf(out, out_cap, "/%s", node);
	} else {
		n = snprintf(out, out_cap, "%s/%s", prefix, node);
	}
	return (n > 0 && (size_t)n < out_cap) ? 0 : -1;
}

static __wasi_errno_t pm_metal_proc_open_rel(pm_metal_vfd_kind_t parent_kind, const char *path,
					      __wasi_oflags_t oflags, wasi_libc_file_access_mode access_mode,
					      os_file_handle *out)
{
	pm_metal_mount_proc_hook_fn fn = NULL;
	const char *node = path;
	int write_intent = (access_mode == WASI_LIBC_ACCESS_MODE_WRITE_ONLY
			    || access_mode == WASI_LIBC_ACCESS_MODE_READ_WRITE
			    || (oflags & __WASI_O_CREAT) != 0);

	if (!path || !out) {
		return __WASI_EINVAL;
	}
	while (node[0] == '/') {
		node++;
	}
	if (node[0] == '\0' || strcmp(node, ".") == 0) {
		/* Directory nodes: reject write-intent opens (not only O_CREAT). */
		if ((oflags & __WASI_O_DIRECTORY) == 0 && write_intent) {
			return __WASI_EISDIR;
		}
		if (parent_kind == PM_METAL_VFD_PROC_SELF) {
			return pm_metal_proc_vfd_alloc(PM_METAL_VFD_PROC_SELF, "/proc/self", out);
		}
		return pm_metal_proc_vfd_alloc(PM_METAL_VFD_PROC_ROOT, "/proc", out);
	}

	if (parent_kind == PM_METAL_VFD_PROC_ROOT) {
		if (strcmp(node, "self") == 0) {
			if ((oflags & __WASI_O_DIRECTORY) == 0 && write_intent) {
				return __WASI_EISDIR;
			}
			return pm_metal_proc_vfd_alloc(PM_METAL_VFD_PROC_SELF, "/proc/self", out);
		}
		if (strncmp(node, "self/", 5) == 0) {
			node += 5;
			parent_kind = PM_METAL_VFD_PROC_SELF;
		}
	}

	if (parent_kind == PM_METAL_VFD_PROC_SELF) {
		if (strcmp(node, "cmdline") == 0) {
			return pm_metal_proc_open_memfd(pm_metal_mount_proc_generate_cmdline, out);
		}
		if (strcmp(node, "environ") == 0) {
			return pm_metal_proc_open_memfd(pm_metal_mount_proc_generate_environ, out);
		}
		return __WASI_ENOENT;
	}

	if (pm_metal_mount_proc_lookup(node, &fn) != 0 || !fn) {
		return __WASI_ENOENT;
	}
	if ((oflags & __WASI_O_DIRECTORY) != 0) {
		return __WASI_ENOTDIR;
	}
	return pm_metal_proc_open_memfd(fn, out);
}

static __wasi_errno_t pm_metal_live_open_host(const char *host_base, const char *remainder, __wasi_oflags_t oflags,
					       __wasi_fdflags_t fs_flags, __wasi_lookupflags_t lookup_flags,
					       wasi_libc_file_access_mode read_write_mode, os_file_handle *out)
{
	os_file_handle root;
	__wasi_errno_t err;
	const char *rel = (remainder && remainder[0]) ? remainder : ".";

	err = __real_os_open_preopendir(host_base, &root);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = __real_os_openat(root, rel, oflags, fs_flags, lookup_flags, read_write_mode, out);
	__real_os_close(root, false);
	return err;
}

static __wasi_errno_t pm_metal_live_tag_if_dir(os_file_handle fd, const char *guest_abs)
{
	struct __wasi_filestat_t st;
	__wasi_errno_t err;

	err = __real_os_fstat(fd, &st);
	if (err != __WASI_ESUCCESS) {
		return __WASI_ESUCCESS;
	}
	if (st.st_filetype != __WASI_FILETYPE_DIRECTORY) {
		return __WASI_ESUCCESS;
	}
	return pm_metal_vfd_tag(fd, PM_METAL_VFD_HOST, guest_abs);
}

static __wasi_errno_t pm_metal_live_open_resolved(const pm_metal_mount_resolve_t *r, __wasi_oflags_t oflags,
						   __wasi_fdflags_t fs_flags, __wasi_lookupflags_t lookup_flags,
						   wasi_libc_file_access_mode read_write_mode, os_file_handle *out)
{
	char guest_abs[PM_METAL_MOUNT_GUEST_PATH_MAX];
	__wasi_errno_t err;

	if (r->kind == PM_METAL_MOUNT_PROC) {
		return pm_metal_proc_open_rel(PM_METAL_VFD_PROC_ROOT, r->remainder, oflags, read_write_mode,
						out);
	}

	err = pm_metal_live_open_host(r->host_base, r->remainder, oflags, fs_flags, lookup_flags, read_write_mode,
				      out);
	if (err != __WASI_ESUCCESS) {
		return err;
	}

	if (pm_metal_join_guest(r->guest_mount, r->remainder, guest_abs, sizeof(guest_abs)) == 0) {
		__wasi_errno_t tag_err = pm_metal_live_tag_if_dir(*out, guest_abs);

		if (tag_err != __WASI_ESUCCESS) {
			os_close(*out, false);
			*out = -1;
			return tag_err;
		}
	}
	return __WASI_ESUCCESS;
}

static __wasi_errno_t pm_metal_live_resolve_from(const pm_metal_vfd_t *parent, const char *path,
						  pm_metal_mount_resolve_t *out)
{
	char guest_abs[PM_METAL_MOUNT_GUEST_PATH_MAX];

	if (pm_metal_join_guest(parent->guest_prefix, path, guest_abs, sizeof(guest_abs)) != 0) {
		return __WASI_ENAMETOOLONG;
	}
	if (pm_metal_mount_resolve_ex(guest_abs, out) != 0) {
		return __WASI_ENOENT;
	}
	return __WASI_ESUCCESS;
}

char *os_realpath(const char *path, char *resolved_path)
{
	if (pm_metal_mount_proc_is_sentinel(path)) {
		char *out = resolved_path;

		/* Match glibc/POSIX: NULL buffer → malloc(PATH_MAX). */
		if (!out) {
			out = malloc(PATH_MAX);
			if (!out) {
				return NULL;
			}
		}
		snprintf(out, PATH_MAX, "%s", PM_METAL_MOUNT_PROC_SENTINEL);
		return out;
	}
	return __real_os_realpath(path, resolved_path);
}

typedef struct pm_metal_preopen_tag_ctx {
	const char *path;
	char guest[PM_METAL_MOUNT_GUEST_PATH_MAX];
	pm_metal_mount_kind_t kind;
	int found;
} pm_metal_preopen_tag_ctx_t;

static void pm_metal_preopen_tag_match(const char *guest_path, const char *source, const char *host_path,
				       pm_metal_mount_kind_t kind, const char *opts, int readonly, void *vctx)
{
	pm_metal_preopen_tag_ctx_t *ctx = vctx;
	char resolved[PATH_MAX];

	(void)source;
	(void)opts;
	(void)readonly;
	if (ctx->found || !host_path) {
		return;
	}
	if (strcmp(host_path, ctx->path) == 0) {
		snprintf(ctx->guest, sizeof(ctx->guest), "%s", guest_path);
		ctx->kind = kind;
		ctx->found = 1;
		return;
	}
	/* WAMR realpath()'s map_host before open_preopendir — match that too. */
	if (realpath(host_path, resolved) && strcmp(resolved, ctx->path) == 0) {
		snprintf(ctx->guest, sizeof(ctx->guest), "%s", guest_path);
		ctx->kind = kind;
		ctx->found = 1;
	}
}

__wasi_errno_t os_open_preopendir(const char *path, os_file_handle *out)
{
	pm_metal_preopen_tag_ctx_t ctx;
	__wasi_errno_t err;

	if (pm_metal_mount_proc_is_sentinel(path)) {
		return pm_metal_proc_vfd_alloc(PM_METAL_VFD_PROC_ROOT, "/proc", out);
	}

	err = __real_os_open_preopendir(path, out);
	if (err != __WASI_ESUCCESS) {
		return err;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.path = path;
	if (pm_metal_mount_find_by_host(path, ctx.guest, sizeof(ctx.guest), &ctx.kind) == 0) {
		ctx.found = 1;
	} else {
		pm_metal_mount_foreach(pm_metal_preopen_tag_match, &ctx);
	}
	if (ctx.found) {
		err = pm_metal_vfd_tag(*out, PM_METAL_VFD_HOST, ctx.guest);
		if (err != __WASI_ESUCCESS) {
			__real_os_close(*out, false);
			return err;
		}
	}
	return __WASI_ESUCCESS;
}

__wasi_errno_t os_openat(os_file_handle handle, const char *path, __wasi_oflags_t oflags,
			  __wasi_fdflags_t fs_flags, __wasi_lookupflags_t lookup_flags,
			  wasi_libc_file_access_mode read_write_mode, os_file_handle *out)
{
	pm_metal_vfd_t v;
	pm_metal_mount_resolve_t r;
	__wasi_errno_t err;

	if (!pm_metal_vfd_lookup(handle, &v)) {
		return __real_os_openat(handle, path, oflags, fs_flags, lookup_flags, read_write_mode, out);
	}
	if (pm_metal_vfd_is_proc(&v)) {
		return pm_metal_proc_open_rel(v.kind, path, oflags, read_write_mode, out);
	}

	err = pm_metal_live_resolve_from(&v, path, &r);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	return pm_metal_live_open_resolved(&r, oflags, fs_flags, lookup_flags, read_write_mode, out);
}

__wasi_errno_t os_close(os_file_handle handle, bool is_stdio)
{
	pm_metal_vfd_t v;

	if (pm_metal_vfd_lookup(handle, &v) && pm_metal_vfd_is_proc(&v)) {
		pm_metal_proc_vfd_free(handle);
		return __WASI_ESUCCESS;
	}
	if (pm_metal_vfd_lookup(handle, &v)) {
		pm_metal_vfd_untag(handle);
	}
	return __real_os_close(handle, is_stdio);
}

__wasi_errno_t os_fstat(os_file_handle handle, struct __wasi_filestat_t *buf)
{
	pm_metal_vfd_t v;

	if (pm_metal_vfd_lookup(handle, &v) && pm_metal_vfd_is_proc(&v)) {
		pm_metal_proc_fill_dir_stat(buf);
		return __WASI_ESUCCESS;
	}
	return __real_os_fstat(handle, buf);
}

__wasi_errno_t os_fstatat(os_file_handle handle, const char *path, struct __wasi_filestat_t *buf,
			   __wasi_lookupflags_t lookup_flags)
{
	pm_metal_vfd_t v;
	pm_metal_vfd_t tmpv;
	pm_metal_mount_resolve_t r;
	os_file_handle tmp;
	__wasi_errno_t err;

	if (!pm_metal_vfd_lookup(handle, &v)) {
		return __real_os_fstatat(handle, path, buf, lookup_flags);
	}
	if (pm_metal_vfd_is_proc(&v)) {
		err = pm_metal_proc_open_rel(v.kind, path, 0, WASI_LIBC_ACCESS_MODE_READ_ONLY, &tmp);
		if (err != __WASI_ESUCCESS) {
			return err;
		}
		if (pm_metal_vfd_lookup(tmp, &tmpv) && pm_metal_vfd_is_proc(&tmpv)) {
			pm_metal_proc_fill_dir_stat(buf);
			pm_metal_proc_vfd_free(tmp);
			return __WASI_ESUCCESS;
		}
		err = __real_os_fstat(tmp, buf);
		__real_os_close(tmp, false);
		return err;
	}

	err = pm_metal_live_resolve_from(&v, path, &r);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	if (r.kind == PM_METAL_MOUNT_PROC) {
		err = pm_metal_proc_open_rel(PM_METAL_VFD_PROC_ROOT, r.remainder, 0,
					      WASI_LIBC_ACCESS_MODE_READ_ONLY, &tmp);
		if (err != __WASI_ESUCCESS) {
			return err;
		}
		if (pm_metal_vfd_lookup(tmp, &tmpv) && pm_metal_vfd_is_proc(&tmpv)) {
			pm_metal_proc_fill_dir_stat(buf);
			pm_metal_proc_vfd_free(tmp);
			return __WASI_ESUCCESS;
		}
		err = __real_os_fstat(tmp, buf);
		__real_os_close(tmp, false);
		return err;
	}
	{
		os_file_handle root;
		const char *rel = r.remainder[0] ? r.remainder : ".";

		err = __real_os_open_preopendir(r.host_base, &root);
		if (err != __WASI_ESUCCESS) {
			return err;
		}
		err = __real_os_fstatat(root, rel, buf, lookup_flags);
		__real_os_close(root, false);
		return err;
	}
}

__wasi_errno_t os_file_get_access_mode(os_file_handle handle, wasi_libc_file_access_mode *access_mode)
{
	pm_metal_vfd_t v;

	if (pm_metal_vfd_lookup(handle, &v) && pm_metal_vfd_is_proc(&v)) {
		*access_mode = WASI_LIBC_ACCESS_MODE_READ_ONLY;
		return __WASI_ESUCCESS;
	}
	return __real_os_file_get_access_mode(handle, access_mode);
}

__wasi_errno_t os_readlinkat(os_file_handle handle, const char *path, char *buf, size_t bufsize,
			      size_t *bufused)
{
	pm_metal_vfd_t v;
	pm_metal_mount_resolve_t r;
	os_file_handle root;
	__wasi_errno_t err;
	const char *rel;

	if (!pm_metal_vfd_lookup(handle, &v)) {
		return __real_os_readlinkat(handle, path, buf, bufsize, bufused);
	}
	/* Proc tokens are not symlinks; host live-resolve still uses readlinkat. */
	if (pm_metal_vfd_is_proc(&v)) {
		(void)path;
		(void)buf;
		(void)bufsize;
		(void)bufused;
		return __WASI_EINVAL;
	}

	err = pm_metal_live_resolve_from(&v, path, &r);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	if (r.kind == PM_METAL_MOUNT_PROC) {
		return __WASI_EINVAL;
	}
	rel = r.remainder[0] ? r.remainder : ".";
	err = __real_os_open_preopendir(r.host_base, &root);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = __real_os_readlinkat(root, rel, buf, bufsize, bufused);
	__real_os_close(root, false);
	return err;
}

__wasi_errno_t os_mkdirat(os_file_handle handle, const char *path)
{
	pm_metal_vfd_t v;
	pm_metal_mount_resolve_t r;
	os_file_handle root;
	__wasi_errno_t err;
	const char *rel;

	if (!pm_metal_vfd_lookup(handle, &v)) {
		return __real_os_mkdirat(handle, path);
	}
	if (pm_metal_vfd_is_proc(&v)) {
		return __WASI_EROFS;
	}
	err = pm_metal_live_resolve_from(&v, path, &r);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	if (r.kind == PM_METAL_MOUNT_PROC) {
		return __WASI_EROFS;
	}
	rel = r.remainder[0] ? r.remainder : ".";
	err = __real_os_open_preopendir(r.host_base, &root);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = __real_os_mkdirat(root, rel);
	__real_os_close(root, false);
	return err;
}

__wasi_errno_t os_unlinkat(os_file_handle handle, const char *path, bool is_dir)
{
	pm_metal_vfd_t v;
	pm_metal_mount_resolve_t r;
	os_file_handle root;
	__wasi_errno_t err;
	const char *rel;

	if (!pm_metal_vfd_lookup(handle, &v)) {
		return __real_os_unlinkat(handle, path, is_dir);
	}
	if (pm_metal_vfd_is_proc(&v)) {
		return __WASI_EROFS;
	}
	err = pm_metal_live_resolve_from(&v, path, &r);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	if (r.kind == PM_METAL_MOUNT_PROC) {
		return __WASI_EROFS;
	}
	rel = r.remainder[0] ? r.remainder : ".";
	err = __real_os_open_preopendir(r.host_base, &root);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = __real_os_unlinkat(root, rel, is_dir);
	__real_os_close(root, false);
	return err;
}

__wasi_errno_t os_renameat(os_file_handle old_handle, const char *old_path, os_file_handle new_handle,
			    const char *new_path)
{
	pm_metal_vfd_t ov, nv;
	pm_metal_mount_resolve_t orr, nrr;
	os_file_handle old_root, new_root;
	__wasi_errno_t err;
	const char *old_rel, *new_rel;
	int have_ov = pm_metal_vfd_lookup(old_handle, &ov);
	int have_nv = pm_metal_vfd_lookup(new_handle, &nv);

	if (!have_ov && !have_nv) {
		return __real_os_renameat(old_handle, old_path, new_handle, new_path);
	}
	if (!have_ov || !have_nv || pm_metal_vfd_is_proc(&ov) || pm_metal_vfd_is_proc(&nv)) {
		return __WASI_EROFS;
	}
	err = pm_metal_live_resolve_from(&ov, old_path, &orr);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = pm_metal_live_resolve_from(&nv, new_path, &nrr);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	if (orr.kind == PM_METAL_MOUNT_PROC || nrr.kind == PM_METAL_MOUNT_PROC) {
		return __WASI_EROFS;
	}
	old_rel = orr.remainder[0] ? orr.remainder : ".";
	new_rel = nrr.remainder[0] ? nrr.remainder : ".";
	err = __real_os_open_preopendir(orr.host_base, &old_root);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = __real_os_open_preopendir(nrr.host_base, &new_root);
	if (err != __WASI_ESUCCESS) {
		__real_os_close(old_root, false);
		return err;
	}
	err = __real_os_renameat(old_root, old_rel, new_root, new_rel);
	__real_os_close(old_root, false);
	__real_os_close(new_root, false);
	return err;
}

__wasi_errno_t os_linkat(os_file_handle from_handle, const char *from_path, os_file_handle to_handle,
			  const char *to_path, __wasi_lookupflags_t lookup_flags)
{
	pm_metal_vfd_t fv, tv;
	pm_metal_mount_resolve_t fr, tr;
	os_file_handle from_root, to_root;
	__wasi_errno_t err;
	const char *from_rel, *to_rel;
	int have_fv = pm_metal_vfd_lookup(from_handle, &fv);
	int have_tv = pm_metal_vfd_lookup(to_handle, &tv);

	if (!have_fv && !have_tv) {
		return __real_os_linkat(from_handle, from_path, to_handle, to_path, lookup_flags);
	}
	if (!have_fv || !have_tv || pm_metal_vfd_is_proc(&fv) || pm_metal_vfd_is_proc(&tv)) {
		return __WASI_EROFS;
	}
	err = pm_metal_live_resolve_from(&fv, from_path, &fr);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = pm_metal_live_resolve_from(&tv, to_path, &tr);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	if (fr.kind == PM_METAL_MOUNT_PROC || tr.kind == PM_METAL_MOUNT_PROC) {
		return __WASI_EROFS;
	}
	from_rel = fr.remainder[0] ? fr.remainder : ".";
	to_rel = tr.remainder[0] ? tr.remainder : ".";
	err = __real_os_open_preopendir(fr.host_base, &from_root);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = __real_os_open_preopendir(tr.host_base, &to_root);
	if (err != __WASI_ESUCCESS) {
		__real_os_close(from_root, false);
		return err;
	}
	err = __real_os_linkat(from_root, from_rel, to_root, to_rel, lookup_flags);
	__real_os_close(from_root, false);
	__real_os_close(to_root, false);
	return err;
}

__wasi_errno_t os_symlinkat(const char *old_path, os_file_handle handle, const char *new_path)
{
	pm_metal_vfd_t v;
	pm_metal_mount_resolve_t r;
	os_file_handle root;
	__wasi_errno_t err;
	const char *rel;

	if (!pm_metal_vfd_lookup(handle, &v)) {
		return __real_os_symlinkat(old_path, handle, new_path);
	}
	if (pm_metal_vfd_is_proc(&v)) {
		return __WASI_EROFS;
	}
	err = pm_metal_live_resolve_from(&v, new_path, &r);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	if (r.kind == PM_METAL_MOUNT_PROC) {
		return __WASI_EROFS;
	}
	rel = r.remainder[0] ? r.remainder : ".";
	err = __real_os_open_preopendir(r.host_base, &root);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = __real_os_symlinkat(old_path, root, rel);
	__real_os_close(root, false);
	return err;
}

__wasi_errno_t os_utimensat(os_file_handle handle, const char *path, __wasi_timestamp_t access_time,
			     __wasi_timestamp_t modification_time, __wasi_fstflags_t fstflags,
			     __wasi_lookupflags_t lookup_flags)
{
	pm_metal_vfd_t v;
	pm_metal_mount_resolve_t r;
	os_file_handle root;
	__wasi_errno_t err;
	const char *rel;

	if (!pm_metal_vfd_lookup(handle, &v)) {
		return __real_os_utimensat(handle, path, access_time, modification_time, fstflags, lookup_flags);
	}
	if (pm_metal_vfd_is_proc(&v)) {
		return __WASI_EROFS;
	}
	err = pm_metal_live_resolve_from(&v, path, &r);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	if (r.kind == PM_METAL_MOUNT_PROC) {
		return __WASI_EROFS;
	}
	rel = r.remainder[0] ? r.remainder : ".";
	err = __real_os_open_preopendir(r.host_base, &root);
	if (err != __WASI_ESUCCESS) {
		return err;
	}
	err = __real_os_utimensat(root, rel, access_time, modification_time, fstflags, lookup_flags);
	__real_os_close(root, false);
	return err;
}

__wasi_errno_t os_fdopendir(os_file_handle handle, os_dir_stream *dir_stream)
{
	pm_metal_vfd_t v;
	pm_metal_proc_dirstream_t *ds;

	if (!pm_metal_vfd_lookup(handle, &v) || !pm_metal_vfd_is_proc(&v)) {
		return __real_os_fdopendir(handle, dir_stream);
	}
	ds = calloc(1, sizeof(*ds));
	if (!ds) {
		return __WASI_ENOMEM;
	}
	ds->magic = PM_METAL_PROC_DIR_MAGIC;
	ds->kind = v.kind;
	ds->index = 0;
	*dir_stream = (os_dir_stream)ds;
	return __WASI_ESUCCESS;
}

__wasi_errno_t os_readdir(os_dir_stream dir_stream, __wasi_dirent_t *entry, const char **d_name)
{
	pm_metal_proc_dirstream_t *ds = (pm_metal_proc_dirstream_t *)dir_stream;
	const char *name = NULL;

	if (!pm_metal_proc_is_dirstream(dir_stream)) {
		return __real_os_readdir(dir_stream, entry, d_name);
	}

	if (ds->kind == PM_METAL_VFD_PROC_ROOT) {
		int hooks = pm_metal_mount_proc_hook_count();

		if (ds->index < hooks) {
			name = pm_metal_mount_proc_hook_name(ds->index);
			entry->d_type = __WASI_FILETYPE_REGULAR_FILE;
		} else if (ds->index == hooks) {
			name = "self";
			entry->d_type = __WASI_FILETYPE_DIRECTORY;
		} else {
			*d_name = NULL;
			return __WASI_ESUCCESS;
		}
	} else if (ds->kind == PM_METAL_VFD_PROC_SELF) {
		if (ds->index == 0) {
			name = "cmdline";
			entry->d_type = __WASI_FILETYPE_REGULAR_FILE;
		} else if (ds->index == 1) {
			name = "environ";
			entry->d_type = __WASI_FILETYPE_REGULAR_FILE;
		} else {
			*d_name = NULL;
			return __WASI_ESUCCESS;
		}
	} else {
		*d_name = NULL;
		return __WASI_ESUCCESS;
	}

	if (!name) {
		*d_name = NULL;
		return __WASI_ESUCCESS;
	}
	snprintf(ds->name_buf, sizeof(ds->name_buf), "%s", name);
	*d_name = ds->name_buf;
	entry->d_next = (__wasi_dircookie_t)(ds->index + 1);
	entry->d_namlen = (__wasi_dirnamlen_t)strlen(ds->name_buf);
	entry->d_ino = 0;
	ds->index++;
	return __WASI_ESUCCESS;
}

__wasi_errno_t os_closedir(os_dir_stream dir_stream)
{
	if (pm_metal_proc_is_dirstream(dir_stream)) {
		free(dir_stream);
		return __WASI_ESUCCESS;
	}
	return __real_os_closedir(dir_stream);
}

__wasi_errno_t os_rewinddir(os_dir_stream dir_stream)
{
	pm_metal_proc_dirstream_t *ds = (pm_metal_proc_dirstream_t *)dir_stream;

	if (pm_metal_proc_is_dirstream(dir_stream)) {
		ds->index = 0;
		return __WASI_ESUCCESS;
	}
	return __real_os_rewinddir(dir_stream);
}

__wasi_errno_t os_seekdir(os_dir_stream dir_stream, __wasi_dircookie_t position)
{
	pm_metal_proc_dirstream_t *ds = (pm_metal_proc_dirstream_t *)dir_stream;

	if (pm_metal_proc_is_dirstream(dir_stream)) {
		ds->index = (int)position;
		return __WASI_ESUCCESS;
	}
	return __real_os_seekdir(dir_stream, position);
}

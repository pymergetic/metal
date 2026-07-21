/*
 * WASI filesystem for EFI: stdio + read-only ESP preopen ("/").
 * stdout/stderr → UI wasm stdout tab (line-buffered) + serial Print.
 * Regular files are loaded from the boot volume into heap buffers.
 */
#include "platform_api_extension.h"
#include "efi_wamr_internal.h"

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>

#include "pymergetic/metal/wasm/wasm.h"
#include "pymergetic/metal/ui/ui.h"
#include "pymergetic/metal/esp/esp.h"
#include "pymergetic/metal/shell/shell.h"
#include "pymergetic/metal/input/input.h"
#include "pymergetic/metal/stream/stream.h"
#include <mem/mem.h>

#include <string.h>

#define EFI_WAMR_LINE_CAP 512
#define EFI_WAMR_FD_PREOPEN 3
#define EFI_WAMR_FD_FILE0   4
#define EFI_WAMR_MAX_FILES  16

typedef struct {
  int       used;
  uint8_t  *data;
  uint32_t  len;
  uint32_t  pos;
} efi_wamr_file_t;

static efi_wamr_file_t s_files[EFI_WAMR_MAX_FILES];

/* Weak until pm_metal_wasm_* is linked from the EFI wasm runner. */
__attribute__((weak)) pm_metal_ui_handle_t
pm_metal_wasm_stdout_tab(void)
{
    return PM_METAL_UI_HANDLE_INVALID;
}

static char s_line[EFI_WAMR_LINE_CAP];
static size_t s_line_len;

static void
emit_line(const char *line, int also_serial)
{
    pm_metal_stream_h out;
    pm_metal_ui_handle_t tab;

    if (line == NULL)
        return;

    if (also_serial) {
        /* ConOut paints GOP — keep game frames clean. */
        if (pm_metal_input_game_focus())
            pm_metal_shell_serial_log(line);
        else
            Print(L"%a\n", line);
    }

    if (pm_metal_input_game_focus())
        return;

    out = pm_metal_stdio_out();
    if (out != PM_METAL_STREAM_INVALID) {
        (void)pm_metal_stream_write_line(out, line);
        return;
    }

    tab = pm_metal_wasm_stdout_tab();
    if (tab != PM_METAL_UI_HANDLE_INVALID)
        pm_metal_ui_tab_puts(tab, line);
}

void
efi_wamr_flush_stdout(int also_serial)
{
    if (s_line_len == 0)
        return;
    s_line[s_line_len] = '\0';
    emit_line(s_line, also_serial);
    s_line_len = 0;
}

void
efi_wamr_feed_stdout(const char *buf, size_t len, int also_serial)
{
    size_t i;

    if (buf == NULL || len == 0)
        return;

    for (i = 0; i < len; i++) {
        char c = buf[i];

        if (c == '\n') {
            s_line[s_line_len] = '\0';
            emit_line(s_line, also_serial);
            s_line_len = 0;
            continue;
        }
        if (c == '\r')
            continue;

        if (s_line_len + 1 >= EFI_WAMR_LINE_CAP) {
            s_line[s_line_len] = '\0';
            emit_line(s_line, also_serial);
            s_line_len = 0;
        }
        s_line[s_line_len++] = c;
    }
}

static bool
handle_is_stdio(os_file_handle handle)
{
    return handle == STDIN_FILENO || handle == STDOUT_FILENO
           || handle == STDERR_FILENO;
}

static efi_wamr_file_t *
file_from_fd(os_file_handle handle)
{
    int idx;

    if (handle < EFI_WAMR_FD_FILE0)
        return NULL;
    idx = (int)handle - EFI_WAMR_FD_FILE0;
    if (idx < 0 || idx >= EFI_WAMR_MAX_FILES)
        return NULL;
    if (!s_files[idx].used)
        return NULL;
    return &s_files[idx];
}

__wasi_errno_t
os_fstat(os_file_handle handle, struct __wasi_filestat_t *buf)
{
    efi_wamr_file_t *f;

    if (buf == NULL)
        return __WASI_EFAULT;

    if (handle_is_stdio(handle)) {
        memset(buf, 0, sizeof(*buf));
        buf->st_filetype = __WASI_FILETYPE_CHARACTER_DEVICE;
        buf->st_nlink = 1;
        return __WASI_ESUCCESS;
    }

    if (handle == EFI_WAMR_FD_PREOPEN) {
        memset(buf, 0, sizeof(*buf));
        buf->st_filetype = __WASI_FILETYPE_DIRECTORY;
        buf->st_nlink = 1;
        return __WASI_ESUCCESS;
    }

    f = file_from_fd(handle);
    if (f == NULL)
        return __WASI_EBADF;

    memset(buf, 0, sizeof(*buf));
    buf->st_filetype = __WASI_FILETYPE_REGULAR_FILE;
    buf->st_nlink = 1;
    buf->st_size = f->len;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fstatat(os_file_handle handle, const char *path, struct __wasi_filestat_t *buf,
           __wasi_lookupflags_t lookup_flags)
{
    (void)handle;
    (void)path;
    (void)buf;
    (void)lookup_flags;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_file_get_fdflags(os_file_handle handle, __wasi_fdflags_t *flags)
{
    if (flags == NULL)
        return __WASI_EFAULT;
    if (!handle_is_stdio(handle))
        return __WASI_EBADF;
    *flags = 0;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_file_set_fdflags(os_file_handle handle, __wasi_fdflags_t flags)
{
    (void)flags;
    if (!handle_is_stdio(handle))
        return __WASI_EBADF;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_fdatasync(os_file_handle handle)
{
    if (handle == STDOUT_FILENO || handle == STDERR_FILENO) {
        efi_wamr_flush_stdout(1);
        return __WASI_ESUCCESS;
    }
    if (handle == STDIN_FILENO)
        return __WASI_ESUCCESS;
    return __WASI_EBADF;
}

__wasi_errno_t
os_fsync(os_file_handle handle)
{
    return os_fdatasync(handle);
}

__wasi_errno_t
os_open_preopendir(const char *path, os_file_handle *out)
{
    if (out == NULL)
        return __WASI_EFAULT;
    /* Any preopen maps to ESP root ("/"). */
    (void)path;
    if (!pm_metal_esp_ready())
        return __WASI_ENOENT;
    *out = EFI_WAMR_FD_PREOPEN;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_openat(os_file_handle handle, const char *path, __wasi_oflags_t oflags,
          __wasi_fdflags_t fd_flags, __wasi_lookupflags_t lookup_flags,
          wasi_libc_file_access_mode access_mode, os_file_handle *out)
{
    uint8_t *data = NULL;
    uint32_t len = 0;
    int i;
    char cleaned[256];
    size_t n = 0;
    size_t p;

    (void)fd_flags;
    (void)lookup_flags;
    (void)access_mode;

    if (out == NULL || path == NULL)
        return __WASI_EFAULT;
    if (handle != EFI_WAMR_FD_PREOPEN)
        return __WASI_EBADF;
    if ((oflags & __WASI_O_CREAT) || (oflags & __WASI_O_TRUNC)
        || (oflags & __WASI_O_DIRECTORY))
        return __WASI_ENOTCAPABLE;
    if (!pm_metal_esp_ready())
        return __WASI_ENOENT;

    /* Normalize leading "./" and slashes for ESP relative paths. */
    p = 0;
    while (path[p] == '/' || path[p] == '\\')
        p++;
    if (path[p] == '.' && (path[p + 1] == '/' || path[p + 1] == '\\'))
        p += 2;
    while (path[p] != '\0' && n + 1 < sizeof(cleaned)) {
        char c = path[p++];
        if (c == '\\')
            c = '/';
        cleaned[n++] = c;
    }
    cleaned[n] = '\0';
    if (cleaned[0] == '\0')
        return __WASI_ENOENT;

    if (pm_metal_esp_read_file(cleaned, &data, &len) != 0)
        return __WASI_ENOENT;

    for (i = 0; i < EFI_WAMR_MAX_FILES; i++) {
        if (!s_files[i].used) {
            s_files[i].used = 1;
            s_files[i].data = data;
            s_files[i].len = len;
            s_files[i].pos = 0;
            *out = (os_file_handle)(EFI_WAMR_FD_FILE0 + i);
            return __WASI_ESUCCESS;
        }
    }

    pm_metal_mem_free(data);
    return __WASI_ENFILE;
}

__wasi_errno_t
os_file_get_access_mode(os_file_handle handle,
                        wasi_libc_file_access_mode *access_mode)
{
    if (access_mode == NULL)
        return __WASI_EFAULT;
    if (handle == STDIN_FILENO) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_ONLY;
        return __WASI_ESUCCESS;
    }
    if (handle == STDOUT_FILENO || handle == STDERR_FILENO) {
        *access_mode = WASI_LIBC_ACCESS_MODE_WRITE_ONLY;
        return __WASI_ESUCCESS;
    }
    if (handle == EFI_WAMR_FD_PREOPEN || file_from_fd(handle) != NULL) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_ONLY;
        return __WASI_ESUCCESS;
    }
    return __WASI_EBADF;
}

__wasi_errno_t
os_close(os_file_handle handle, bool is_stdio_fd)
{
    efi_wamr_file_t *f;

    if (is_stdio_fd || handle_is_stdio(handle))
        return __WASI_ESUCCESS;
    if (handle == EFI_WAMR_FD_PREOPEN)
        return __WASI_ESUCCESS;

    f = file_from_fd(handle);
    if (f == NULL)
        return __WASI_EBADF;
    if (f->data != NULL)
        pm_metal_mem_free(f->data);
    memset(f, 0, sizeof(*f));
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_preadv(os_file_handle handle, const struct __wasi_iovec_t *iov, int iovcnt,
          __wasi_filesize_t offset, size_t *nread)
{
    (void)handle;
    (void)iov;
    (void)iovcnt;
    (void)offset;
    (void)nread;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_pwritev(os_file_handle handle, const struct __wasi_ciovec_t *iov, int iovcnt,
           __wasi_filesize_t offset, size_t *nwritten)
{
    (void)handle;
    (void)iov;
    (void)iovcnt;
    (void)offset;
    (void)nwritten;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_readv(os_file_handle handle, const struct __wasi_iovec_t *iov, int iovcnt,
         size_t *nread)
{
    efi_wamr_file_t *f;
    size_t total = 0;
    int i;

    if (nread == NULL)
        return __WASI_EFAULT;

    if (handle == STDIN_FILENO) {
        *nread = 0;
        return __WASI_ESUCCESS;
    }

    if (handle == STDOUT_FILENO || handle == STDERR_FILENO)
        return __WASI_EBADF;

    f = file_from_fd(handle);
    if (f == NULL)
        return __WASI_EBADF;
    if (iov == NULL && iovcnt > 0)
        return __WASI_EFAULT;

    for (i = 0; i < iovcnt; i++) {
        size_t want;
        size_t avail;

        if (iov[i].buf == NULL && iov[i].buf_len > 0)
            return __WASI_EFAULT;
        if (f->pos >= f->len)
            break;
        avail = (size_t)(f->len - f->pos);
        want = (size_t)iov[i].buf_len;
        if (want > avail)
            want = avail;
        if (want > 0) {
            CopyMem(iov[i].buf, f->data + f->pos, want);
            f->pos += (uint32_t)want;
            total += want;
        }
    }

    *nread = total;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_writev(os_file_handle handle, const struct __wasi_ciovec_t *iov, int iovcnt,
          size_t *nwritten)
{
    size_t total = 0;
    int i;

    if (nwritten == NULL)
        return __WASI_EFAULT;

    if (handle != STDOUT_FILENO && handle != STDERR_FILENO) {
        if (handle == STDIN_FILENO)
            return __WASI_EBADF;
        return __WASI_ENOTCAPABLE;
    }

    if (iov == NULL && iovcnt > 0)
        return __WASI_EFAULT;

    for (i = 0; i < iovcnt; i++) {
        if (iov[i].buf == NULL && iov[i].buf_len > 0)
            return __WASI_EFAULT;
        efi_wamr_feed_stdout((const char *)iov[i].buf, (size_t)iov[i].buf_len,
                             1);
        total += (size_t)iov[i].buf_len;
    }

    *nwritten = total;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fallocate(os_file_handle handle, __wasi_filesize_t offset,
             __wasi_filesize_t length)
{
    (void)handle;
    (void)offset;
    (void)length;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_ftruncate(os_file_handle handle, __wasi_filesize_t size)
{
    (void)handle;
    (void)size;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_futimens(os_file_handle handle, __wasi_timestamp_t access_time,
            __wasi_timestamp_t modification_time, __wasi_fstflags_t fstflags)
{
    (void)handle;
    (void)access_time;
    (void)modification_time;
    (void)fstflags;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_utimensat(os_file_handle handle, const char *path,
             __wasi_timestamp_t access_time,
             __wasi_timestamp_t modification_time, __wasi_fstflags_t fstflags,
             __wasi_lookupflags_t lookup_flags)
{
    (void)handle;
    (void)path;
    (void)access_time;
    (void)modification_time;
    (void)fstflags;
    (void)lookup_flags;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_readlinkat(os_file_handle handle, const char *path, char *buf, size_t bufsize,
              size_t *nread)
{
    (void)handle;
    (void)path;
    (void)buf;
    (void)bufsize;
    (void)nread;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_linkat(os_file_handle from_handle, const char *from_path,
          os_file_handle to_handle, const char *to_path,
          __wasi_lookupflags_t lookup_flags)
{
    (void)from_handle;
    (void)from_path;
    (void)to_handle;
    (void)to_path;
    (void)lookup_flags;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_symlinkat(const char *old_path, os_file_handle handle, const char *new_path)
{
    (void)old_path;
    (void)handle;
    (void)new_path;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_mkdirat(os_file_handle handle, const char *path)
{
    (void)handle;
    (void)path;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_renameat(os_file_handle old_handle, const char *old_path,
            os_file_handle new_handle, const char *new_path)
{
    (void)old_handle;
    (void)old_path;
    (void)new_handle;
    (void)new_path;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_unlinkat(os_file_handle handle, const char *path, bool is_dir)
{
    (void)handle;
    (void)path;
    (void)is_dir;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_lseek(os_file_handle handle, __wasi_filedelta_t offset,
         __wasi_whence_t whence, __wasi_filesize_t *new_offset)
{
    efi_wamr_file_t *f;
    int64_t base;
    int64_t next;

    f = file_from_fd(handle);
    if (f == NULL)
        return __WASI_EBADF;
    if (new_offset == NULL)
        return __WASI_EFAULT;

    if (whence == __WASI_WHENCE_SET)
        base = 0;
    else if (whence == __WASI_WHENCE_CUR)
        base = (int64_t)f->pos;
    else if (whence == __WASI_WHENCE_END)
        base = (int64_t)f->len;
    else
        return __WASI_EINVAL;

    next = base + (int64_t)offset;
    if (next < 0)
        return __WASI_EINVAL;
    if (next > (int64_t)f->len)
        next = (int64_t)f->len;
    f->pos = (uint32_t)next;
    *new_offset = (__wasi_filesize_t)f->pos;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fadvise(os_file_handle handle, __wasi_filesize_t offset,
           __wasi_filesize_t length, __wasi_advice_t advice)
{
    (void)handle;
    (void)offset;
    (void)length;
    (void)advice;
    return __WASI_ESUCCESS; /* advisory; ignore */
}

__wasi_errno_t
os_isatty(os_file_handle handle)
{
    if (handle_is_stdio(handle))
        return __WASI_ESUCCESS;
    return __WASI_ENOTTY;
}

os_file_handle
os_convert_stdin_handle(os_raw_file_handle raw_stdin)
{
    return raw_stdin >= 0 ? (os_file_handle)raw_stdin : STDIN_FILENO;
}

os_file_handle
os_convert_stdout_handle(os_raw_file_handle raw_stdout)
{
    return raw_stdout >= 0 ? (os_file_handle)raw_stdout : STDOUT_FILENO;
}

os_file_handle
os_convert_stderr_handle(os_raw_file_handle raw_stderr)
{
    return raw_stderr >= 0 ? (os_file_handle)raw_stderr : STDERR_FILENO;
}

bool
os_is_stdin_handle(os_file_handle fd)
{
    return fd == STDIN_FILENO;
}

bool
os_is_stdout_handle(os_file_handle fd)
{
    return fd == STDOUT_FILENO;
}

bool
os_is_stderr_handle(os_file_handle fd)
{
    return fd == STDERR_FILENO;
}

__wasi_errno_t
os_fdopendir(os_file_handle handle, os_dir_stream *dir_stream)
{
    (void)handle;
    if (dir_stream != NULL)
        *dir_stream = NULL;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_rewinddir(os_dir_stream dir_stream)
{
    (void)dir_stream;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_seekdir(os_dir_stream dir_stream, __wasi_dircookie_t position)
{
    (void)dir_stream;
    (void)position;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_readdir(os_dir_stream dir_stream, __wasi_dirent_t *entry, const char **d_name)
{
    (void)dir_stream;
    (void)entry;
    if (d_name != NULL)
        *d_name = NULL;
    return __WASI_ENOTCAPABLE;
}

__wasi_errno_t
os_closedir(os_dir_stream dir_stream)
{
    (void)dir_stream;
    return __WASI_ENOTCAPABLE;
}

os_dir_stream
os_get_invalid_dir_stream(void)
{
    return NULL;
}

bool
os_is_dir_stream_valid(os_dir_stream *dir_stream)
{
    return dir_stream != NULL && *dir_stream != NULL;
}

bool
os_is_handle_valid(os_file_handle *handle)
{
    return handle != NULL && *handle > -1;
}

char *
os_realpath(const char *path, char *resolved_path)
{
    size_t n;

    if (path == NULL || resolved_path == NULL)
        return NULL;

    /* EFI has no host cwd; ESP preopen paths are already canonical enough. */
    n = strlen(path);
    if (n + 1 > PATH_MAX)
        return NULL;
    memcpy(resolved_path, path, n + 1);
    return resolved_path;
}

# WASI

`snapshot preview1` · `wasm32-wasip1` · 44 syscalls in `api.h` + `proc_exit` (noreturn, engine).

Host implements a **pinned subset**. Unlisted syscalls → `ENOTSUP` until promoted.

---

## Not WASI

| Mechanism | Provider |
|-----------|----------|
| linear memory | WAMR |
| `proc_exit` | WAMR runtime |

---

## Syscalls

### Launch context

Set at instantiate (`wasm_runtime_set_wasi_args`). No OS backend.

| Syscall | libc-ish use |
|---------|--------------|
| `args_sizes_get` | argc / argv buf |
| `args_get` | argv |
| `environ_sizes_get` | env count / buf |
| `environ_get` | env |

### Clocks

| Syscall | libc-ish use | Host needs |
|---------|--------------|------------|
| `clock_res_get` | timer res | realtime + monotonic |
| `clock_time_get` | `time`, `clock_gettime` | same |

### Preopens

| Syscall | Host needs |
|---------|------------|
| `fd_prestat_get` | preopen table at instantiate |
| `fd_prestat_dir_name` | guest path per preopen fd |

### FD I/O

| Syscall | libc-ish use | Host needs |
|---------|--------------|------------|
| `fd_read` | `read`, `fread` | fd table + backend |
| `fd_write` | `write`, `printf` | fd table; 1/2 virtual |
| `fd_close` | `close` | fd table |
| `fd_seek` | `lseek`, `fseek` | seekable handle |
| `fd_tell` | `ftell` | same |
| `fd_pread` | positioned read | file + offset |
| `fd_pwrite` | positioned write | file + offset |
| `fd_fdstat_get` | `fcntl`, `isatty` | per-fd type/flags/rights |
| `fd_fdstat_set_flags` | `fcntl(F_SETFL)` | flag mutation |
| `fd_fdstat_set_rights` | cap narrowing | rights enforcement |
| `fd_filestat_get` | `fstat` | inode meta |
| `fd_filestat_set_size` | `ftruncate` | mutate size |
| `fd_filestat_set_times` | `utimens` | timestamps |
| `fd_allocate` | `posix_fallocate` | sparse grow |
| `fd_advise` | `posix_fadvise` | stub ok |
| `fd_datasync` | `fdatasync` | flush / stub |
| `fd_sync` | `fsync` | flush / stub |
| `fd_renumber` | atomic dup2 | fd table |
| `fd_readdir` | `readdir` | dir iteration |

### Paths (dirfd + relative path)

| Syscall | libc-ish use | Host needs |
|---------|--------------|------------|
| `path_open` | `openat` | resolve under preopen |
| `path_filestat_get` | `stat` | resolve + meta |
| `path_filestat_set_times` | `utimensat` | resolve + times |
| `path_create_directory` | `mkdirat` | create dir |
| `path_remove_directory` | `rmdir` | remove dir |
| `path_unlink_file` | `unlinkat` | delete file |
| `path_rename` | `renameat` | rename |
| `path_link` | `linkat` | hard link |
| `path_symlink` | `symlinkat` | symlink |
| `path_readlink` | `readlinkat` | read symlink |

### Poll / random / sched

| Syscall | libc-ish use | Host needs |
|---------|--------------|------------|
| `poll_oneoff` | `poll`, `select` | fd readiness + clocks |
| `random_get` | `getrandom` | CSPRNG |
| `sched_yield` | `sched_yield` | stub `SUCCESS` ok |

### Sockets (preview1)

| Syscall | Host needs |
|---------|------------|
| `sock_accept` | network stack |
| `sock_recv` | network stack |
| `sock_send` | network stack |
| `sock_shutdown` | network stack |

WAMR adds more `sock_*` — not preview1. Defer all sockets.

---

## Tiers

### T0 — runs

```
proc_exit  fd_write  fd_fdstat_get
```

Test: `printf("ok\n")`.

### T1 — read file under preopen (parity target)

T0 +

```
fd_prestat_get  fd_prestat_dir_name
path_open  path_filestat_get
fd_read  fd_seek  fd_close
```

Test: open file in preopened dir, read, print.

### T2 — small C program

T1 +

```
args_sizes_get  args_get
environ_sizes_get  environ_get
clock_res_get  clock_time_get
random_get  sched_yield
fd_tell
```

### T3 — writable FS

T2 +

```
path_create_directory  path_unlink_file  path_rename
fd_filestat_set_size  path_filestat_set_times
fd_sync  fd_datasync
```

### T4 — full FS

T3 +

```
fd_readdir  path_remove_directory  path_readlink
path_symlink  path_link
fd_pread  fd_pwrite  fd_allocate  fd_advise
fd_renumber  fd_fdstat_set_flags  fd_fdstat_set_rights
fd_filestat_get  fd_filestat_set_times
```

### T5 — later

```
poll_oneoff  sock_*  wasi-threads
```

---

## Pinned start: `PM_WASI_SUBSET_T1`

```
proc_exit
fd_write  fd_fdstat_get
fd_prestat_get  fd_prestat_dir_name
path_open  path_filestat_get
fd_read  fd_seek  fd_close
```

Comfort add (promote to T1b when needed):

```
clock_time_get  clock_res_get
environ_sizes_get  environ_get
random_get  sched_yield
```

---

## Host parity

| Tier | linux (WAMR posix) | zephyr |
|------|-------------------|--------|
| T0 | mostly free | shim: virtual stdout |
| T1 | preopen + posix | VFS + path resolve |
| T2+ | WAMR covers most | per-syscall shim |

Same guest imports. Different host backend. Expected.

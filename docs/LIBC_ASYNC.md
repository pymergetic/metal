# Metal libc / POSIX surface — classified (frozen v1)

**Status: frozen for v1 bring-up** (2026-07-20).  
Changing a row needs an explicit doc edit + reason — not drive-by API creep.

Goal: one **Metal ABI** (host ↔ guest symmetric), not WASI and not “async everything.”

| Class | Meaning |
|-------|---------|
| **omit** | Not in Metal v1. No API, no WASI polyfill, no “compat later” without revisiting this doc. |
| **sync** | Runs on the worker; must be **bounded CPU** (or a non-blocking put to a ring). |
| **async** | Returns a handle; completion only via `await` / `guest_step` resume. |

Stackless rule: anything that waits on the world is **async**.  
CPU work stays **sync**. Preemptive-OS machinery is **omit**.

Doom is **parked** (`mods/apps/doom` kept; default build/run/verify do not stage it).  
Opt-in later: `METAL_BUILD_DOOM=1`.

---

## Locked decisions (the contested ones)

| Topic | Lock | Why |
|-------|------|-----|
| Public sleep | **async only** (`sleep_us` / `sleep_until_us` / `sleep`) | No blocking `sleep` / `nanosleep` in the product ABI |
| Clock read (`mono_us` / `mono_ms`, realtime get) | **sync** | Instant read of already-maintained time |
| `printf` / console write | **sync façade** | Format + enqueue to UART/UI ring; never park the worker |
| File data path (`open`/`read`/`write`/…) | **async** | Even if ESP is fast today — shape matches virtio-blk later |
| `stat` / `fstat` / size probe | **async** | Same as read; avoid “sync meta, async data” split |
| `lseek` | **sync** on open file handle | Cursor math in Metal; no I/O wait |
| `mmap` / demand paging | **omit** | No MMU paging story; use `fs_read` into buffer |
| Map already-owned buffer | **omit v1** | Revisit only if a real guest needs it |
| `poll` / `select` / `epoll` / WASI `poll_oneoff` | **omit** | Metal runloop + handles |
| `setjmp` / `longjmp` | **omit** | Breaks stackless await |
| threads / signals / fork / dlopen | **omit** | 1 worker/CPU; Metal tasks/coros; Metal package load ≠ `dlopen` |
| WASI as product ABI | **retire** | Scaffold only; no new guest deps on wasi-libc I/O |
| Input | **sync poll** v1 (`poll_key_packed`) | Optional async “wait for key” later, not required now |
| Gfx / UI / shell log | **sync** | Device kick + CPU; no await |

---

## Already Metal (keep / grow here)

| Surface | Class | Notes |
|---------|-------|--------|
| `pm_metal_async_*` (sleep_us/until, yield, await, present, mono_us) | **async** (+ sync get for mono) | See `docs/IO.md` |
| `pm_metal_fs_{size,read,write}_async` + `fs_result` | **async** | Eager ESP ok; always `await` then `fs_result`. Sync size/read transitional (parked doom) |
| `pm_metal_input_poll_key_packed` | **sync poll** | |
| `pm_metal_gfx_*` | **sync** | |
| `pm_metal_shell_*` / `pm_metal_ui_*` | **sync** | |
| host TLSF / `malloc` / `memcpy` / `snprintf` | **sync** | Freestanding kit |

---

## Freestanding / libc

| Family | Class | Notes |
|--------|-------|--------|
| `string.h` / `strings.h` | sync | |
| `ctype.h` | sync | |
| `stdlib.h` abs/div/rand | sync | No `system` |
| process `exit` | control | → session `DONE` / tear-down, not POSIX `_exit` |
| `stdio.h` sprintf/snprintf/vsnprintf | sync | |
| `stdio.h` printf/fwrite to console | sync façade | → Metal log/serial ring |
| `math.h` | sync | |
| `errno` / assert | sync | |
| `setjmp` / `longjmp` | **omit** | |
| locale / wide char / iconv | **omit** | |
| `time.h` clock get | sync | |
| `time.h` sleep / nanosleep | **async** | only via Metal sleep+await |

---

## POSIX I/O

| Family | Class | Notes |
|--------|-------|--------|
| `open` / `read` / `write` / `close` | **async** | Metal fs handles, not WASI fds as ABI |
| `pread` / `pwrite` | **async** | |
| `stat` / `fstat` / size | **async** | |
| `lseek` | **sync** | On live handle |
| directory iter (`readdir`) | **async** | |
| `mkdir` / `unlink` / `rename` | **async** | |
| `fsync` | **async** | |
| `mmap` / `munmap` | **omit** | |
| `poll` / `select` / `epoll` | **omit** | |
| `fcntl` / `ioctl` zoo | **omit** | |
| pipes / tty / stdio | **stream** class | See `docs/IO.md` — raw PTY first; cooked omit until needed |
| `socket` / `connect` / `send` / `recv` | **async** (post-v1) | Same await shape |
| `getaddrinfo` | **async** (post-v1) | |

---

## POSIX process / threads

| Family | Class |
|--------|-------|
| `fork` / `exec` / `wait` / `kill` | **omit** |
| `pthread_*` / mutex zoo | **omit** |
| signals / `sigaction` | **omit** |
| `dlopen` / `dlsym` | **omit** |
| shm / POSIX semaphores | **omit** |

---

## WASI preview1 — retirement map

| WASI area | Metal fate |
|-----------|------------|
| args/environ | sync launch context (optional) |
| clocks | sync get + async sleep |
| fd_* / path_* file | → async `pm_metal_fs_*` |
| fd_write stdout/stderr | → sync Metal log/serial |
| `poll_oneoff` | omit |
| `proc_exit` | session DONE |
| random | sync |
| everything else | omit |

**Rule:** do not add new guest code that depends on wasi-libc blocking I/O.

---

## Stacklessness rules

1. Public wait = `await(handle)` / `guest_step` resume — never block this CPU worker.
2. Sync libc on the worker must be bounded CPU (or non-blocking ring put).
3. No `setjmp` across await points.
4. Host and guest see the same op classes; bindings differ (native vs import).

---

## Near-term work order

1. [x] Park doom from default build/run/verify/shell  
2. [x] Freeze this classification  
3. [x] Grow Metal async FS/time to match the async column (awaitable fs even when ESP is eager)  
4. [x] Shrink/remove WASI from guest bring-up path  
5. [x] Optimize runloop / visibility / locking (1 worker/CPU)  
6. [ ] Re-enable doom only on Metal ABI (native or wasm-without-WASI)

### Step 3–5 notes (2026-07-20)

- **FS:** `pm_metal_fs_size_async` / `read_async` / `write_async` → host coro → `await` → `pm_metal_fs_result(self)` (u32 on guest coro). ESP size uses GetInfo-only (`pm_metal_esp_file_size`); write create/truncates via SimpleFileSystem. Proof: embed `async_fs` + ESP `mods/tests/async_fs.txt` (+ write round-trip).
- **WASI:** embed `hello` uses `shell_log` (no printf). Instantiation sets **0** WASI preopens — no `/` → ESP. libc-wasi still linked for CRT/`proc_exit`; no new guest I/O deps.
- **Runloop:** timer lock init in `pm_metal_run_init`; session pump uses `CpuPause` (no 1 ms `Stall`); `sleep(0)` completes eagerly (fairness via `yield`).

---

## Symmetry

```text
Guest  --imports/calls-->  Metal ops (async | sync)  <--calls--  Host EFI
```

Not: guest WASI + host glibc. One checklist, two bindings.

# Layers

Base stack only — from hardware/OS up to the point where a `.wasm` file can run.
Everything above the **wasm interface** is out of scope here.

```
targets = { linux, zephyr, rump, unikraft }   # experiment: linux + zephyr first
```

---

## Stack

```
┌─────────────────────────────────────────┐
│  WASM INTERFACE                         │  guest sees this — nothing below
│  wasm32-wasip1 · WASI preview 1         │
├─────────────────────────────────────────┤
│  WASI host                              │  implement preview-1 syscalls
├─────────────────────────────────────────┤
│  WAMR                                   │  load · instantiate · run
├─────────────────────────────────────────┤
│  runtime                                │  one native binary per target
├─────────────────────────────────────────┤
│  port                                   │  OS → portable floor (probes, alloc)
├─────────────────────────────────────────┤
│  os                                     │  Linux kernel · Zephyr kernel · …
├─────────────────────────────────────────┤
│  boot                                   │  GRUB · UEFI · multiboot · …
├─────────────────────────────────────────┤
│  hardware                               │
└─────────────────────────────────────────┘
```

**Wasm interface** = a `wasm32-wasip1` module running under WAMR with a pinned WASI preview 1 host.
Same `.wasm` artifact on every target. Same engine. Same syscall surface.

---

## Layers (bottom → wasm)

| # | Layer | Job |
|---|-------|-----|
| 1 | **hardware** | CPU, RAM, devices |
| 2 | **boot** | firmware hands off to an OS image |
| 3 | **os** | kernel + libc the port talks to |
| 4 | **port** | hide the OS; expose a small C API (RAM probe, heap, map, time, …) |
| 5 | **runtime** | long-lived host — `init`, dynamic `load`/`run`/`unload`, `shutdown` |
| 6 | **wamr** | wasm engine — one build (WAMR) on all targets |
| 7 | **wasi host** | syscall backend behind preview 1 (POSIX on linux, shim on zephyr, …) |
| 8 | **wasm interface** | `.wasm` in, execution out — **stop here** |

---

## Rules

1. **One engine** — WAMR everywhere. No per-target wasm runtimes.
2. **One mod format** — `wasm32-wasip1`. Pin WASI preview 1; no component model in this slice.
3. **Port is the only OS leak** — no `#include <zephyr/…>` or `<unistd.h>` outside `src/<plat>/pymergetic/metal/`.
4. **Runtime is native C** — not wasm. It hosts guests; it does not execute through WASI.
5. **Behavioural parity** — same `.wasm` + same preopened guest paths → same observable result on every target.

---

## Per-target (what changes)

| Layer | linux | zephyr |
|-------|-------|--------|
| boot | (host OS already up) | multiboot / native_sim / EFI |
| os | Linux | Zephyr |
| port | env probe, malloc, POSIX | E820/DT probe, k_malloc, VFS |
| runtime | same shape | same shape |
| wamr | `WAMR_BUILD_PLATFORM=linux` | `WAMR_BUILD_PLATFORM=zephyr` |
| wasi host | WAMR linux WASI (POSIX) | WAMR zephyr WASI shim |
| wasm interface | **identical** | **identical** |

Upper rows differ. The wasm interface does not.

---

## Done when

- [ ] `src/linux` and `src/zephyr` each build a runtime binary
- [ ] dynamic loader: `init` → load → run → unload (see [RUNTIME.md](RUNTIME.md))
- [ ] two mods in one process without restart
- [ ] WASI preview 1 syscalls used by tests work on both
- [ ] `verify`: build mods → load/run on linux + zephyr → same stdout

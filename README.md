# pymergetic-metal

Pymergetic-metal: native **runtime** per target that runs **wasm** mods (`wasm32-wasip1`).

**Not here:** CPython, zlib, OpenSSL, etc. — those live in [`packages/kernel`](../kernel/).

**Runtime** — long-lived native binary per target: dynamic load/run/unload via WAMR.  
**Mods** — `.wasm` files loaded and executed through the wasm interface.

`backup/1st_try/` is reference only — not the codebase being built.

---

## Documentation

| Doc | What |
|-----|------|
| [docs/LAYERS.md](docs/LAYERS.md) | Base layer model — hardware/OS up to the wasm interface |
| [docs/WASI.md](docs/WASI.md) | WASI preview1 syscalls, host requirements, tiers |
| [docs/RUNTIME.md](docs/RUNTIME.md) | Process model — long-lived dynamic loader |
| [docs/SOURCETREE.md](docs/SOURCETREE.md) | Folder layout (`include/` / `src/`) |

---

## Layout

```
packages/metal/
├── include/pymergetic/metal/   mod-facing (metal.h)
├── src/
│   ├── common/pymergetic/metal/  cross-target runtime + contracts
│   ├── linux/                    OS bind
│   └── zephyr/
├── mods/                       test .wasm guests (wasi-sdk)
├── scripts/
├── docs/
├── west-manifest/
└── backup/                     old tries — not built from
```

See [docs/SOURCETREE.md](docs/SOURCETREE.md).

---

## Visibility

- Public metal API: always visible in headers (mod compiles need no extra define).
- Kernel-only API: `-DPM_METAL_BUILD_KERNEL` (`metal/build.h`).
- Privileged mod: same wasm format, compiled with `-DPM_METAL_BUILD_KERNEL`.

---

## Build status

Scaffold landed (`src/`, `mods/`, linux stub builds). Implementation plan: [docs/RUNTIME.md § Bring-up plan](docs/RUNTIME.md#bring-up-plan).

---

## Scope

| Package | Responsibility |
|---------|----------------|
| **metal** (this) | Runtime (upper+lower) + mod loading |
| **kernel** | CPython 3.14, vendored C libs, language runtime |

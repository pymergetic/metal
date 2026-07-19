# pymergetic-metal

Native **runtime** that runs **wasm** mods (`wasm32-wasip1`) as a
**freestanding UEFI** payload (static virtio I/O). No hosted OS in-tree.

**Not here:** CPython, zlib, OpenSSL, etc. — those live in [`packages/kernel`](../kernel/).

**Archived:** linux / zephyr / nuttx / rump / unikraft ports and verify —
see git branch `archive/multi-host-linux-zephyr-nuttx`.

---

## Documentation

| Doc | What |
|-----|------|
| [docs/LAYERS.md](docs/LAYERS.md) | Stack from firmware up to the wasm interface |
| [docs/WASI.md](docs/WASI.md) | WASI preview1 syscalls, host requirements |
| [docs/RUNTIME.md](docs/RUNTIME.md) | Process / load model |
| [docs/MEMORY.md](docs/MEMORY.md) | Host pools vs guest linear memory |
| [docs/SOURCETREE.md](docs/SOURCETREE.md) | Folder layout |
| [docs/MOUNT.md](docs/MOUNT.md) | Mount design (from hosted era; still the contract) |
| [src/efi/README.md](src/efi/README.md) | EFI + virtio bring-up |

---

## Layout

```
packages/metal/
├── include/pymergetic/metal/   mod-facing API
├── src/
│   ├── common/pymergetic/metal/  cross-target runtime
│   └── efi/                      freestanding UEFI + static virtio
├── mods/tests/                 harness .wasm guests
├── mods/apps/                  real guests (python)
├── scripts/                    setup|build|verify
└── patches/                    tracked diffs against external/*
```

---

## Quick start

```bash
./scripts/setup all          # deps + efi toolchain notes
./scripts/build mod          # guest tests
./scripts/build efi          # scaffold → build/efi (metal.efi next)
./scripts/verify efi         # QEMU+OVMF once metal.efi exists
```

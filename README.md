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
| [docs/EFI.md](docs/EFI.md) | EFI bring-up: boot → EBS → library → WAMR → virtio cut line |
| [docs/COOP_MEMORY.md](docs/COOP_MEMORY.md) | Per-CPU TLSF + custom SHARED typed alloc |
| [docs/WASI.md](docs/WASI.md) | WASI preview1 syscalls, host requirements |
| [docs/RUNTIME.md](docs/RUNTIME.md) | Process / load model |
| [docs/MEMORY.md](docs/MEMORY.md) | Host pools vs guest linear memory |
| [docs/SOURCETREE.md](docs/SOURCETREE.md) | Folder layout |
| [docs/MOUNT.md](docs/MOUNT.md) | Mount design (from hosted era; still the contract) |
| [src/efi/README.md](src/efi/README.md) | EFI tree + build entrypoints |

---

## Layout

```
packages/metal/
├── include/pymergetic/metal/   mod-facing API (thin on this branch)
├── src/
│   ├── common/                 placeholder — host stack on archive branch
│   └── efi/                    freestanding UEFI + static virtio
├── mods/tests/                 harness .wasm guests
├── mods/apps/                  real guests (python)
└── scripts/                    setup|build|verify
```
(Hosted `patches/` live on `archive/multi-host-linux-zephyr-nuttx`.)

---

## Quick start

```bash
./scripts/setup edk2         # once — EDK2 + nasm + BaseTools
./scripts/build efi          # → build/efi/metal.efi (hello + memory)
./scripts/verify efi         # QEMU + OVMF smoke
```

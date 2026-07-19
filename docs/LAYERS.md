# Layers

Base stack — from firmware up to the point where a `.wasm` file can run.
Everything above the **wasm interface** is out of scope here.

```
targets = { efi }   # freestanding UEFI + static virtio
```

Hosted ports (linux, zephyr, nuttx, rump, unikraft) were removed from this
branch; full trees live on `archive/multi-host-linux-zephyr-nuttx`.

`efi` (`src/efi/`) is Metal/WAMR as a UEFI application: after
`ExitBootServices`, Metal owns heap and the runloop. I/O policy is
**static virtio only** (console → blk → net).

---

## Stack

```
┌─────────────────────────────────────────┐
│  WASM INTERFACE                         │  guest sees this
│  wasm32-wasip1 · WASI preview 1         │
├─────────────────────────────────────────┤
│  WASI host                              │
├─────────────────────────────────────────┤
│  WAMR                                   │
├─────────────────────────────────────────┤
│  runtime (Metal) — restore from archive │
├─────────────────────────────────────────┤
│  port / virtio (static)                 │
├─────────────────────────────────────────┤
│  UEFI (boot) → ExitBootServices         │
├─────────────────────────────────────────┤
│  hardware / VM (virtio)                 │
└─────────────────────────────────────────┘
```

Metal `src/common` host modules are on the archive branch until EFI WAMR needs
them. Bring-up design: [EFI.md](EFI.md). Tree/build: [src/efi/README.md](../src/efi/README.md).

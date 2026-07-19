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
│  runtime (Metal)                        │
├─────────────────────────────────────────┤
│  port / virtio (static)                 │
├─────────────────────────────────────────┤
│  UEFI (boot) → ExitBootServices         │
├─────────────────────────────────────────┤
│  hardware / VM (virtio)                 │
└─────────────────────────────────────────┘
```

See [src/efi/README.md](../src/efi/README.md) for bring-up.

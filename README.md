# extmod/metal — `pymergetic.metal` on mp

metalpython (**mp**) = **upywm + Metal**. This directory is the Metal
package (`pymergetic.metal` cards). Fleet map:
[os-sdk `REPO_LAYOUT.md`](https://github.com/pymergetic/os-sdk/blob/main/packages/REPO_LAYOUT.md).

Two trees, same git repo, **different products**:

| Branch | What | Who consumes it |
|--------|------|-----------------|
| **`main`** (this tree) | Card extmod nested at mp `extmod/metal` | unix / emcc / firmware seats |
| **`preview`** | Standalone CMake OS (shell, gfx, PXE, Doom) | [metal-doom](https://github.com/pymergetic/metal-doom) via sibling clone |

Do **not** point `METAL_ROOT` / Doom at this card tree. Preview screenshots
and CMake docs live on
[`preview`](https://github.com/pymergetic/metal/tree/preview).

Heap is wasmmod `pymergetic.util.mem` (`impl = "c"`). There is **no**
`pymergetic.metal.mem`.

```bash
make -C extmod/metal test      # host C, then firmware, unix, emcc
make -C extmod/metal upy
make -C extmod/metal firmware  # QEMU BIOS + UEFI (headless serial prove)
make -C extmod/metal/port BOARD=X86_64_BIOS run
make -C ports/unix MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 BUILD=build-metal
./ports/unix/build-metal/micropython -c "import pymergetic.metal as m; print(m.ready())"
```

Unix boot (`pm_metal_boot` before `pm_wasmmod_host_boot`): arena → async →
ip+lo → tls → http → metal `io_ops`. Packet / `io.fetch` bytes stay on the
arena, not the GC heap. Do not auto-listen ASGI on unix boot.

## Live cards

Faces are `pymergetic.util.gen` (`PM_MOD_EXPORT_C` / RS). Regen:
`make -C extmod/metal gen`.

| Layer | Cards | Host prove | Firmware image | Notes |
|-------|-------|------------|----------------|-------|
| bus / dt | `dt`, `bus.pci`, `bus.virtio` | yes | yes | virtio MMIO magic on firmware |
| drivers | `drivers`, `.net`, `.blk`, `.rtc` | yes | yes | |
| NIC | `drivers.net.{virtio,sim,tap,bge}` | yes | virtio only | tap = host; sim = in-process / emcc frames; bge = probe/queue, not FreeBSD `if_bge` |
| blk / rtc | `blk.virtio`, `blk.ide`, `rtc.{sim,cmos}` | yes | virtio-blk + cmos | |
| async | `async` | yes | yes | stackless ready-ring + timers — not preview N-CPU coop |
| ip | `net.ip` | yes | yes | IPv4 + ICMP/UDP/TCP; lo-reliable; L2 rexmit after `sim_drop` |
| L7 | `net.tls`, `net.http`, `http.asgi` | yes | tls+http (`fw_cdn.mk`) | ASGI is RS HTTP/1.0 on lo (one listen) |
| more L7 | `dns`, `ntp`, `dhcp`, `tftp`, `ssh`, `wg` | yes | **not linked yet** | ssh = ident+KEXINIT; wg = Noise_IK |
| memmap | `fw.memmap` | yes | yes | |
| µPy | `register_upy` | unix / firmware REPL | yes | `upy_guest_prove.py` |

`io.fetch` fills: unix mp stays POSIX+mbedtls; firmware parks in
`metal.net.http`; emcc WAN is `js.fetch` (sim L2 is frames, not HTTP).

## Not this product (stays on `preview`)

Shell / tabs / UART+FB consoles, gfx scanout (Bochs/VESA/radeon/i915),
audio, PS/2, CMake EFI/BIOS/`upload-pxe`, Dropbear viewport, lwIP IPv6,
N-CPU coop runners, Doom guest. Those are the CMake runtime. Bring them
across as **new cards** (`drivers.gfx`, `shell`, …) when firmware wants a
console — do not revive `_old/` or vendor patches from preview `setup.d`.

## Firmware vs 32-bit board names

| `BOARD` | What it actually is |
|---------|---------------------|
| `X86_64_BIOS` / `X86_64_UEFI` | Real QEMU seats |
| `X86_BIOS` | Include of the 64-bit BIOS makefile |
| `X86_UEFI` | Copies `BOOTX64.EFI` → `BOOTIA32.EFI` |

WAMR on firmware is wasmmod `ports/metal/wamr_freestanding.mk` (interp +
shared heap). Fast-jit stays off.

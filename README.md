# Metal

`pymergetic.metal` — async host, replaceable drivers, wasm + MicroPython
guests. Nested at metalpython `extmod/metal`. Heap is wasmmod
`pymergetic.util.mem`. There is no `pymergetic.metal.mem`.

**Boot → drivers → IP → fetch packs → go.** One card face on every seat
(host C, unix µPy, emcc, firmware BIOS/UEFI). A fill may differ (`io.fetch`
is POSIX, metal park, or `js.fetch`); the module does not.

```bash
make -C extmod/metal menu          # tree × seat × mode
make -C extmod/metal help          # print the matrix
make -C extmod/metal prove-all     # mp only: host + QEMU prove + unix + emcc; no run
```

One `pm_metal_boot()` on every mp seat: feed memmap (or a hosted span) →
arena → cards → probe → NICs → the C tree. Unix/emcc fill mmap/`js.fetch`;
BIOS/UEFI fill e820/EFI + virtio; RV1106 fills DRAM. Prove extras (blk
read, unbind) stay in board `main`, not in boot. Firmware GC is off. Do
not auto-listen ASGI on unix boot.

Regen faces: `make -C extmod/metal gen`.

## Cards

| Layer | Module | What |
|-------|--------|------|
| dt / bus | `dt`, `bus.pci`, `bus.virtio` | inventory, PCI, virtio MMIO |
| drivers | `drivers`, `.net`, `.blk`, `.rtc` | bind / unbind |
| NIC | `drivers.net.virtio` | virtio-net (firmware MMIO; host in-process vq) |
| NIC | `drivers.net.sim` | in-process Ethernet (emcc `library.js` frames) |
| NIC | `drivers.net.tap` | linux TAP; other seats compile, open is a no-op |
| NIC | `drivers.net.bge` | Broadcom class probe + queues |
| NIC | `drivers.net.gmac` | dwmac-4.20a RMII (RV1106); other seats fail closed |
| blk / rtc | `blk.virtio`, `blk.ide`, `rtc.cmos`, `rtc.sim` | virtio-blk, ram IDE, CMOS / sim clock |
| async | `async` | stackless ready-ring + timers + `run_until` |
| ip | `net.ip` | IPv4 ICMP/UDP/TCP; lo; L2 rexmit after `sim_drop` |
| tls / http | `net.tls`, `net.http` | mbedtls records; parked `io.fetch` |
| asgi | `net.http.asgi` | RS HTTP/1.0 listen (one socket) |
| L7 | `dns`, `ntp`, `dhcp`, `tftp`, `ssh`, `wg` | A / SNTP / DISCOVER / RRQ / SSH ident / Noise_IK |
| fw | `fw.memmap` | firmware memory map |
| µPy | `register_upy` | bind a guest generator |

`make -C extmod/metal test` / `prove-all` is **mp** host C, then firmware
prove (QEMU greps, RV1106 link), then unix, then emcc. No REPL, no Luckfox
flash. `./menu.sh prove-all` is the same idea across **upy + upywm + mp**.
It never uploads (no TFTP, no Luckfox Maskrom).

## Seats

Tree × seat. Same Python/C face; fill may differ. `upy` is vanilla µPy,
`upywm` is wasmmod without metal, `mp` is metalpython.

| | unix | emcc | bios | uefi | rv1106 | cards |
|--|------|------|------|------|--------|--------|
| upy | bpr | bp | — | — | — | — |
| upywm | bpr | bp | — | — | — | — |
| mp | bpr | bp | bpru | bpru | bpru | bp |

`cards` is the metal C binary (`metal-async-test`), no µPy. Only **mp**
has `extmod/metal`. upywm is wasmmod without metal, so that cell stays empty.

`b` build  `p` prove  `r` run  `u` upload. QEMU serial grep is **prove**;
interactive serial is **run**. **upload** puts the image on iron: BIOS/UEFI
to the TFTP/BOOTP host (`METAL_PXE_HOST`, OpenWrt-style `tar|ssh` into
`METAL_PXE_PATH`), RV1106 Maskrom (`LUCKFOX_IP`, `RKBIN`, `RKTOOLS`). RV1106
`run` is the same burn as `upload`. `prove-all` never uploads.

```bash
./menu.sh prove mp unix
./menu.sh run mp bios
./menu.sh upload mp bios
./menu.sh prove-all
```

Boards: `X86_64_BIOS`, `X86_64_UEFI`, `ARMV7_RV1106` (Luckfox Pico Max).
There is no 32-bit x86 board: a seat needs its own toolchain, not a 64-bit
image copied to `BOOTIA32.EFI`.

Env (never baked into the tree): `EMSDK`, `RKBIN`, `RKTOOLS`,
`LUCKFOX_IP`, `LUCKFOX_ETHADDR`, `METAL_PXE_HOST`, `METAL_PXE_USER`,
`METAL_PXE_PATH`, `METAL_PXE_SSH_OPTS`. QEMU CDN uses SLIRP `10.0.2.2`
(QEMU’s host gateway, not a LAN). DHCP NBP stays `undionly.kpxe` /
`ipxe.efi`; `metal.ipxe` / `metal-efi.ipxe` chain the uploaded payload.

---

Doom / the old CMake runtime: [`preview`](https://github.com/pymergetic/metal/tree/preview).

# Metal — follow-up TODO

Living list. Product surfaces are largely in place; what’s left is mostly **iron smoke** and a few optional gaps.

---

## Shipped (in-tree)

| Area | Notes |
|------|--------|
| **Boot / DT** | Shared harvest + seed (`gfx → UI → net → wasm → shell`); multi-node DT; virtio-blk + IDE; ACPI S5 power-off |
| **BIOS** | i386 Multiboot2 + PXE drop; VESA/Bochs/MB LFB detectors (x86_64 VESA RM still stub) |
| **Net** | `lo` + `eth0`…; virtio-net + **bge**; DHCPv4/v6 (stateful Metal client); bind-if |
| **Names** | `host` nodename (DHCP opt 12); VFS `/etc/hosts`; resolve = literal → local → hosts → DNS |
| **TFTP** | Async `pm_metal_net_tftp_get` + DHCP next-server / bootfile; guest proof `async_tftp` (EFI verify + QEMU `tftp=`/`bootfile=`) |
| **Shell / UI** | Linker-section cmds; tab focus; input lag fixes |
| **Audio** | virtio-snd → AC97 → null |
| **Wasm / FS** | Guest FS ABI + proofs; embed mods |

Details: `IO.md`, `LIBC_ASYNC.md`.

---

## Open — verify on hardware

- [ ] PXE i386 end-to-end (`./scripts/upload-pxe`)
- [ ] VESA LFB on iron (`metal-bios: fb/vesa …` on COM1)
- [ ] **bge** DHCP + ping/DNS (MAC/EEPROM path fixed; confirm lease)
- [ ] Shell typing snappy on BIOS hardware
- [ ] IDE usable from shell / guest
- [ ] Stateful DHCPv6 where RA has **M-flag**
- [ ] TFTP on iron next-server (QEMU path covered by `async_tftp`)

---

## Next step — MicroPython (core)

- [ ] Spike kernel µPy per [`MICROPYTHON.md`](MICROPYTHON.md): Python task =
      Metal task, MAP blob, Metal awaitables, ≥2 overlapping tasks on multi-CPU,
      no GIL; then bind/`metal.async`, C↔Py trampolines, signed zip (see integration
      checklist in that doc)

## Optional / later

- [ ] x86_64 BIOS VESA (needs LM→RM)
- [ ] Broader PCI NIC detect (beyond virtio + bge)
- [ ] WAMR Fast JIT on x86_64 (see `FAST_JIT.md`)

**Done (scanout):** `radeon_rv370` for T43 (`1002:5460`) — PCIe GART+CP present (staging fallback). `i915_855gm` T42 sample. Flip/tear-free still TODO.
**Deferred:** native modeset from dark — only if VESA detector fails on target HW.

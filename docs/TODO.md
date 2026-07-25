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
| **Input / keyboard** | Full USB HID keyboard/keypad page-0x07 coverage (punctuation, locks, F11/F12, Insert/Home/Delete/End, numpad, ISO 102nd-key, GUI/Menu) on both EFI+BIOS PS/2 paths; Home/End/Delete wired into shell line-editing; fixed `MetalShellHandleAscii`'s `ch<127` gate silently dropping `keyb gr`'s Latin-15 umlauts/ß/§/°/´; single-quote support in `ShellSplitArgv`; live-verified via scripted QEMU `sendkey` injection, not just static review; per-language keymaps modularized into self-registering `dev/input/keyb_layout/keyb_layout_{us,gr}.c` (linker-section registry, same idiom as `PM_METAL_SHELL_CMD` — new languages are a new file, no edits to `keyb.c`/`input.h`/linker scripts) |
| **Audio** | virtio-snd → AC97 → null |
| **Wasm / FS** | Guest FS ABI + proofs; embed mods |
| **MicroPython (core)** | Always-on µPy blob on EFI + BIOS; Python task = Metal task; host `py` shell + C↔Py trampolines; guest import + await (`async_py`); generic `PM_METAL_PY_BIND` table; `pymergetic.metal.{aio,process,mod}` + `pmcmd.*`; guest-visible sync **and async** fn resolve+call; exception/cancel/OOM isolation; boot-proofed overlap/yield fairness; `.fresh` isolated/`FRESH` mod instances from Python + dual-ABI guest-to-guest (`pm_metal_mod_fresh_{open,resolve,close}`); generated `.pyi` stubs (`scripts/gen_py_stubs.py`, wired into build); signed `stdlib.zip` + trust check + single-flight HTTP fetch + import-unshadowable regression proof; `mem` blob breakout line — gaps below |

Details: `IO.md`, `LIBC_ASYNC.md`, `MICROPYTHON.md`.

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

Spike landed (see Shipped table above) — see [`MICROPYTHON.md`](MICROPYTHON.md)'s
[Implementation status](MICROPYTHON.md#implementation-status) for the
validated (not aspirational) breakdown. Real gaps left:

- [ ] **Task-local GC spaces** — still one global run-lock serializing all
      Python bytecode; no per-task nursery, no all-parked compact barrier.
      (The former second-nested-async-task hang is resolved — see
      [`MICROPYTHON.md`](MICROPYTHON.md#resolved--gc-stack-scan-boundary-captured-once-vs-resumed-cross-cpu)
      — this item is now purely about GC granularity, not a bug.)
- [ ] **Fat stdlib / native self-register** — `stdlib.zip` is signed, trust-
      checked, and single-flight HTTP-fetched on ESP miss, but still ships
      one sample module; no `import sample`-shaped native self-register, no
      full `metal.fs`/`net`/… stdlib surface.
- [ ] **Python REPL as the system's main shell** — long-term direction, not
      started: `py -i` (an actual interactive REPL task) doesn't exist yet;
      once it does, boot should spawn it as the primary surface instead of
      today's C `shell.c` console, with its own welcome banner. See
      [`MICROPYTHON.md`](MICROPYTHON.md#later--python-repl-as-the-systems-main-shell-replaces-the-console).

## Optional / later

- [ ] x86_64 BIOS VESA (needs LM→RM)
- [ ] Broader PCI NIC detect (beyond virtio + bge)
- [ ] WAMR Fast JIT on x86_64 (see `FAST_JIT.md`)

**Done (scanout):** `radeon_rv370` for T43 (`1002:5460`) — PCIe GART+CP present (staging fallback). `i915_855gm` T42 sample. Flip/tear-free still TODO.
**Deferred:** native modeset from dark — only if VESA detector fails on target HW.

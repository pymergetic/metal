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
| **MicroPython (core)** | Always-on µPy blob on EFI + BIOS; Python task = Metal task; host `py` shell + C↔Py trampolines; guest import + await (`async_py`); generic `PM_METAL_PY_BIND` table; `pymergetic.metal.{aio,process,mod}` + `pmcmd.*`; guest-visible sync fn resolve+call; exception/cancel/OOM isolation; boot-proofed overlap/yield fairness — gaps below |

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

- [ ] **Known issue — second nested async task hangs OOM-isolation.**
      Pre-existing async-engine bug, unrelated to Python content or mod
      count: the boot sequence tolerates exactly **one** coroutine-backed
      async task spawn total across the whole run; a second one anywhere
      (even a bare repeated `pm_metal_py_run_script`, no new code) hangs
      the boot sequence with no crash/traceback right before the OOM
      proof. Currently blocks boot-proofing the guest-visible **async**
      call path (`pm_metal_py_fn_call_async`, implemented but unverified
      end-to-end). See [`MICROPYTHON.md`](MICROPYTHON.md#known-issue--second-nested-async-task-hangs-oom-isolation)
      for the full bisection writeup.
- [ ] **Signed stdlib zip** — `mods/py/stdlib.zip` exists and mounts on both
      ports, but no `.sig`/trust check, no HTTP single-flight fetch, no
      enforced `builtin→frozen→aot→wasm→py` import order.
- [ ] **Task-local GC spaces** — still one global run-lock serializing all
      Python bytecode; no per-task nursery, no all-parked compact barrier.
- [ ] `mem` shell command doesn't break out the µPy blob as its own line
      (counted inside aggregate `map`, per the doc's own Memory section).
- [ ] Write up `.mpy`/NLR park-resume safety note + blob-size-vs-Doom-HEAP note.
- [ ] Build-time `.pyi` generation — stubs under `typings/pymergetic/metal/`
      + `typings/pmcmd.pyi` are hand-written today, matching the bind-table
      surface; generating them from the linker-section rows is a later step.

## Optional / later

- [ ] x86_64 BIOS VESA (needs LM→RM)
- [ ] Broader PCI NIC detect (beyond virtio + bge)
- [ ] WAMR Fast JIT on x86_64 (see `FAST_JIT.md`)

**Done (scanout):** `radeon_rv370` for T43 (`1002:5460`) — PCIe GART+CP present (staging fallback). `i915_855gm` T42 sample. Flip/tear-free still TODO.
**Deferred:** native modeset from dark — only if VESA detector fails on target HW.

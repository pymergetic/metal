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
| **Input / keyboard** | Full USB HID keyboard/keypad page-0x07 coverage (punctuation, locks, F11/F12, Insert/Home/Delete/End, numpad, ISO 102nd-key, GUI/Menu) on both EFI+BIOS PS/2 paths; Home/End/Delete wired into shell line-editing; fixed `MetalShellHandleAscii`'s `ch<127` gate silently dropping `keyb de`'s Latin-15 umlauts/ß/§/°/´; single-quote support in `ShellSplitArgv`; live-verified via scripted QEMU `sendkey` injection, not just static review; per-language keymaps modularized into self-registering `dev/input/keyb_layout/keyb_layout_{us,de}.c` (linker-section registry, same idiom as `PM_METAL_SHELL_CMD` — new languages are a new file, no edits to `keyb.c`/`input.h`/linker scripts; the German layout's id was renamed `gr`→`de`, `gr` kept as an alias); status-bar 2-letter layout indicator + `Ctrl+Alt+Home` cycle chord; `ps2trace` shell command to log raw i8042 keyboard bytes for real-hardware-only scancode bugs QEMU can't reproduce |
| **Audio** | virtio-snd → AC97 → null |
| **Wasm / FS** | Guest FS ABI + proofs; embed mods |
| **MicroPython (core)** | Always-on µPy blob on EFI + BIOS; Python task = Metal task; host `py` shell + C↔Py trampolines; guest import + await (`async_py`); generic `PM_METAL_PY_BIND` table; `pymergetic.metal.{aio,process,mod}` + `pmcmd.*`; guest-visible sync **and async** fn resolve+call; exception/cancel/OOM isolation; boot-proofed overlap/yield fairness; `.fresh` isolated/`FRESH` mod instances from Python + dual-ABI guest-to-guest (`pm_metal_mod_fresh_{open,resolve,close}`); generated `.pyi` stubs (`scripts/gen_py_stubs.py`, wired into build); signed `stdlib.zip` + trust check + single-flight HTTP fetch + import-unshadowable regression proof; `mem` blob breakout line; **task-local GC spaces + true parallel bytecode** via opt-in isolated MicroPython contexts (per-CPU `mp_state_ctx` patch, own heap/own state, `PY_PROOF_PARALLEL`); persistent **Python REPL as the system's main boot shell** (`PY_STEP_REPL`, C console kept as `console`-escape fallback); real 26-module + 4 C-extmod (`binascii`/`random`/`hashlib`/`re`) + own `os`/`io` "Easy" `stdlib.zip` pack (`collections`, `heapq`, `bisect`, `functools`, `itertools`, `contextlib`, `copy`, `struct`, `string`, `pprint`, `operator`, `types`, `warnings`, `errno`, `keyword`, `abc`, `quopri`, `html`, `argparse`, `stat`, `pickle`, `inspect`, `traceback`, `logging`, `base64`, `fnmatch`, `os`/`os.path`, `io`, …) + reproducible, git-ignored, build-embedded build (`embed-stdlib.sh` → `py_zip_embed.inc.c`, no `stdlib.zip`/`.sig` in git history); **isolated contexts can import from `stdlib.zip`** (`pm_metal_py_ctx_create` seeds each context's own `sys.path`, `PY_PROOF_ISOLATED_STDLIB`); **rest of the Needs-glue tier shipped** — `time`/`datetime` (real RTC via `pymergetic.metal.time`, wired to EFI `gRT->GetTime()`/BIOS CMOS RTC + SNTP), `hmac` (`MICROPY_PY_BUILTINS_PROPERTY`), `zlib`/`gzip` (`extmod/moddeflate.c` + native `BytesIO`), `pathlib`/`shutil`/`tempfile`, `tarfile` (facade over Metal's existing C microtar, `pymergetic.metal.tar`), `unittest`, `textwrap` (regex-free rewrite — `re1.5` has no lookahead/lookbehind), `uu` (from-scratch codec, upstream `binascii` has no `b2a_uu`/`a2b_uu`); `ssl` shipped as `pymergetic.metal.tls` — an honestly Metal-flavored async TCP+TLS client (`net/tls/tls_conn.c` slot table + mbedTLS), not a CPython `ssl` shim; `threading`/`_thread` deliberately, permanently not shimmed (conflicts with Metal's coroutine task model — isolated contexts are the real answer to "run Python in parallel"); **`pymergetic.metal.net`/`net.http`** — thin async TCP/UDP socket + HTTP GET facade over the existing `dev/net/net.h`/lwIP stack (offline-safe loopback boot proof `PY_PROOF_NET`: listen/accept/connect/send/recv/dns); real on-disk `io.IOBase` streams (`io.py`'s `FileIO` now subclasses native `uio.IOBase`, so `gzip.open()`/`deflate.DeflateIO` can wrap an actual file, not just `BytesIO`); `json` (`extmod/modjson.c`) — gaps below |

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
- [ ] **ThinkPad: Backspace prints a digit instead of deleting.** Not reproducible in QEMU (its i8042 is quirk-free) and static review of the ASCII tables/shell handler found no software bug that turns `0x08` into a printed digit (see `docs/IO.md`'s "Real-HW scancode debugging" note) — needs a real trace. Boot, run `ps2trace on`, press Backspace, read the log (`ps2kbd: raw=XX` / `ps2kbd: sc=XX ... ascii=XX`) and compare against expected set-1 `0x0E`→`0x08`.

---

## Next step — MicroPython (core)

Spike landed (see Shipped table above) — see [`MICROPYTHON.md`](MICROPYTHON.md)'s
[Implementation status](MICROPYTHON.md#implementation-status) for the
validated (not aspirational) breakdown. Task-local GC, the REPL-as-main-shell
milestone, and a real "Easy" stdlib pack are now all shipped (see Shipped
table above); real gaps left:

- [ ] **Native self-register / full stdlib surface** — `stdlib.zip` now
      ships every module from the original Easy *and* Needs-glue tiers
      (`time`/`datetime`, `hmac`, `zlib`/`gzip`, `pathlib`/`shutil`/
      `tempfile`/`tarfile`, `unittest`, `textwrap`, `uu` — see
      `MICROPYTHON.md`'s [Second Needs-glue
      pass](MICROPYTHON.md#second-needs-glue-pass) for what each one
      actually needed), plus `ssl` as `pymergetic.metal.tls` (async
      TCP+TLS, not a CPython shim), plus `pymergetic.metal.net`/`net.http`,
      real on-disk `io.IOBase` streams, and `json` (see `MICROPYTHON.md`'s
      [Network + FS/IO
      polish](MICROPYTHON.md#network--fsio-polish-pymergeticmetalnet-ioiobase-json)).
      `threading`/`_thread` is the one deliberate, permanent skip (see
      `MICROPYTHON.md`). What's left: no `import sample`-shaped native
      self-register, no full `metal.fs`/`net`/… stdlib-shaped orchestration
      layer, no CPython `socket`/`select` shim, no `umqtt`/`requests`/
      `aiohttp` ports, no new VFS/mount table/per-context CWD/FS jail
      (deliberate scope boundary, not a gap).

## Optional / later

- [ ] x86_64 BIOS VESA (needs LM→RM)
- [ ] Broader PCI NIC detect (beyond virtio + bge)
- [ ] WAMR Fast JIT on x86_64 (see `FAST_JIT.md`)

**Done (scanout):** `radeon_rv370` for T43 (`1002:5460`) — PCIe GART+CP present (staging fallback). `i915_855gm` T42 sample. Flip/tear-free still TODO.
**Deferred:** native modeset from dark — only if VESA detector fails on target HW.

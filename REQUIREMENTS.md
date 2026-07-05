# pymergetic-metal — requirements

OS core on Zephyr: boot, **4-fold memory model**, portable **`.o` module loading**, and **`pm_port`** shims. No CPython here — see `packages/kernel/REQUIREMENTS_PYTHON.md`.

Same behavior on **fake metal** (`native_sim`) and **real metal** (QEMU / HW).

---

## Phase 0 — Scope

- [ ] **Targets**: `native_sim/native/64`, `qemu_x86_64`, eventual HW board
- [ ] **Kernel static / kernel heap**: Zephyr defaults; document reserved budget
- [ ] **App static / app heap**: single growable pool for runtime + loaded mods
- [ ] **Mod model**: portable `.o` blobs linked against kernel API; load into app-static slots
- [ ] **Port layer**: POSIX-ish surface for kernel/mods (`stdio`, time, future heap hooks)

**Verification:** scope recorded; board overlays match fake vs real metal.

---

## Phase 1 — Zephyr shell

**`third_party/zephyr/`** · `v4.4.0`

- [x] submodule
- [x] west workspace in `packages/metal/` (gitignored `modules/`, `build/`, `.venv/`)
- [x] hello app + out-of-tree module under `src/pymergetic/metal/`
- [ ] board-specific RAM/linker maps

**Verification:** `./scripts/build -b native_sim/native/64 app` runs; prints `pymergetic/metal: ok`.

---

## Phase 2 — App heap (`ram/`)

- [ ] Boot probe: kernel budget + minimum app reserve → one app heap arena
- [ ] Route dynamic allocation for metal + loaded mods through app heap (not ad-hoc `k_malloc` / competing picolibc heap without policy)
- [ ] Kconfig caps for fake vs real metal

**Verification:** allocation tests; no silent OOM from wrong arena.

---

## Phase 3 — Mod loader (`mod/`)

- [ ] Parse/load relocatable `.o` into reserved app-static region
- [ ] Resolve symbols against kernel export table
- [ ] Slot accounting vs app heap

**Verification:** load a trivial test mod; call one exported entry point.

---

## Phase 4 — Port layer (`port/`)

- [ ] `pm_port` headers implemented on Zephyr + native_sim
- [ ] Hooks for kernel to use app heap / stdio / time

**Verification:** kernel smoke test can link against `pm_port` without pulling CPython into metal.

---

## Appendix — Zephyr built-ins vs kernel needs

Zephyr provides picolibc, optional mbedTLS, UUID via `CONFIG_UUID` — **not** desktop `libz`, OpenSSL, libffi, etc. Those are **kernel** vendored deps, not metal submodules.

# Doom → Metal async build plan

**Status:** A–D wired (parked app; `METAL_DOOM_BUILD=1` opt-in)  
**Constraint:** await only from `pm_metal_guest_step`. External `doomgeneric` stays vanilla.

## Run (EFI + BIOS/PXE — same package layout)

```bash
METAL_DOOM_BUILD=1 ./scripts/build.d/port/efi/doom.sh   # → build/doom/
# EFI:
METAL_DOOM_BUILD=1 ./scripts/build efi
./scripts/run efi --gtk          # stages mods/apps/doom/ onto ESP
# shell: run doom

# BIOS/PXE (HTTP :8080 mirrors TFTP; fetch on run/tab if not on ESP):
METAL_DOOM_BUILD=1 ./scripts/upload-pxe --build
# shell: run doom   /  tab doom
```

Package files must be served at `http://<next-server|:192.168.10.1>:8080/mods/apps/doom/…` when not staged on ESP. Boot shows `pkg cached` (local preload) or `pkg lazy` (no auto-download). HTTP seed runs only when you `run`/`tab doom`.

| Env | Meaning |
|-----|---------|
| `METAL_DOOM_BUILD=1` | build `build/doom/{doom.wasm,doom.x86_64.aot,doom.i386.aot,doom1.wad}` (+ `.sig` when PKI) |
| `METAL_DOOM_WINDOWED` | `1` (default) tab/windowed; `0` fullscreen |
| `METAL_DOOM_OUT_DIR` | override build output (default `build/doom`) |
| `METAL_DOOM_DIR` | override stage source (default = OUT) |
| `METAL_PKI_DIR` | external PKI (`./scripts/pki init`); signs `doom.*.aot.sig` / `doom.wasm.sig` |
| `METAL_TRUST_MODE=off\|soft\|enforce` | trust policy (default soft if PKI) |
| `METAL_TRUST=1` | ⇒ enforce + bake fails if PKI incomplete |

## Rule

Deep Doom C stays **sync CPU** once its inputs are in memory. Every world wait (FS, wipe frame pacing, quit, audio drain) is owned by the **`guest_step` stem** — preload / flag / one-frame advance — not `setjmp` yield from inside `W_OpenFile` or the wipe loop.

| Do | Don’t |
|----|--------|
| Cut at stem; push async outward | `setjmp`/`longjmp` stackful yield |
| `--wrap`, alternate `.c` in `mods/apps/doom` | Hand-edit `external/doomgeneric` |
| Memory wad after async preload | Sync `fopen` / sync FS inside Create |

## Callpath stem

```
guest_step
  ST_MKDIR:     async mkdir saves/
  ST_SIZE…READ: async IWAD → memory wad
  ST_CREATE:    doomgeneric_Create → await(0)
  ST_TICK+:     Tick (or wipe-only) → save I/O → audio drain → present → sleep
```

## Inventory

| Site | Status | Strategy |
|------|--------|----------|
| Outer loop / `guest_step` | done | `metal_main.c` |
| `singletics` / `TryRunTics` | done | force `singletics=1` |
| `DG_*` time / blit / keys | done | `doomgeneric_metal.c` |
| Hybrid controls | done | arrows move/turn + Alt-strafe (classic); WASD → same; mouse look optional; E/Space use, Ctrl fire |
| Fill tab / FS | done | stretch-fill whole surface (run + tab, no letterbox) |
| Display pace | done | phase-locked `sleep_until` on 60 Hz mono grid (AOT-safe; `async_frame` host-ready) |
| Present | done | `async_present` after blit; lower half is work-only (no busy-wait) |
| `M_FileExists` / `I_AtExit` | done | `--wrap` |
| `W_OpenFile` / wad class | done | async preload → memory wad |
| Wipe melt loop | done | `--wrap=D_Display` + `doomgeneric_Tick` (1 frame / Tick) |
| `I_Quit` / `I_Error` | done | `--wrap` → wasi exit → `DONE` / `ERROR` |
| Present fence | done | `blit_bgra` → shadow; stem `await(present)` once/frame (chunked LFB + yield on iron) |
| Offline AOT | done | `doom.{x86_64,i386}.aot` via `wamrc` (host picks matching AOT, else `.wasm`) |
| Save/load | done | memory serialize + stem `fs_*_async` |
| Audio | done | `DG_sound_module` → queue + stem `drain` |
| Config / screenshot `fopen` | omit v1 | no-op |
| Net / multip | omit | already `#undef FEATURE_MULTIPLAYER` |
| `setjmp` deep yield | omit | never |

## Phases

### A — Tick hygiene

- Wipe: `--wrap=D_Display` — one `wipe_ScreenWipe` per Tick; `--wrap=doomgeneric_Tick` freezes sim while melting.
- `--wrap=I_Quit` / `I_Error` → wasi exit.
- Display: default fullscreen; `-w` → tab surface.

### B — Async IWAD stem

`guest_step` before `Create`: size → read → memory wad → `Create` (CPU only).

### C — Saves

- `--wrap=G_DoSaveGame`: `fmemopen` + archive → pending write; stem `fs_write_async`.
- `--wrap=G_DoLoadGame`: request read → stem size/read → retry load from memory.
- `--wrap=M_GetSaveGameDir` → `/mods/apps/doom/saves/`; stem `mkdir_async`.
- `--wrap=M_ReadSaveStrings`: in-memory slot titles (updated on save).
- Config / screenshots: omit.

### D — Audio

- `i_sound_metal.c`: `DG_sound_module` mixes to S16LE stereo 22050 → `pm_metal_audio_queue`.
- Stem `await(pm_metal_audio_drain)` when queued.
- Stub `DG_music_module`; `FEATURE_SOUND` on; no `-nosound`.

### E — Wire-up / verify

- Opt-in verify / autostart marker; docs.

## Mechanism map

| Mechanism | Use for |
|-----------|---------|
| `DG_*` | time, blit, keys, sleep no-op |
| `guest_step` states | IWAD, wipe pacing, quit, saves, audio drain |
| `--wrap` | `M_FileExists`, `I_AtExit`, `I_Quit`, `I_Error`, `I_Sleep`, `D_Display`, `doomgeneric_Tick`, save/load |
| Alternate `.c` | wad class, sound module |
| Compile flags | `-DFEATURE_SOUND` |
| `patches/doomgeneric` | none |

## Display modes (`run` / `tab`)

| Mode | Draw target | Scale | Shell chrome |
|------|-------------|-------|--------------|
| **Fullscreen** (`run doom`) | `DEFAULT` | stretch-fill | suppressed while guest focus |
| **Windowed** (`tab doom`) | tab surface | stretch-fill | kept |

## Key paths

- `mods/apps/doom/` — Metal glue
- `scripts/build.d/port/efi/doom.sh` — wasm link + wraps
- `external/doomgeneric/` — vanilla pin via `./scripts/setup doomgeneric`
- `docs/LIBC_ASYNC.md` — await / FS / omit `setjmp`

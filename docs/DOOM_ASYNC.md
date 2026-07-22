# Doom → Metal async build plan

**Status:** plan (parked app; `METAL_BUILD_DOOM=1` opt-in)  
**Constraint:** await only from `pm_metal_guest_step`. External `doomgeneric` stays vanilla.

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
  step 0:  [async IWAD…] → doomgeneric_Create → D_DoomMain → D_DoomLoop (one Tick) → return
  step 1+: doomgeneric_Tick → TryRunTics → S_UpdateSounds → D_Display → await(frame sleep)
```

Vanilla already returns after one Tick (`D_DoomLoop`). Wipe branch (`d_main.c` melt + `I_Sleep`) is the main Tick hog.

## Inventory

| Site | Status | Strategy |
|------|--------|----------|
| Outer loop / `guest_step` | done | `metal_main.c` |
| `singletics` / `TryRunTics` | done | force `singletics=1` |
| `DG_*` time / blit / keys | done | `doomgeneric_metal.c` |
| `M_FileExists` / `I_AtExit` | done | `--wrap` |
| `W_OpenFile` / wad class | partial | sync FS today → Phase B preload |
| Wipe melt loop | gap | one frame / Tick |
| `I_Quit` / `I_Error` | gap | `--wrap` → `DONE` / `ERROR` |
| `FEATURE_SOUND` | gap | A: `-UFEATURE_SOUND`; D: Metal module |
| Save/load `fopen` | gap | Phase C stem + async FS |
| Config / screenshot `fopen` | omit v1 | no-op |
| Net / multip | omit | already `#undef FEATURE_MULTIPLAYER` |
| `setjmp` deep yield | omit | never |

## Phases

### A — Tick hygiene (re-add unblocker)

- Wipe: Metal-owned state — one `wipe_ScreenWipe` advance per Tick; no nested `I_Sleep` loop.
- `--wrap=I_Quit` / `I_Error` → session `PM_METAL_DONE` / error.
- Compile `-UFEATURE_SOUND` (or null stubs) until D.
- **Display:** default fullscreen (DEFAULT surface); `-w` → tab surface only (see below).
- Done when: menu quit ends session; wipe doesn’t hog one step; no sound-module crash; `-w` keeps chrome.

### B — Async IWAD stem

`guest_step` before `Create`:

1. `pm_metal_fs_size_async` → await → `pm_metal_fs_result`
2. malloc + `pm_metal_fs_read_async` → await
3. Install memory wad class (`OpenFile` attaches buffer; no FS)
4. `doomgeneric_Create` (CPU only)

Retire sync `pm_metal_fs_size` / `read` from the open path.

### C — Saves (optional v1)

- Save/load: deep path sets “need I/O” + buffer; `guest_step` does async write/read then resumes.
- Config / screenshots: omit or no-op.

### D — Audio

- Metal `DG_sound_module`: `pm_metal_audio_queue` (sync); `pm_metal_audio_drain` await between ticks if backpressure.
- Music stub OK.

### E — Wire-up / verify

- Opt-in verify / autostart marker; docs. Minimum bar: **A**. “Fully async FS”: **A+B**.

## Mechanism map

| Mechanism | Use for |
|-----------|---------|
| `DG_*` | time, blit, keys, sleep no-op |
| `guest_step` states | IWAD preload, wipe, quit, saves |
| `--wrap` | `M_FileExists`, `I_AtExit`, `I_Quit`, `I_Error` |
| Alternate `.c` | wad class, sound module (omit `w_file_stdc.c`) |
| Compile flags | `-UFEATURE_SOUND` until D |
| `patches/doomgeneric` | none |

## Display modes (`-w`)

| Mode | Argv | Draw target | Shell chrome |
|------|------|-------------|--------------|
| **Fullscreen** (default) | no `-w` | `PM_METAL_GFX_SURFACE_DEFAULT` — letterbox scale to GOP | suppressed while guest focus (game owns FB) |
| **Windowed** | `-w` | active tab `pm_metal_ui_tab_surface` — scale into tab content | kept: tab strip / status / input; do not wipe guest content rect |

Guest: `M_CheckParm("-w")` in `DG_Init` (after `myargv` is set).  
`metal_main` may append `-w` when `METAL_DOOM_WINDOWED=1` (until shell forwards guest argv).

Host (needed for windowed): while session live on a tab surface, chrome paint must **skip** clearing that tab’s content; `shell_poll` still paints chrome under guest focus when `pm_metal_gfx_draw_surface() != DEFAULT`.

## First cut

Ship **A** (incl. `-w` display), then **B**. C/D follow. Re-vendor: `./scripts/setup doomgeneric`.

## Key paths

- `mods/apps/doom/` — Metal glue
- `scripts/build.d/port/efi/doom.sh` — wasm link + wraps
- `external/doomgeneric/` — vanilla pin via `scripts/setup doomgeneric`
- `docs/LIBC_ASYNC.md` — await / FS / omit `setjmp`

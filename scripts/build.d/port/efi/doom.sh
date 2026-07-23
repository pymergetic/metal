#!/usr/bin/env bash
# Build doomgeneric → build/doom/doom.wasm (+ doom1.wad).
# Parked from default builds — opt-in: METAL_DOOM_BUILD=1
# Shared by EFI ESP + BIOS/PXE staging (scripts/lib/doom.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/doom.sh"
WASI_SDK="${ROOT}/.tools/wasi-sdk"
CLANG="${WASI_SDK}/bin/clang"
SYSROOT="${WASI_SDK}/share/wasi-sysroot"
DG="${ROOT}/external/doomgeneric/doomgeneric"
OUT_DIR="$(pm_metal_doom_out_dir)"
WAD_CACHE="${ROOT}/.tools/doom/doom1.wad"
TARGET=wasm32-wasip1

if [[ "${METAL_DOOM_BUILD:-0}" != "1" ]]; then
	echo "doom: parked (set METAL_DOOM_BUILD=1 to build)" >&2
	exit 0
fi

# 0 = interactive forever (default).
METAL_DOOM_MAX_TICKS="${METAL_DOOM_MAX_TICKS:-0}"
# Display: run → fullscreen, tab → tab surface (no compile -w).

if [[ ! -x "${CLANG}" ]]; then
	echo "doom: wasi-sdk missing (${CLANG})" >&2
	exit 1
fi
if [[ ! -d "${DG}" ]]; then
	echo "doom: missing ${DG} — run ./scripts/setup doomgeneric" >&2
	exit 1
fi
if [[ ! -f "${WAD_CACHE}" ]]; then
	echo "doom: missing shareware WAD at ${WAD_CACHE} — run ./scripts/setup doomgeneric" >&2
	exit 1
fi

mkdir -p "${OUT_DIR}"

# Same object list as upstream Makefile, minus X11 platform; + Metal.
# Omit w_file_stdc / i_sdlsound / i_sdlmusic — Metal provides those symbols.
SRC_DOOM=(
	dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop
	d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info
	i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio
	m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc
	m_random p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map
	p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight p_spec
	p_switch p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane
	r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff s_sound
	tables v_video wi_stuff w_checksum w_file w_main w_wad z_zone
	i_video doomgeneric
)

OBJS=()
for s in "${SRC_DOOM[@]}"; do
	OBJS+=("${DG}/${s}.c")
done
OBJS+=(
	"${ROOT}/mods/apps/doom/w_file_metal.c"
	"${ROOT}/mods/apps/doom/m_fileexists_metal.c"
	"${ROOT}/mods/apps/doom/g_checkdemo_atexit.c"
	"${ROOT}/mods/apps/doom/doomgeneric_metal.c"
	"${ROOT}/mods/apps/doom/i_input_metal.c"
	"${ROOT}/mods/apps/doom/metal_main.c"
	"${ROOT}/mods/apps/doom/metal_quit.c"
	"${ROOT}/mods/apps/doom/metal_sleep.c"
	"${ROOT}/mods/apps/doom/d_display_metal.c"
	"${ROOT}/mods/apps/doom/g_save_metal.c"
	"${ROOT}/mods/apps/doom/i_sound_metal.c"
)

# Upstream doomgeneric is noisy under -Wall; Metal glue stays clean.
DG_WARN=(
	-Wno-unused-variable
	-Wno-unused-but-set-variable
	-Wno-unused-const-variable
	-Wno-absolute-value
)

echo "doom: compiling wasm (METAL_DOOM_MAX_TICKS=${METAL_DOOM_MAX_TICKS})"
# Native Doom size: one Metal blit scales to the surface (no DG upscale).
"${CLANG}" \
	--target="${TARGET}" \
	--sysroot="${SYSROOT}" \
	-O2 -Wall "${DG_WARN[@]}" \
	-DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
	-DFEATURE_SOUND \
	-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
	"-DMETAL_DOOM_MAX_TICKS=${METAL_DOOM_MAX_TICKS}" \
	-I "${ROOT}/include" \
	-I "${DG}" \
	-I "${ROOT}/mods/apps/doom" \
	-I "${ROOT}/mods/apps/doom/ide_stubs" \
	-Wl,--export=main \
	-Wl,--export=pm_metal_guest_step \
	-Wl,--export=malloc \
	-Wl,--export=free \
	-Wl,--export=__heap_base \
	-Wl,--export=__data_end \
	-Wl,--wrap=M_FileExists \
	-Wl,--wrap=I_AtExit \
	-Wl,--wrap=I_Quit \
	-Wl,--wrap=I_Error \
	-Wl,--wrap=I_Sleep \
	-Wl,--wrap=D_Display \
	-Wl,--wrap=doomgeneric_Tick \
	-Wl,--wrap=G_SaveGame \
	-Wl,--wrap=G_DoSaveGame \
	-Wl,--wrap=G_DoLoadGame \
	-Wl,--wrap=M_GetSaveGameDir \
	-Wl,--wrap=M_ReadSaveStrings \
	-Wl,--allow-undefined \
	-Wl,--stack-first \
	-Wl,-z,stack-size=1048576 \
	-Wl,--initial-memory=16777216 \
	-Wl,--max-memory=67108864 \
	-o "${OUT_DIR}/doom.wasm" \
	"${OBJS[@]}"

cp -f "${WAD_CACHE}" "${OUT_DIR}/doom1.wad"
# Always resign or clear stale .sig (soft trust fails on mismatched sig).
pm_metal_doom_sign_or_clear "${OUT_DIR}" || true
echo "doom: ok -> ${OUT_DIR}/doom.wasm ($(wc -c <"${OUT_DIR}/doom.wasm") bytes)"

# Offline AOT — arch infix in the filename (never bare doom.aot).
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/aot.sh"
if ! pm_metal_aot_compile_all "${OUT_DIR}/doom.wasm" "${OUT_DIR}/doom"; then
	echo "doom: AOT skipped (install wamrc: ./scripts/setup wamrc)" >&2
fi

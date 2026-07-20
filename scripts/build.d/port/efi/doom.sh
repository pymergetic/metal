#!/usr/bin/env bash
# Build doomgeneric → build/efi/doom/doom.wasm (+ stage helpers copy WAD).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
WASI_SDK="${ROOT}/.tools/wasi-sdk"
CLANG="${WASI_SDK}/bin/clang"
SYSROOT="${WASI_SDK}/share/wasi-sysroot"
DG="${ROOT}/external/doomgeneric/doomgeneric"
OUT_DIR="${METAL_DOOM_OUT_DIR:-${ROOT}/build/efi/doom}"
WAD_CACHE="${ROOT}/.tools/doom/doom1.wad"
TARGET=wasm32-wasip1

# 0 = interactive forever (default). Verify sets METAL_DOOM_MAX_TICKS=120
# into METAL_DOOM_OUT_DIR=build/efi/doom-verify so it does not clobber play builds.
METAL_DOOM_MAX_TICKS="${METAL_DOOM_MAX_TICKS:-0}"

if [[ ! -x "${CLANG}" ]]; then
	echo "doom: wasi-sdk missing (${CLANG})" >&2
	exit 1
fi
if [[ ! -d "${DG}" ]]; then
	echo "doom: missing ${DG} — clone ozkl/doomgeneric into external/" >&2
	exit 1
fi
if [[ ! -f "${WAD_CACHE}" ]]; then
	echo "doom: missing shareware WAD at ${WAD_CACHE}" >&2
	exit 1
fi

mkdir -p "${OUT_DIR}"

# Same object list as upstream Makefile, minus X11 platform; + Metal.
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
	i_input i_video doomgeneric
)

OBJS=()
for s in "${SRC_DOOM[@]}"; do
	OBJS+=("${DG}/${s}.c")
done
OBJS+=(
	"${ROOT}/mods/apps/doom/w_file_metal.c"
	"${ROOT}/mods/apps/doom/m_fileexists_metal.c"
	"${ROOT}/mods/apps/doom/doomgeneric_metal.c"
	"${ROOT}/mods/apps/doom/metal_main.c"
)

echo "doom: compiling wasm (METAL_DOOM_MAX_TICKS=${METAL_DOOM_MAX_TICKS})"
"${CLANG}" \
	--target="${TARGET}" \
	--sysroot="${SYSROOT}" \
	-O2 -Wall \
	-DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE \
	-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
	"-DMETAL_DOOM_MAX_TICKS=${METAL_DOOM_MAX_TICKS}" \
	-I "${ROOT}/include" \
	-I "${DG}" \
	-Wl,--export=main \
	-Wl,--export=pm_metal_guest_step \
	-Wl,--wrap=M_FileExists \
	-Wl,--allow-undefined \
	-Wl,--stack-first \
	-Wl,-z,stack-size=1048576 \
	-Wl,--max-memory=67108864 \
	-o "${OUT_DIR}/doom.wasm" \
	"${OBJS[@]}"

cp -f "${WAD_CACHE}" "${OUT_DIR}/doom1.wad"
echo "doom: ok -> ${OUT_DIR}/doom.wasm ($(wc -c <"${OUT_DIR}/doom.wasm") bytes)"

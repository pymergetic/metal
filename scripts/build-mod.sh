#!/usr/bin/env bash
# Build a freestanding mod .o — PM_MAX_VIS=PM_VIS_MOD (minimal API slice).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MOD_DIR="${1:?usage: build-mod.sh <mods/hello> [out.o]}"
OUT="${2:-${ROOT}/mods/build/$(basename "$MOD_DIR").o}"

CC="${CC:-cc}"
CFLAGS=(
	-std=c11
	-ffreestanding
	-fno-builtin
	-Wall
	-Wextra
	-DPM_MAX_VIS=0
	-I"${ROOT}/include"
	-c
)

mkdir -p "$(dirname "$OUT")"
shopt -s nullglob
sources=("${MOD_DIR}"/*.c)
if [[ ${#sources[@]} -eq 0 ]]; then
	echo "no .c files in ${MOD_DIR}" >&2
	exit 1
fi

objs=()
for src in "${sources[@]}"; do
	obj="$(mktemp "${TMPDIR:-/tmp}/pm_mod.XXXXXX.o")"
	"${CC}" "${CFLAGS[@]}" "$src" -o "$obj"
	objs+=("$obj")
done

if [[ ${#objs[@]} -eq 1 ]]; then
	mv "${objs[0]}" "$OUT"
else
	ld -r -o "$OUT" "${objs[@]}"
	rm -f "${objs[@]}"
fi

echo "OK: ${OUT}"

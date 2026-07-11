#!/usr/bin/env bash
# Build mods/*/main.c -> build/mods/*.wasm with wasi-sdk (wasm32-wasip1).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WASI_SDK="${ROOT}/.tools/wasi-sdk"
CLANG="${WASI_SDK}/bin/clang"
SYSROOT="${WASI_SDK}/share/wasi-sysroot"
OUT="${ROOT}/build/mods"

if [ ! -x "${CLANG}" ]; then
	echo "wasi-sdk clang not found at ${CLANG} (see scripts/setup-tools.sh)" >&2
	exit 1
fi

mkdir -p "${OUT}"

for mod_dir in "${ROOT}"/mods/*/; do
	name="$(basename "${mod_dir}")"
	src="${mod_dir}main.c"
	[ -f "${src}" ] || continue

	out="${OUT}/${name}.wasm"
	echo "mod: ${name}"
	"${CLANG}" --target=wasm32-wasip1 --sysroot="${SYSROOT}" \
		-O2 -Wall -Wextra \
		-I "${ROOT}/include" \
		-o "${out}" "${src}"
done

echo "mods -> ${OUT}"

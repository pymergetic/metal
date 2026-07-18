#!/usr/bin/env bash
# Cross-build vendored CPython (external/cpython) to wasm32-wasip1-threads via
# upstream Tools/wasm/wasi, then copy the guest binary into build/cpython/.
#
# Threads from day one — same triple as scripts/build.d/guest/mod.sh — so we hit
# shared-memory / wasi-threads gaps early instead of bolting them on later.
# Host runner for the upstream smoke check is .tools/wasmtime (setup-tools.sh).
# Packaged as /mods/apps/python.wasm when PM_METAL_APP_PYTHON=1
# (scripts/lib/guest-package.sh).
#
# Prereqs:
#   scripts/setup.d/deps/cpython.sh
#   scripts/setup.d/deps/tools.sh   # wasi-sdk 24 + wasmtime (matches CPython WASI_SDK_VERSION)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CPYTHON_DIR="${ROOT}/external/cpython"
WASI_SDK="${ROOT}/.tools/wasi-sdk"
WASMTIME="${ROOT}/.tools/wasmtime/bin/wasmtime"
OUT_DIR="${ROOT}/build/cpython"
HOST_TRIPLE="${HOST_TRIPLE:-wasm32-wasip1-threads}"

if [ ! -f "${CPYTHON_DIR}/configure" ]; then
	echo "external/cpython missing (run scripts/setup.d/deps/cpython.sh)" >&2
	exit 1
fi
if [ ! -x "${WASI_SDK}/bin/clang" ]; then
	echo "wasi-sdk not found at ${WASI_SDK} (run scripts/setup.d/deps/tools.sh)" >&2
	exit 1
fi
if [ ! -x "${WASMTIME}" ]; then
	echo "wasmtime not found at ${WASMTIME} (run scripts/setup.d/deps/tools.sh)" >&2
	exit 1
fi

# Upstream host-runner template (Tools/wasm/wasi) with threads enabled — the
# stock default leaves "--wasm threads=y --wasi threads=y" commented out.
HOST_RUNNER="${WASMTIME} run \
--wasm max-wasm-stack=16777216 \
--wasm threads=y --wasi threads=y \
--dir {HOST_DIR}::{GUEST_DIR} \
--env {ENV_VAR_NAME}={ENV_VAR_VALUE}"

export WASI_SDK_PATH="${WASI_SDK}"
export PATH="$(dirname "${WASMTIME}"):${PATH}"

cd "${CPYTHON_DIR}"
python3 Tools/wasm/wasi build \
	--wasi-sdk "${WASI_SDK}" \
	--host-triple "${HOST_TRIPLE}" \
	--host-runner "${HOST_RUNNER}" \
	-- \
	"$@"

PYTHON_WASM="${CPYTHON_DIR}/cross-build/${HOST_TRIPLE}/python.wasm"
if [ ! -f "${PYTHON_WASM}" ]; then
	echo "expected ${PYTHON_WASM} missing after build" >&2
	exit 1
fi

mkdir -p "${OUT_DIR}"
cp -f "${PYTHON_WASM}" "${OUT_DIR}/python.wasm"
# Convenience wrapper for wasmtime smoke (Metal/WAMR load comes later).
cat > "${OUT_DIR}/python.sh" <<EOF
#!/bin/sh
exec ${WASMTIME} run \\
	--wasm max-wasm-stack=16777216 \\
	--wasm threads=y --wasi threads=y \\
	${OUT_DIR}/python.wasm "\$@"
EOF
chmod +x "${OUT_DIR}/python.sh"

echo "cpython wasm (${HOST_TRIPLE}) -> ${OUT_DIR}/python.wasm"

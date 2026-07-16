#!/usr/bin/env bash
# Fetch wasmtime CLI + wasi-sdk into .tools/ (gitignored).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="${ROOT}/.tools"
WASMTIME_VER="${WASMTIME_VER:-v24.0.0}"
WASI_SDK_TAG="${WASI_SDK_TAG:-24}"
WASI_SDK_FULL="${WASI_SDK_FULL:-24.0}"
ARCH="$(uname -m)"

case "${ARCH}" in
x86_64) WASI_ARCH="x86_64" ;;
aarch64) WASI_ARCH="arm64" ;;
*)
	echo "unsupported arch: ${ARCH}" >&2
	exit 1
	;;
esac

mkdir -p "${TOOLS}"

WASMTIME_BIN="${TOOLS}/wasmtime/bin/wasmtime"

if [[ -x "${TOOLS}/wasmtime/wasmtime" && ! -x "${WASMTIME_BIN}" ]]; then
	mkdir -p "${TOOLS}/wasmtime/bin"
	mv "${TOOLS}/wasmtime/wasmtime" "${WASMTIME_BIN}"
fi

if [[ ! -x "${WASMTIME_BIN}" ]]; then
	echo "=== wasmtime ${WASMTIME_VER} ==="
	tmp="$(mktemp -d)"
	trap 'rm -rf "${tmp}"' EXIT
	curl -fsSL -o "${tmp}/wasmtime.tar.xz" \
		"https://github.com/bytecodealliance/wasmtime/releases/download/${WASMTIME_VER}/wasmtime-${WASMTIME_VER}-${ARCH}-linux.tar.xz"
	tar -xJf "${tmp}/wasmtime.tar.xz" -C "${tmp}"
	rm -rf "${TOOLS}/wasmtime"
	mv "${tmp}/wasmtime-${WASMTIME_VER}-${ARCH}-linux" "${TOOLS}/wasmtime"
	mkdir -p "${TOOLS}/wasmtime/bin"
	if [[ -x "${TOOLS}/wasmtime/wasmtime" && ! -x "${WASMTIME_BIN}" ]]; then
		mv "${TOOLS}/wasmtime/wasmtime" "${WASMTIME_BIN}"
	fi
	trap - EXIT
	rm -rf "${tmp}"
fi

WASI_SDK_ROOT="${TOOLS}/wasi-sdk"
if [[ ! -x "${WASI_SDK_ROOT}/bin/clang" ]]; then
	echo "=== wasi-sdk ${WASI_SDK_FULL} ==="
	tmp="$(mktemp -d)"
	curl -fsSL -o "${tmp}/wasi-sdk.tar.gz" \
		"https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK_TAG}/wasi-sdk-${WASI_SDK_FULL}-${WASI_ARCH}-linux.tar.gz"
	tar -xzf "${tmp}/wasi-sdk.tar.gz" -C "${tmp}"
	rm -rf "${WASI_SDK_ROOT}"
	mv "${tmp}/wasi-sdk-${WASI_SDK_FULL}-${WASI_ARCH}-linux" "${WASI_SDK_ROOT}"
	rm -rf "${tmp}"
fi

echo "tools ready"

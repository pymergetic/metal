#!/usr/bin/env bash
# End-to-end proof for runtime/process.h's spawn()/wait()/kill() — builds
# pm-linux-process-test (tests/process_test.c) in the normal (non-TSan)
# build dir and runs it against mods/t4_getpid.wasm + mods/t5_spin.wasm.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/build/linux/runtime"
JOBS="$(nproc 2>/dev/null || echo 4)"

"${ROOT}/scripts/build-mod.sh"

cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
cmake --build "${BUILD}" --target pm-linux-process-test -j"${JOBS}"

RUNTIME="${BUILD}/pm-linux-process-test"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}"' EXIT

mkdir -p "${VFS_ROOT}/mods"
cp "${ROOT}/build/mods/t2_env.wasm" \
	"${ROOT}/build/mods/t4_getpid.wasm" "${ROOT}/build/mods/t5_spin.wasm" \
	"${ROOT}/build/mods/t6_pipe_writer.wasm" "${ROOT}/build/mods/t7_pipe_reader.wasm" \
	"${ROOT}/build/mods/t10_socket_server.wasm" "${ROOT}/build/mods/t11_socket_client.wasm" \
	"${ROOT}/build/mods/t24_udp_server.wasm" "${ROOT}/build/mods/t25_udp_client.wasm" \
	"${ROOT}/build/mods/t26_ipv6_server.wasm" "${ROOT}/build/mods/t27_ipv6_client.wasm" \
	"${ROOT}/build/mods/t28_dns_lookup.wasm" \
	"${VFS_ROOT}/mods/"

# t5_spin.wasm never returns on its own — bounded here so a real kill()
# regression fails this script instead of hanging it forever. t7_pipe_reader
# blocking forever on a broken EOF (see process.c's own fd-close-on-exit
# contract) would show up the same way — one shared timeout covers both.
set +e
OUT="$(PM_METAL_TEST_VFS_ROOT="${VFS_ROOT}" timeout 15s "${RUNTIME}")"
RC=$?
set -e

echo "${OUT}"

if [ "${RC}" -eq 124 ]; then
	echo "FAIL: pm-linux-process-test timed out — kill() or the pipe pair's EOF did not work" >&2
	exit 1
fi
if [ "${RC}" -ne 0 ]; then
	echo "FAIL: pm-linux-process-test exited ${RC}" >&2
	exit 1
fi

echo "${OUT}" | grep -q "t7_pipe_reader: got: hello through the pipe" \
	|| { echo "FAIL: pipe pair did not deliver t6_pipe_writer's output to t7_pipe_reader" >&2; exit 1; }

# runtime/env.h's build_exported(): LOCAL_ONLY must never reach the child,
# EXPORTED_VAR must.
echo "${OUT}" | grep -q "t2_env: LOCAL_ONLY=(unset)" \
	|| { echo "FAIL: build_exported() leaked a non-exported var to the child" >&2; exit 1; }
echo "${OUT}" | grep -q "t2_env: EXPORTED_VAR=should-be-visible" \
	|| { echo "FAIL: build_exported() did not pass through an exported var" >&2; exit 1; }

# WASI preview1's own socket extension (docs/RUNTIME.md "Sockets") —
# TCP IPv4, UDP, TCP IPv6, and getaddrinfo("localhost"), gated on
# process_test.c's addr_pool / ns_lookup_pool.
echo "${OUT}" | grep -q "t10_socket_server: served \"hello socket\"" \
	|| { echo "FAIL: socket server did not receive the client's message" >&2; exit 1; }
echo "${OUT}" | grep -q "t11_socket_client: got: echo: hello socket" \
	|| { echo "FAIL: socket client did not get the server's reply" >&2; exit 1; }
echo "${OUT}" | grep -q "t24_udp_server: served \"hello udp\"" \
	|| { echo "FAIL: udp server did not receive the client's message" >&2; exit 1; }
echo "${OUT}" | grep -q "t25_udp_client: got: echo: hello udp" \
	|| { echo "FAIL: udp client did not get the server's reply" >&2; exit 1; }
echo "${OUT}" | grep -q "t26_ipv6_server: served \"hello ipv6\"" \
	|| { echo "FAIL: ipv6 server did not receive the client's message" >&2; exit 1; }
echo "${OUT}" | grep -q "t27_ipv6_client: got: echo: hello ipv6" \
	|| { echo "FAIL: ipv6 client did not get the server's reply" >&2; exit 1; }
echo "${OUT}" | grep -q "t28_dns_lookup: localhost -> 127.0.0.1" \
	|| { echo "FAIL: dns lookup did not resolve localhost to 127.0.0.1" >&2; exit 1; }

echo "verify-linux-process: OK"

#!/usr/bin/env bash
# Metal/WAMR smoke for /mods/apps/python.wasm — same guest paths as Zephyr/NuttX.
# Content from embedded packages (python-stdlib + mods-apps-python).
#   PYTHONHOME=/
#   /mods/apps/python.wasm --version
#   /mods/apps/python.wasm /mods/apps/pm-test.py
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/common.sh"
MODE="${1:?usage: $0 linux}"

case "${MODE}" in
linux)
	"${ROOT}/scripts/build.d/port/linux/default.sh"

	RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
	if [[ ! -x "${RUNTIME}" ]]; then
		echo "missing ${RUNTIME}" >&2
		exit 1
	fi

	VFS="$(mktemp -d)"
	trap 'rm -rf "${VFS}"' EXIT
	# Pre-create mount point for the WASI preopen workaround below; packages
	# extract into it at boot (pkg_apply_all).
	mkdir -p "${VFS}/lib/python3.14"

	run_py() {
		local label="$1"
		shift
		local out
		# Extra hostdir mount of /lib/python3.14 is a Linux WASI preopen
		# workaround: a lone rootfs preopen fails nested stdlib imports.
		# Guest layout matches Zephyr/NuttX (PYTHONHOME=/ only; no /wasi-lib).
		out="$("${RUNTIME}" \
			--rootfs="hostdir:${VFS}" \
			--mount="hostdir:${VFS}/lib/python3.14:/lib/python3.14" \
			--env=PYTHONHOME=/ \
			-- /mods/apps/python.wasm "$@")"
		echo "${out}"
		echo "${out}" | grep -qE "/mods/apps/python\.wasm: exit=0" \
			|| { echo "FAIL: python ${label} did not exit 0" >&2; exit 1; }
		if echo "${out}" | grep -qF "Could not find platform"; then
			echo "FAIL: python getpath still missing prefix (PYTHONHOME layout)" >&2
			exit 1
		fi
	}

	echo "=== python --version ==="
	OUT_VER="$(run_py version --version)"
	echo "${OUT_VER}"
	echo "${OUT_VER}" | grep -qE "Python 3\.14" \
		|| { echo "FAIL: missing Python 3.14 version string" >&2; exit 1; }

	echo "=== python /mods/apps/pm-test.py ==="
	OUT_PY="$(run_py file /mods/apps/pm-test.py)"
	echo "${OUT_PY}"
	echo "${OUT_PY}" | grep -qE '^1$' \
		|| { echo "FAIL: missing pm-test.py print(1) output" >&2; exit 1; }
	echo "${OUT_PY}" | grep -qF "pm-test: ok" \
		|| { echo "FAIL: missing pm-test: ok marker" >&2; exit 1; }

	echo "verify-python-guest: linux OK"
	;;

*)
	echo "usage: $0 linux" >&2
	exit 1
	;;
esac

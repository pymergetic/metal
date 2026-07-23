#!/usr/bin/env bash
# Host-side focused regressions (no QEMU). METAL defect backlog items.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="${ROOT}/build/host"
TLSF="${ROOT}/external/tlsf"
SRC="${ROOT}/tests/host"

mkdir -p "${OUT}"

run_one() {
	local name="$1"
	shift
	echo "verify-host: ${name}" >&2
	gcc -std=c11 -O1 -Wall -Wextra -Werror "$@" -o "${OUT}/${name}"
	"${OUT}/${name}"
	if gcc -fsanitize=address -fno-omit-frame-pointer -std=c11 -O1 -Wall -Wextra \
		"$@" -o "${OUT}/${name}_asan" 2>/dev/null; then
		echo "verify-host: ${name} under ASan" >&2
		"${OUT}/${name}_asan"
	fi
}

run_one metal001_realloc_move \
	-I"${TLSF}" \
	"${SRC}/metal001_realloc_move.c" \
	"${TLSF}/tlsf.c"

run_one metal002_task_lifetime \
	"${SRC}/metal002_task_lifetime.c"

run_one metal004_affinity \
	"${SRC}/metal004_affinity.c"

run_one metal005_trust_enforce \
	"${SRC}/metal005_trust_enforce.c"

run_one metal006_loaded_bind \
	"${SRC}/metal006_loaded_bind.c"

echo "verify-host: ok"

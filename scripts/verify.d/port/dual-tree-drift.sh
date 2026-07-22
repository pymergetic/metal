#!/usr/bin/env bash
# Fail if shared modules are re-duplicated under port trees, or if unexpected
# dual copies appear outside the port-local overlay allowlist.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIOS_METAL="${ROOT}/src/bios/pymergetic/metal"
EFI_METAL="${ROOT}/src/efi/pymergetic/metal"
SHARED_METAL="${ROOT}/src/pymergetic/metal"

# Intentional dual port overlays (platform-native implementations).
PORT_LOCAL=(
	# none — all former forks are shared + *_port
)

in_list() {
	local needle="$1"; shift
	local x
	for x in "$@"; do
		[[ "${x}" == "${needle}" ]] && return 0
	done
	return 1
}

fail=0

if [[ ! -d "${SHARED_METAL}" ]]; then
	echo "dual-tree-drift: missing shared host ${SHARED_METAL}" >&2
	exit 1
fi

while IFS= read -r -d '' sf; do
	rel="${sf#"${SHARED_METAL}/"}"
	if [[ -f "${BIOS_METAL}/${rel}" || -f "${EFI_METAL}/${rel}" ]]; then
		if in_list "${rel}" "${PORT_LOCAL[@]}"; then
			echo "dual-tree-drift: port-local also in shared: ${rel}" >&2
			fail=1
		else
			echo "dual-tree-drift: duplicated shared module under port tree: ${rel}" >&2
			fail=1
		fi
	fi
done < <(find "${SHARED_METAL}" -type f -print0 | sort -z)

if [[ -d "${BIOS_METAL}" && -d "${EFI_METAL}" ]]; then
	while IFS= read -r -d '' bf; do
		rel="${bf#"${BIOS_METAL}/"}"
		[[ -f "${EFI_METAL}/${rel}" ]] || continue
		# Port HW backends may share relative names (time_port, gfx_port, …).
		if [[ "${rel}" == *_port.c ]]; then
			continue
		fi
		if ! in_list "${rel}" "${PORT_LOCAL[@]}"; then
			echo "dual-tree-drift: unexpected dual port file: ${rel}" >&2
			fail=1
		fi
	done < <(find "${BIOS_METAL}" -type f -print0 | sort -z)
fi

# Product layers must not call bios/efi owned or touch ConIn directly.
# Allowed: boot stubs, input_port, BiosPkg/MetalPkg, shim.
LEAK_ROOTS=(
	"${SHARED_METAL}"
)
LEAK_ALLOW_GREP='(boot/bios/|boot/efi/|dev/input/input_port\.c|/BiosPkg/|/MetalPkg/|/shim/)'

leak=0
while IFS= read -r -d '' f; do
	rel="${f#"${ROOT}/"}"
	if echo "${rel}" | grep -Eq "${LEAK_ALLOW_GREP}"; then
		continue
	fi
	if grep -En 'pm_metal_bios_owned|pm_metal_efi_owned|->ConIn|gST->ConIn' "${f}" >/dev/null 2>&1; then
		echo "dual-tree-drift: product-layer leak in ${rel}:" >&2
		grep -En 'pm_metal_bios_owned|pm_metal_efi_owned|->ConIn|gST->ConIn' "${f}" >&2 || true
		leak=1
	fi
done < <(find "${SHARED_METAL}" -type f \( -name '*.c' -o -name '*.h' \) -print0)

# Also scan port overlays except allowed
for metal in "${BIOS_METAL}" "${EFI_METAL}"; do
	[[ -d "${metal}" ]] || continue
	while IFS= read -r -d '' f; do
		rel="${f#"${ROOT}/"}"
		if echo "${rel}" | grep -Eq "${LEAK_ALLOW_GREP}"; then
			continue
		fi
		if grep -En 'pm_metal_bios_owned|pm_metal_efi_owned|gST->ConIn' "${f}" >/dev/null 2>&1; then
			echo "dual-tree-drift: product-layer leak in ${rel}:" >&2
			grep -En 'pm_metal_bios_owned|pm_metal_efi_owned|gST->ConIn' "${f}" >&2 || true
			leak=1
		fi
	done < <(find "${metal}" -type f \( -name '*.c' -o -name '*.h' \) -print0)
done

if [[ "${leak}" -ne 0 ]]; then
	fail=1
fi

if [[ "${fail}" -ne 0 ]]; then
	echo "dual-tree-drift: FAIL" >&2
	exit 1
fi

echo "dual-tree-drift: ok (shared host + ${#PORT_LOCAL[@]} port-local overlays)"
exit 0

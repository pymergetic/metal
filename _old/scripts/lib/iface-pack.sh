# Shared lz4(ustar) packing for iface packs (embed-iface.sh + external apps).
# Requires ROOT = Metal checkout. Source from a bash script:
#   source "${ROOT}/scripts/lib/iface-pack.sh"
# shellcheck shell=bash

_PM_METAL_IFACE_LZ4PACK=""
_PM_METAL_IFACE_PACK_TMP=""

pm_metal_iface_pack_cleanup() {
	if [[ -n "${_PM_METAL_IFACE_PACK_TMP}" && -d "${_PM_METAL_IFACE_PACK_TMP}" ]]; then
		rm -rf "${_PM_METAL_IFACE_PACK_TMP}"
	fi
	_PM_METAL_IFACE_PACK_TMP=""
	_PM_METAL_IFACE_LZ4PACK=""
}

# Build the host lz4pack helper once (into a private temp dir).
pm_metal_iface_pack_init() {
	local cc_bin lz4pack_c

	if [[ -z "${ROOT:-}" || ! -d "${ROOT}/external/lz4/lib" ]]; then
		echo "iface-pack: ROOT must point at a Metal checkout (missing external/lz4/lib)" >&2
		return 1
	fi
	if [[ -n "${_PM_METAL_IFACE_LZ4PACK}" && -x "${_PM_METAL_IFACE_LZ4PACK}" ]]; then
		return 0
	fi

	_PM_METAL_IFACE_PACK_TMP="$(mktemp -d)"
	lz4pack_c="${_PM_METAL_IFACE_PACK_TMP}/lz4pack.c"
	_PM_METAL_IFACE_LZ4PACK="${_PM_METAL_IFACE_PACK_TMP}/lz4pack"
	cc_bin="${CC:-cc}"

	cat >"${lz4pack_c}" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "lz4.h"
int main(void) {
	long n; unsigned char *in; int bound, out_n; unsigned char *out;
	if (fseek(stdin, 0, SEEK_END) != 0) return 1;
	n = ftell(stdin); if (n < 0 || fseek(stdin, 0, SEEK_SET) != 0) return 1;
	in = malloc((size_t)n); if (!in || fread(in, 1, (size_t)n, stdin) != (size_t)n) return 1;
	bound = LZ4_compressBound((int)n); out = malloc((size_t)bound);
	if (!out) return 1;
	out_n = LZ4_compress_default((const char *)in, (char *)out, (int)n, bound);
	if (out_n <= 0) return 1;
	if (fwrite(out, 1, (size_t)out_n, stdout) != (size_t)out_n) return 1;
	return 0;
}
EOF
	"${cc_bin}" -O2 -I "${ROOT}/external/lz4/lib" -o "${_PM_METAL_IFACE_LZ4PACK}" \
		"${lz4pack_c}" "${ROOT}/external/lz4/lib/lz4.c"
}

# pm_metal_iface_pack_dir <stage_dir> <out_blob>
# Pack stage_dir as ustar (%P paths, no "./" prefix), lz4-compress to out_blob.
# Prints uncompressed ustar length (decimal) on stdout. Returns 0 on success.
pm_metal_iface_pack_dir() {
	local stage="$1" out_blob="$2"
	local tar_path uncompressed_len

	if [[ -z "${stage}" || -z "${out_blob}" || ! -d "${stage}" ]]; then
		echo "iface-pack: usage: pm_metal_iface_pack_dir <stage_dir> <out_blob>" >&2
		return 1
	fi
	pm_metal_iface_pack_init || return 1

	tar_path="${_PM_METAL_IFACE_PACK_TMP}/$(basename "${out_blob}").tar"
	# %P paths (no "./" prefix) — HTTP clients collapse "/./" in URLs.
	find "${stage}" -type f -printf '%P\n' | tar --format=ustar -cf "${tar_path}" -C "${stage}" -T -
	uncompressed_len="$(wc -c <"${tar_path}" | tr -d ' ')"
	"${_PM_METAL_IFACE_LZ4PACK}" <"${tar_path}" >"${out_blob}"
	rm -f "${tar_path}"
	printf '%s\n' "${uncompressed_len}"
}

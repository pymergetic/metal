# Metal PKI helpers — keys live outside the source tree.
# Multi-realm / multi-root ready: bake collects all Mods/Kernel/Root pubs.
# shellcheck shell=bash

pm_metal_pki_dir() {
	if [[ -n "${METAL_PKI_DIR:-}" ]]; then
		printf '%s\n' "${METAL_PKI_DIR}"
		return 0
	fi
	printf '%s\n' "${HOME}/.local/share/metal/pki"
}

pm_metal_pki_bake_dir() {
	printf '%s\n' "${ROOT}/build/trust"
}

pm_metal_pki_inc() {
	# Distinct name so trust.c can __has_include the bake without the
	# co-located soft stub (metal_trust.inc.c) shadowing it.
	printf '%s\n' "$(pm_metal_pki_bake_dir)/metal_trust_bake.inc.c"
}

pm_metal_pki_mode_file() {
	printf '%s\n' "$(pm_metal_pki_bake_dir)/mode"
}

# Normalize METAL_TRUST_MODE → off|soft|enforce. Empty = caller decides default.
pm_metal_pki_normalize_mode() {
	local raw="${1:-}"
	case "$(printf '%s' "${raw}" | tr '[:upper:]' '[:lower:]')" in
	"" ) printf '\n' ;;
	off|0|none|disabled ) printf 'off\n' ;;
	soft|warn|optional ) printf 'soft\n' ;;
	enforce|2|on|required|prod ) printf 'enforce\n' ;;
	*)
		echo "pki: unknown METAL_TRUST_MODE='${raw}' (use off|soft|enforce)" >&2
		return 1
		;;
	esac
}

# Effective mode for this bake/build (writes nothing). Prefer METAL_TRUST_MODE;
# METAL_TRUST=1 ⇒ enforce; else soft if default realm present else off.
pm_metal_pki_trust_mode() {
	local pki mode def_root def_kern def_mods
	mode="$(pm_metal_pki_normalize_mode "${METAL_TRUST_MODE:-}")" || return 1
	if [[ -n "${mode}" ]]; then
		printf '%s\n' "${mode}"
		return 0
	fi
	if [[ "${METAL_TRUST:-0}" == "1" ]]; then
		printf 'enforce\n'
		return 0
	fi
	pki="$(pm_metal_pki_dir)"
	def_root="${pki}/root/ca.crt"
	def_kern="${pki}/kernel/ca.crt"
	def_mods="${pki}/mods/ca.crt"
	if [[ -f "${pki}/realms/pymergetic/mods/ca.crt" ]]; then
		def_root="${pki}/realms/pymergetic/root/ca.crt"
		def_kern="${pki}/realms/pymergetic/kernel/ca.crt"
		def_mods="${pki}/realms/pymergetic/mods/ca.crt"
	fi
	if [[ -f "${def_root}" && -f "${def_kern}" && -f "${def_mods}" ]]; then
		printf 'soft\n'
	else
		printf 'off\n'
	fi
}

# 1 if build should attempt to detach-sign artifacts.
pm_metal_pki_want_sign() {
	local mode
	mode="$(pm_metal_pki_trust_mode)" || return 1
	[[ "${mode}" != "off" ]]
}

# Default realm (pymergetic) — flat layout or realms/pymergetic/
pm_metal_pki_realm_dir() {
	local pki id
	pki="$(pm_metal_pki_dir)"
	id="${1:-pymergetic}"
	if [[ -d "${pki}/realms/${id}" ]]; then
		printf '%s\n' "${pki}/realms/${id}"
	elif [[ "${id}" == "pymergetic" && -d "${pki}/mods" ]]; then
		printf '%s\n' "${pki}"
	else
		printf '%s\n' "${pki}/realms/${id}"
	fi
}

# Convert PEM cert → C byte array (prints to stdout)
_pm_metal_pki_pem_to_c_array() {
	local pem="$1"
	local arr="$2"
	local der
	der="$(mktemp)"
	openssl x509 -in "${pem}" -outform DER -out "${der}"
	python3 - "$der" "$arr" <<'PY'
import sys
path, name = sys.argv[1], sys.argv[2]
data = open(path, "rb").read()
print(f"STATIC CONST UINT8 {name}[] = {{")
for i in range(0, len(data), 12):
    chunk = data[i : i + 12]
    print("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
print("};")
print(f"STATIC CONST UINT32 {name}_len = {len(data)};")
PY
	rm -f "${der}"
}

# Sanitize realm/id → C identifier fragment
_pm_metal_pki_cid() {
	printf '%s' "$1" | tr -c 'A-Za-z0-9' '_'
}

# Generate build/trust/metal_trust_bake.inc.c — multi root / multi mods CA.
# Mode: METAL_TRUST_MODE=off|soft|enforce (see pm_metal_pki_trust_mode).
# METAL_TRUST=1 → enforce + fail if default realm incomplete.
pm_metal_pki_bake() {
	local pki bake inc mode mode_num
	local -a root_pems=() root_ids=()
	local -a kern_pems=() kern_ids=()
	local -a mods_pems=() mods_ids=()
	local def_root def_app def_kern def_mods
	local rdir id pem cid i

	pki="$(pm_metal_pki_dir)"
	bake="$(pm_metal_pki_bake_dir)"
	inc="$(pm_metal_pki_inc)"
	mkdir -p "${bake}"

	mode="$(pm_metal_pki_trust_mode)" || return 1
	case "${mode}" in
	off) mode_num=0 ;;
	soft) mode_num=1 ;;
	enforce) mode_num=2 ;;
	*) return 1 ;;
	esac
	printf '%s\n' "${mode}" >"$(pm_metal_pki_mode_file)"

	def_root="${pki}/root/ca.crt"
	def_app="${pki}/app/ca.crt"
	def_kern="${pki}/kernel/ca.crt"
	def_mods="${pki}/mods/ca.crt"
	if [[ -f "${pki}/realms/pymergetic/mods/ca.crt" ]]; then
		def_root="${pki}/realms/pymergetic/root/ca.crt"
		def_app="${pki}/realms/pymergetic/app/ca.crt"
		def_kern="${pki}/realms/pymergetic/kernel/ca.crt"
		def_mods="${pki}/realms/pymergetic/mods/ca.crt"
	fi

	# off → stub (no pubs). enforce without PKI → fail. soft without PKI → stub.
	if [[ "${mode}" == "off" ]] \
		|| [[ ! -f "${def_root}" || ! -f "${def_kern}" || ! -f "${def_mods}" ]]
	then
		if [[ "${mode}" == "enforce" ]] || [[ "${METAL_TRUST:-0}" == "1" ]]; then
			echo "pki-bake: mode=${mode} but missing default realm under ${pki}" >&2
			echo "pki-bake: run: METAL_PKI_DIR=${pki} ./scripts/pki init" >&2
			return 1
		fi
		cat >"${inc}" <<EOF
/* AUTO-GENERATED — trust mode=${mode}; verify disabled / no CA pubs. */
#ifndef PM_METAL_TRUST_BAKED
#define PM_METAL_TRUST_BAKED 0
#endif
#ifndef PM_METAL_TRUST_MODE
#define PM_METAL_TRUST_MODE ${mode_num}
#endif
EOF
		echo "pki-bake: stub mode=${mode} -> ${inc}" >&2
		return 0
	fi

	# Default pymergetic realm
	root_pems+=("${def_root}")
	root_ids+=("pymergetic")
	kern_pems+=("${def_kern}")
	kern_ids+=("pymergetic")
	mods_pems+=("${def_mods}")
	mods_ids+=("pymergetic")
	# app CA (${def_app}) optional for pin-trust bake (signer pubs suffice)

	# realms/<id>/ — additional teams (root + mods; kernel optional)
	if [[ -d "${pki}/realms" ]]; then
		for rdir in "${pki}/realms"/*; do
			[[ -d "${rdir}" ]] || continue
			id="$(basename "${rdir}")"
			[[ "${id}" == "pymergetic" ]] && continue
			if [[ -f "${rdir}/root/ca.crt" ]]; then
				root_pems+=("${rdir}/root/ca.crt")
				root_ids+=("${id}")
			fi
			if [[ -f "${rdir}/kernel/ca.crt" ]]; then
				kern_pems+=("${rdir}/kernel/ca.crt")
				kern_ids+=("${id}")
			fi
			if [[ -f "${rdir}/mods/ca.crt" ]]; then
				mods_pems+=("${rdir}/mods/ca.crt")
				mods_ids+=("${id}")
			fi
		done
	fi

	# extra/mods/<id>.crt — Mods CA pubs only (team drop-in; skip if id already baked)
	if [[ -d "${pki}/extra/mods" ]]; then
		for pem in "${pki}/extra/mods"/*.crt; do
			[[ -f "${pem}" ]] || continue
			id="$(basename "${pem}" .crt)"
			local already=0
			for x in "${mods_ids[@]+"${mods_ids[@]}"}"; do
				[[ "${x}" == "${id}" ]] && already=1 && break
			done
			[[ "${already}" == "1" ]] && continue
			mods_pems+=("${pem}")
			mods_ids+=("${id}")
		done
	fi

	# extra/roots/<id>.crt — additional trust anchors
	if [[ -d "${pki}/extra/roots" ]]; then
		for pem in "${pki}/extra/roots"/*.crt; do
			[[ -f "${pem}" ]] || continue
			id="$(basename "${pem}" .crt)"
			local already=0
			for x in "${root_ids[@]+"${root_ids[@]}"}"; do
				[[ "${x}" == "${id}" ]] && already=1 && break
			done
			[[ "${already}" == "1" ]] && continue
			root_pems+=("${pem}")
			root_ids+=("${id}")
		done
	fi

	{
		echo "/* AUTO-GENERATED by scripts/pki bake from ${pki} — do not edit */"
		echo "/* trust mode=${mode} (0=off 1=soft 2=enforce) */"
		echo "#ifndef PM_METAL_TRUST_BAKED"
		echo "#define PM_METAL_TRUST_BAKED 1"
		echo "#endif"
		echo "#ifndef PM_METAL_TRUST_MODE"
		echo "#define PM_METAL_TRUST_MODE ${mode_num}"
		echo "#endif"
		echo "#ifndef PM_METAL_TRUST_DER_T_DEFINED"
		echo "#define PM_METAL_TRUST_DER_T_DEFINED"
		echo "typedef struct {"
		echo "  const UINT8 *der;"
		echo "  UINT32       len;"
		echo "  const CHAR8 *id;"
		echo "} pm_metal_trust_der_t;"
		echo "#endif"

		# Index-prefix symbols so duplicate realm ids / extra drops cannot collide.
		for i in "${!root_pems[@]}"; do
			cid="$(_pm_metal_pki_cid "${root_ids[$i]}")"
			_pm_metal_pki_pem_to_c_array "${root_pems[$i]}" "g_pm_metal_trust_root_${i}_${cid}_der"
		done
		for i in "${!kern_pems[@]}"; do
			cid="$(_pm_metal_pki_cid "${kern_ids[$i]}")"
			_pm_metal_pki_pem_to_c_array "${kern_pems[$i]}" "g_pm_metal_trust_kernel_${i}_${cid}_der"
		done
		for i in "${!mods_pems[@]}"; do
			cid="$(_pm_metal_pki_cid "${mods_ids[$i]}")"
			_pm_metal_pki_pem_to_c_array "${mods_pems[$i]}" "g_pm_metal_trust_mods_${i}_${cid}_der"
		done

		echo "STATIC CONST pm_metal_trust_der_t g_pm_metal_trust_roots[] = {"
		for i in "${!root_pems[@]}"; do
			cid="$(_pm_metal_pki_cid "${root_ids[$i]}")"
			echo "  { g_pm_metal_trust_root_${i}_${cid}_der, g_pm_metal_trust_root_${i}_${cid}_der_len, \"${root_ids[$i]}\" },"
		done
		echo "};"
		echo "STATIC CONST UINT32 g_pm_metal_trust_root_count ="
		echo "  (UINT32)(sizeof (g_pm_metal_trust_roots) / sizeof (g_pm_metal_trust_roots[0]));"

		echo "STATIC CONST pm_metal_trust_der_t g_pm_metal_trust_kernel_cas[] = {"
		for i in "${!kern_pems[@]}"; do
			cid="$(_pm_metal_pki_cid "${kern_ids[$i]}")"
			echo "  { g_pm_metal_trust_kernel_${i}_${cid}_der, g_pm_metal_trust_kernel_${i}_${cid}_der_len, \"${kern_ids[$i]}\" },"
		done
		echo "};"
		echo "STATIC CONST UINT32 g_pm_metal_trust_kernel_ca_count ="
		echo "  (UINT32)(sizeof (g_pm_metal_trust_kernel_cas) / sizeof (g_pm_metal_trust_kernel_cas[0]));"

		echo "STATIC CONST pm_metal_trust_der_t g_pm_metal_trust_mods_cas[] = {"
		for i in "${!mods_pems[@]}"; do
			cid="$(_pm_metal_pki_cid "${mods_ids[$i]}")"
			echo "  { g_pm_metal_trust_mods_${i}_${cid}_der, g_pm_metal_trust_mods_${i}_${cid}_der_len, \"${mods_ids[$i]}\" },"
		done
		echo "};"
		echo "STATIC CONST UINT32 g_pm_metal_trust_mods_ca_count ="
		echo "  (UINT32)(sizeof (g_pm_metal_trust_mods_cas) / sizeof (g_pm_metal_trust_mods_cas[0]));"
	} >"${inc}"

	echo "pki-bake: ok mode=${mode} -> ${inc}" >&2
	echo "pki-bake: roots=${#root_pems[@]} kernel_cas=${#kern_pems[@]} mods_cas=${#mods_pems[@]}" >&2
}

pm_metal_pki_sign_file() {
	local key="$1"
	local infile="$2"
	local sigout="$3"

	if [[ ! -f "${key}" ]]; then
		echo "pki-sign: missing key ${key}" >&2
		return 1
	fi
	if [[ ! -f "${infile}" ]]; then
		echo "pki-sign: missing input ${infile}" >&2
		return 1
	fi
	openssl dgst -sha256 -sign "${key}" -out "${sigout}" "${infile}"
	echo "pki-sign: ${infile} -> ${sigout} (key=${key})" >&2
}

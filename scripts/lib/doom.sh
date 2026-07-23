# Shared doom wasm/aot package helpers (EFI ESP + BIOS/PXE).
# shellcheck shell=bash

pm_metal_doom_out_dir() {
	printf '%s\n' "${METAL_DOOM_OUT_DIR:-${ROOT}/build/doom}"
}

pm_metal_doom_dir() {
	# Prefer explicit stage dir; else build output.
	if [[ -n "${METAL_DOOM_DIR:-}" ]]; then
		printf '%s\n' "${METAL_DOOM_DIR}"
	else
		pm_metal_doom_out_dir
	fi
}

# After doom.wasm exists: resign, or delete stale .sig (soft trust fails on bad sig).
pm_metal_doom_sign_or_clear() {
	local out="${1:-}"
	local wasm sig key

	# shellcheck disable=SC1091
	source "${ROOT}/scripts/lib/pki.sh"

	if [[ -z "${out}" ]]; then
		out="$(pm_metal_doom_out_dir)"
	fi
	wasm="${out}/doom.wasm"
	sig="${out}/doom.wasm.sig"
	if [[ ! -f "${wasm}" ]]; then
		return 1
	fi

	key="$(pm_metal_pki_dir)/mods/ca.key"
	if pm_metal_pki_want_sign && [[ -f "${key}" ]]; then
		if "${ROOT}/scripts/pki" sign-wasm "${wasm}"; then
			echo "doom: signed ${sig}" >&2
			return 0
		fi
		echo "doom: sign-wasm failed — removing stale ${sig}" >&2
		rm -f "${sig}"
		return 1
	fi

	if [[ -f "${sig}" ]]; then
		echo "doom: clear .sig (trust mode=$(pm_metal_pki_trust_mode); no mods CA)" >&2
		rm -f "${sig}"
	fi
	return 0
}

# Build doom.wasm (+ doom.{x86_64,i386}.aot when wamrc present). Signing inside doom.sh.
pm_metal_doom_build() {
	METAL_DOOM_BUILD=1 \
		METAL_DOOM_OUT_DIR="$(pm_metal_doom_out_dir)" \
		"${ROOT}/scripts/build.d/port/efi/doom.sh"
}

# Stage one AOT (+ optional .sig) into dest; resign if stale.
pm_metal_doom_stage_aot() {
	local src_aot="$1"
	local dest_aot="$2"
	local asig

	# shellcheck disable=SC1091
	source "${ROOT}/scripts/lib/aot.sh"
	if [[ ! -f "${src_aot}" ]]; then
		rm -f "${dest_aot}" "${dest_aot}.sig"
		return 1
	fi
	asig="${src_aot}.sig"
	if [[ -f "${asig}" && "${asig}" -ot "${src_aot}" ]]; then
		pm_metal_aot_sign_or_clear "${src_aot}" || true
	elif [[ ! -f "${asig}" ]]; then
		pm_metal_aot_sign_or_clear "${src_aot}" || true
	fi
	cp -f "${src_aot}" "${dest_aot}"
	rm -f "${dest_aot}.sig"
	if [[ -f "${asig}" && ! "${asig}" -ot "${src_aot}" ]]; then
		cp -f "${asig}" "${dest_aot}.sig"
	fi
	return 0
}

# Stage doom package into <dest_root>/mods/apps/doom/ if built.
# Stages wasm + wad, plus doom.{x86_64,i386}.aot when present.
# Returns 0 if staged (or already present), 1 if package missing.
pm_metal_doom_stage_into() {
	local dest_root="$1"
	local src
	local dest
	local wasm sig
	local staged=()

	src="$(pm_metal_doom_dir)"
	wasm="${src}/doom.wasm"
	sig="${src}/doom.wasm.sig"
	if [[ ! -f "${wasm}" || ! -f "${src}/doom1.wad" ]]; then
		echo "doom: not staged (missing ${src}/doom.wasm or doom1.wad)" >&2
		echo "doom: build with: METAL_DOOM_BUILD=1 ./scripts/build efi" >&2
		return 1
	fi

	# Never ship a sig older than the artifact (stale → soft-trust fail).
	if [[ -f "${sig}" && "${sig}" -ot "${wasm}" ]]; then
		echo "doom: stale wasm .sig — resigning before stage" >&2
		pm_metal_doom_sign_or_clear "${src}" || true
	elif [[ ! -f "${sig}" ]]; then
		pm_metal_doom_sign_or_clear "${src}" || true
	fi

	dest="${dest_root}/mods/apps/doom"
	mkdir -p "${dest}"
	cp -f "${wasm}" "${dest}/doom.wasm"
	cp -f "${src}/doom1.wad" "${dest}/doom1.wad"
	rm -f "${dest}/doom.wasm.sig" \
		"${dest}/doom.aot" "${dest}/doom.aot.sig" \
		"${dest}/doom.x86_64.aot" "${dest}/doom.x86_64.aot.sig" \
		"${dest}/doom.i386.aot" "${dest}/doom.i386.aot.sig"
	if [[ -f "${sig}" && ! "${sig}" -ot "${wasm}" ]]; then
		cp -f "${sig}" "${dest}/doom.wasm.sig"
	fi
	# One-shot migrate pre-infix builds, then kill bare names everywhere.
	if [[ ! -f "${src}/doom.x86_64.aot" && -f "${src}/doom.aot" ]]; then
		echo "doom: migrating ${src}/doom.aot → doom.x86_64.aot" >&2
		mv -f "${src}/doom.aot" "${src}/doom.x86_64.aot"
		if [[ -f "${src}/doom.aot.sig" ]]; then
			mv -f "${src}/doom.aot.sig" "${src}/doom.x86_64.aot.sig"
		fi
	fi
	rm -f "${src}/doom.aot" "${src}/doom.aot.sig"
	if pm_metal_doom_stage_aot "${src}/doom.x86_64.aot" "${dest}/doom.x86_64.aot"; then
		staged+=("doom.x86_64.aot")
	fi
	if pm_metal_doom_stage_aot "${src}/doom.i386.aot" "${dest}/doom.i386.aot"; then
		staged+=("doom.i386.aot")
	fi
	# Never leave un-infixed AOT on the stage (wrong-arch hosts must not see it).
	rm -f "${dest}/doom.aot" "${dest}/doom.aot.sig"
	if [[ -f "${src}/autostart" ]]; then
		cp -f "${src}/autostart" "${dest}/autostart"
	fi
	if [[ "${#staged[@]}" -gt 0 ]]; then
		echo "doom: staged ${dest}/{${staged[*]},doom.wasm,doom1.wad}" >&2
	elif [[ -f "${dest}/doom.wasm.sig" ]]; then
		echo "doom: staged ${dest}/{doom.wasm,doom.wasm.sig,doom1.wad}" >&2
	else
		echo "doom: staged ${dest}/{doom.wasm,doom1.wad}" >&2
	fi
	return 0
}

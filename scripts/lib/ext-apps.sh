# Generic external-app staging (METAL_EXT_APPS="name=dir[:name2=dir2...]").
#
# Metal never builds or signs external apps itself — an external app lives
# in its own sibling repo (e.g. ../metal-doom), builds + signs its own
# guest binary there, and this just copies the already-built output dir
# verbatim into <dest_root>/mods/apps/<name>/ (EFI ESP or BIOS/PXE root).
# shellcheck shell=bash

# Stage every "name=dir" pair from METAL_EXT_APPS into <dest_root>/mods/apps/<name>/.
# Pure copy (no build/sign): whatever files sit in <dir> land as-is
# (wasm/aot[.sig]/assets.list/extra assets/...). Missing/malformed entries
# are skipped with a warning, never fatal — an external app is optional.
pm_metal_ext_apps_stage_into() {
	local dest_root="$1"
	local entries=()
	local entry name src dest

	if [[ -z "${METAL_EXT_APPS:-}" ]]; then
		return 0
	fi

	IFS=':' read -ra entries <<<"${METAL_EXT_APPS}"
	for entry in "${entries[@]}"; do
		[[ -z "${entry}" ]] && continue
		name="${entry%%=*}"
		src="${entry#*=}"
		if [[ -z "${name}" || -z "${src}" || "${name}" == "${entry}" ]]; then
			echo "ext-apps: skip malformed METAL_EXT_APPS entry '${entry}' (want name=dir)" >&2
			continue
		fi
		if [[ ! -d "${src}" ]]; then
			echo "ext-apps: skip ${name} -- missing dir ${src}" >&2
			continue
		fi

		dest="${dest_root}/mods/apps/${name}"
		rm -rf "${dest}"
		mkdir -p "${dest}"
		cp -a "${src}/." "${dest}/"
		if [[ "${PM_METAL_STAGE_QUIET:-}" != "1" ]]; then
			echo "ext-apps: staged ${name} from ${src} -> ${dest}" >&2
		fi
	done
}

# Generic external-app staging (METAL_EXT_APPS="name=dir[:name2=dir2...]").
#
# Metal never builds external apps — copy already-built output into
# <dest_root>/mods/apps/<name>/.
# shellcheck shell=bash

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

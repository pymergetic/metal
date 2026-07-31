# Shared Kconfig helpers — source after ROOT is set.
# shellcheck shell=bash

pm_metal_kconfig_root() {
	printf '%s\n' "${ROOT}/config"
}

pm_metal_kconfig_dotconfig() {
	printf '%s\n' "${ROOT}/config/.config"
}

pm_metal_kconfig_ensure() {
	local confgen="${ROOT}/scripts/confgen"
	local dotcfg
	dotcfg="$(pm_metal_kconfig_dotconfig)"
	if [[ ! -f "${dotcfg}" ]]; then
		cp -f "${ROOT}/config/defconfig" "${dotcfg}"
	fi
	if [[ ! -x "${confgen}" ]]; then
		echo "kconfig: missing ${confgen}" >&2
		return 1
	fi
	"${confgen}"
}

# Source generated build/config.sh (CONFIG_* exports). No-op if missing after ensure fails.
pm_metal_kconfig_load() {
	pm_metal_kconfig_ensure || return 1
	# shellcheck disable=SC1091
	source "${ROOT}/build/config.sh"
}

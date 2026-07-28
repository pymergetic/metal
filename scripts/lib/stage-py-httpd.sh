#!/usr/bin/env bash
# Stage mods/py + product Python (import layout) into an ESP/PXE root.
# shellcheck shell=bash
# Requires ROOT.

pm_metal_stage_py_httpd_into() {
	local dest="$1"

	if [[ ! -d "${ROOT}/mods/py" ]]; then
		return 0
	fi

	# shellcheck disable=SC1091
	source "${ROOT}/scripts/lib/stage-py-trees.sh"

	mkdir -p "${dest}/mods/py"

	# Loose Easy stdlib -> /mods/py/stdlib (sys.path); mandatory for µPy.
	if [[ ! -d "${ROOT}/mods/py/stdlib" ]]; then
		echo "stage-py-httpd: missing mandatory ${ROOT}/mods/py/stdlib" >&2
		return 1
	fi
	pm_metal_stage_py_copy_py_tree "${ROOT}/mods/py/stdlib" "${dest}/mods/py/stdlib"
	if [[ -z "$(find "${dest}/mods/py/stdlib" -type f -name '*.py' 2>/dev/null | head -n 1)" ]]; then
		echo "stage-py-httpd: mandatory stdlib stage empty" >&2
		return 1
	fi

	if [[ -d "${ROOT}/mods/py/tests" ]]; then
		mkdir -p "${dest}/mods/py/tests"
		cp -a "${ROOT}/mods/py/tests/." "${dest}/mods/py/tests/"
	fi

	# Import layout under /mods (httpd/, api/, …).
	pm_metal_stage_py_import_tree_into "${dest}/mods"
}

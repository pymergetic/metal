#!/usr/bin/env bash
# Stage product Python into import layout (httpd/, api/, templates/, …).
# Shared by ESP/PXE staging and embed-iface py@metal.guest pack — one layout.
# shellcheck shell=bash
# Requires ROOT.

# Copy tree of *.py (and optional extra globs) from src -> dest, preserving
# relative paths under src. Skips __pycache__.
pm_metal_stage_py_copy_py_tree() {
	local src="$1"
	local dest="$2"
	local rel f

	if [[ ! -d "${src}" ]]; then
		return 0
	fi
	mkdir -p "${dest}"
	while IFS= read -r -d '' f; do
		rel="${f#"${src}"/}"
		mkdir -p "$(dirname "${dest}/${rel}")"
		cp -a "${f}" "${dest}/${rel}"
	done < <(find "${src}" -type f -name '*.py' ! -path '*/__pycache__/*' -print0)
}

# Also copy *.html next to compiled templates (source companions).
pm_metal_stage_py_copy_templates() {
	local src="$1"
	local dest="$2"
	local rel f

	if [[ ! -d "${src}" ]]; then
		return 0
	fi
	mkdir -p "${dest}"
	while IFS= read -r -d '' f; do
		rel="${f#"${src}"/}"
		mkdir -p "$(dirname "${dest}/${rel}")"
		cp -a "${f}" "${dest}/${rel}"
	done < <(find "${src}" -type f \( -name '*.py' -o -name '*.html' \) ! -path '*/__pycache__/*' -print0)
}

# Populate ${dest}/ with import roots: httpd api templates microdot utemplate.
# Compiles templates when pack/compile scripts exist.
pm_metal_stage_py_import_tree_into() {
	local dest="$1"
	local microdot_src utem_src

	mkdir -p "${dest}"

	if [[ -x "${ROOT}/mods/api/compile_templates.py" ]] || [[ -f "${ROOT}/mods/api/compile_templates.py" ]]; then
		python3 "${ROOT}/mods/api/compile_templates.py" || true
	fi

	# httpd package (loose .py only — no zips).
	mkdir -p "${dest}/httpd"
	for f in __init__.py autoload.py util.py; do
		if [[ -f "${ROOT}/mods/httpd/${f}" ]]; then
			cp -a "${ROOT}/mods/httpd/${f}" "${dest}/httpd/"
		fi
	done
	if [[ -d "${ROOT}/mods/httpd/highlight" ]]; then
		pm_metal_stage_py_copy_py_tree "${ROOT}/mods/httpd/highlight" "${dest}/httpd/highlight"
	fi

	pm_metal_stage_py_copy_py_tree "${ROOT}/mods/api/api" "${dest}/api"
	pm_metal_stage_py_copy_templates "${ROOT}/mods/api/templates" "${dest}/templates"

	microdot_src=""
	if [[ -d "${ROOT}/external/microdot/src/microdot" ]]; then
		microdot_src="${ROOT}/external/microdot/src/microdot"
	elif [[ -d "${ROOT}/external/microdot/microdot" ]]; then
		microdot_src="${ROOT}/external/microdot/microdot"
	fi
	if [[ -n "${microdot_src}" ]]; then
		pm_metal_stage_py_copy_py_tree "${microdot_src}" "${dest}/microdot"
	fi

	utem_src="${ROOT}/external/utemplate"
	if [[ -d "${utem_src}" ]]; then
		pm_metal_stage_py_copy_py_tree "${utem_src}" "${dest}/utemplate"
	fi
}

# typings/ -> dest (strip typings/ prefix). For pyi@metal.guest pack.
pm_metal_stage_py_typings_into() {
	local dest="$1"
	local typings="${ROOT}/typings"
	local f rel

	if [[ ! -d "${typings}" ]]; then
		return 0
	fi
	mkdir -p "${dest}"
	while IFS= read -r -d '' f; do
		rel="${f#"${typings}"/}"
		mkdir -p "$(dirname "${dest}/${rel}")"
		cp -a "${f}" "${dest}/${rel}"
	done < <(find "${typings}" -type f -name '*.pyi' -print0)
}

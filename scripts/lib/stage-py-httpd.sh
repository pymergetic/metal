#!/usr/bin/env bash
# Stage mods/py + mods/httpd + mods/api guest artifacts into an ESP/PXE root.
# shellcheck shell=bash
# Requires ROOT. Packs/signs zips from source every call.

pm_metal_stage_py_httpd_into() {
	local dest="$1"

	if [[ ! -d "${ROOT}/mods/py" ]]; then
		return 0
	fi

	if [[ -x "${ROOT}/mods/httpd/build_microdot_zip.sh" ]]; then
		"${ROOT}/mods/httpd/build_microdot_zip.sh"
	fi
	if [[ -x "${ROOT}/mods/httpd/pack_zips.sh" ]]; then
		"${ROOT}/mods/httpd/pack_zips.sh"
	fi
	if [[ -x "${ROOT}/mods/py/build_stdlib_zip.sh" ]]; then
		# stdlib.zip also embedded at firmware build; refresh for ESP copy.
		"${ROOT}/mods/py/build_stdlib_zip.sh" || true
	fi

	mkdir -p "${dest}/mods/py" "${dest}/mods/httpd" "${dest}/mods/api"

	for f in stdlib.zip stdlib.zip.sig; do
		if [[ -f "${ROOT}/mods/py/${f}" ]]; then
			cp -a "${ROOT}/mods/py/${f}" "${dest}/mods/py/"
		fi
	done
	if [[ -d "${ROOT}/mods/py/tests" ]]; then
		mkdir -p "${dest}/mods/py/tests"
		cp -a "${ROOT}/mods/py/tests/." "${dest}/mods/py/tests/"
	fi

	for f in autoload.py httpd.py util.py highlight.py microdot.zip microdot.zip.sig utemplate.zip; do
		if [[ -f "${ROOT}/mods/httpd/${f}" ]]; then
			cp -a "${ROOT}/mods/httpd/${f}" "${dest}/mods/httpd/"
		fi
	done

	for f in api.zip templates.zip; do
		if [[ -f "${ROOT}/mods/api/${f}" ]]; then
			cp -a "${ROOT}/mods/api/${f}" "${dest}/mods/api/"
		fi
	done
}

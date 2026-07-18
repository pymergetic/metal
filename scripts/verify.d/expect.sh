# Platform-agnostic verify helpers — source after ROOT is set (via lib/root.sh).
# shellcheck shell=bash

pm_expect() {
	# pm_expect <file|-> <fixed-string> <fail-msg>
	local src="$1" needle="$2" msg="$3"
	if [[ "${src}" == "-" ]]; then
		echo "${OUT}" | grep -qF -- "${needle}" \
			|| { echo "FAIL: ${msg}" >&2; exit 1; }
	else
		grep -qF -- "${needle}" "${src}" \
			|| { echo "FAIL: ${msg}" >&2; exit 1; }
	fi
}

pm_expect_re() {
	# pm_expect_re <file|-> <ere> <fail-msg>
	local src="$1" re="$2" msg="$3"
	if [[ "${src}" == "-" ]]; then
		echo "${OUT}" | grep -qE -- "${re}" \
			|| { echo "FAIL: ${msg}" >&2; exit 1; }
	else
		grep -qE -- "${re}" "${src}" \
			|| { echo "FAIL: ${msg}" >&2; exit 1; }
	fi
}

pm_expect_absent() {
	# pm_expect_absent <file|-> <fixed-string> <fail-msg>
	# Must not leave a failing grep as the last status (set -e callers).
	local src="$1" needle="$2" msg="$3"
	if [[ "${src}" == "-" ]]; then
		if echo "${OUT}" | grep -qF -- "${needle}"; then
			echo "FAIL: ${msg}" >&2
			exit 1
		fi
	else
		if grep -qF -- "${needle}" "${src}"; then
			echo "FAIL: ${msg}" >&2
			exit 1
		fi
	fi
}

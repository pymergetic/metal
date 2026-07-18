# Run a list of dispatcher steps; collect failures for AI-friendly full runs.
# Usage:
#   pm_run_steps <dispatcher> <step> [<step>...]
# Each <step> is one shell-word string of args after the dispatcher, e.g.
#   "linux"  or  "zephyr qemu"
# shellcheck shell=bash

pm_run_steps() {
	local dispatcher="$1"
	shift
	local step n="$#"
	local -a failed=()

	if [[ ! -x "${dispatcher}" ]]; then
		echo "pm_run_steps: missing dispatcher ${dispatcher}" >&2
		return 1
	fi

	for step in "$@"; do
		echo
		echo "========== ${dispatcher##*/} ${step} =========="
		# Intentional word-split: step is an args string for the dispatcher.
		# shellcheck disable=SC2086
		if ! "${dispatcher}" ${step}; then
			failed+=("${step}")
			echo "FAILED: ${dispatcher##*/} ${step}" >&2
		else
			echo "OK: ${dispatcher##*/} ${step}"
		fi
	done

	echo
	if [[ ${#failed[@]} -gt 0 ]]; then
		echo "pm_run_steps: ${#failed[@]}/${n} failed:" >&2
		local f
		for f in "${failed[@]}"; do
			echo "  - ${dispatcher##*/} ${f}" >&2
		done
		return 1
	fi
	echo "pm_run_steps: all ${n} steps OK"
	return 0
}

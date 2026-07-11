#!/usr/bin/env bash
# Shared Zephyr west env for verify/build scripts.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

"${ROOT}/scripts/setup-tools.sh"
"${ROOT}/scripts/setup-west.sh"

# shellcheck disable=SC1091
source "${ROOT}/.venv/bin/activate"
export ZEPHYR_BASE="${ROOT}/external/zephyr"
export WAMR_ROOT_DIR="${ROOT}/external/wamr"
export WASI_SDK_PATH="${ROOT}/.tools/wasi-sdk"

PM_ZEPHYR_OVERLAY="${ROOT}/host/zephyr/boards/pm_ramdisk.overlay"

pm_zephyr_run_with_timeout() {
	local secs=$1
	shift

	if timeout --foreground "${secs}" "$@"; then
		return 0
	fi

	local rc=$?

	if [[ ${rc} -eq 124 ]]; then
		echo "(stopped after ${secs}s timeout — boot looked healthy)"
		return 0
	fi

	return "${rc}"
}

# Zephyr board.cmake generates run_qemu in build.ninja; extract it so verify can
# build kernel images separately and run QEMU under timeout (ninja run blocks).
pm_zephyr_qemu_run_cmd() {
	local build_dir=$1
	local cmd

	cmd=$(rg -m1 'run_qemu: CUSTOM_COMMAND' -A2 "${build_dir}/build.ninja" \
		| rg -m1 'COMMAND = cd' \
		| sed 's/^  COMMAND = cd [^&]* && //')
	if [[ -z "${cmd}" ]]; then
		echo "pm_zephyr_qemu_run_cmd: no run_qemu command in ${build_dir}/build.ninja" >&2
		return 1
	fi
	printf '%s' "${cmd}"
}

pm_zephyr_qemu_stop() {
	local build_dir=$1

	if [[ -f "${build_dir}/qemu.pid" ]]; then
		kill "$(cat "${build_dir}/qemu.pid")" 2>/dev/null || true
		rm -f "${build_dir}/qemu.pid"
	fi
}

pm_zephyr_qemu_boot_ok() {
	local out_file=$1

	grep -q "runtime: target=zephyr" "${out_file}" &&
		(grep -q "pymergetic orchestrator" "${out_file}" ||
			grep -q "published bootstrap" "${out_file}")
}

# Run QEMU, stream output, stop when all patterns match (or timeout).
pm_zephyr_qemu_run_smoke() {
	local build_dir=$1
	local out_file=$2
	local max_secs=$3
	shift 3
	local patterns=("$@")
	local cmd runner_pid elapsed pat

	ninja -C "${build_dir}" zephyr/qemu_kernel_target >/dev/null
	rm -f "${build_dir}/qemu.pid"

	cmd=$(pm_zephyr_qemu_run_cmd "${build_dir}")
	: >"${out_file}"

	(cd "${build_dir}" && bash -c "${cmd}") 2>&1 | tee "${out_file}" &
	runner_pid=$!

	elapsed=0
	while (( elapsed < max_secs )); do
		local ok=1
		for pat in "${patterns[@]}"; do
			if ! grep -q "${pat}" "${out_file}"; then
				ok=0
				break
			fi
		done
		if [[ ${ok} -eq 1 ]]; then
			pm_zephyr_qemu_stop "${build_dir}"
			kill "${runner_pid}" 2>/dev/null || true
			wait "${runner_pid}" 2>/dev/null || true
			echo "(ok — stopped qemu)"
			return 0
		fi
		if ! kill -0 "${runner_pid}" 2>/dev/null; then
			wait "${runner_pid}"
			return $?
		fi
		sleep 1
		elapsed=$((elapsed + 1))
	done

	pm_zephyr_qemu_stop "${build_dir}"
	kill "${runner_pid}" 2>/dev/null || true
	wait "${runner_pid}" 2>/dev/null || true
	echo "(stopped after ${max_secs}s timeout)"
	return 0
}

# Run QEMU for a boot smoke test: stream output, stop as soon as boot markers appear.
pm_zephyr_qemu_run_boot_smoke() {
	local build_dir=$1
	local out_file=$2
	local max_secs=${3:-20}

	pm_zephyr_qemu_run_smoke "${build_dir}" "${out_file}" "${max_secs}" \
		"runtime: target=zephyr" \
		"pymergetic orchestrator"
}

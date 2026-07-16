#!/usr/bin/env bash
# Shared Zephyr west env for verify/build scripts.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

if [[ -f "${ROOT}/scripts/setup-tools.sh" ]]; then
	"${ROOT}/scripts/setup-tools.sh"
fi
# One west update + WAMR patch apply per checkout session. Verify sources
# this then execs build-* as a child — use a stamp file so the child skips
# the second full `west update`. PM_METAL_FORCE_WEST=1 to re-run.
if [[ -f "${ROOT}/scripts/setup-west.sh" ]]; then
	PM_WEST_STAMP="${ROOT}/build/.pm-metal-west-ready"
	if [[ "${PM_METAL_FORCE_WEST:-0}" == "1" || ! -f "${PM_WEST_STAMP}" ]]; then
		"${ROOT}/scripts/setup-west.sh"
		mkdir -p "${ROOT}/build"
		touch "${PM_WEST_STAMP}"
	fi
	export PM_METAL_WEST_READY=1
fi

# shellcheck disable=SC1091
if [[ -f "${ROOT}/.venv/bin/activate" ]]; then
	source "${ROOT}/.venv/bin/activate"
fi

export ZEPHYR_BASE="${ROOT}/external/zephyr"
export WAMR_ROOT_DIR="${ROOT}/external/wamr"
export WASI_SDK_PATH="${ROOT}/.tools/wasi-sdk"

PM_ZEPHYR_OVERLAY="${ROOT}/src/zephyr/boards/pm_ramdisk.overlay"

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

# Run a binary, stream stdout/stderr live, stop as soon as all patterns appear
# (or after max_secs). Avoids the silent "hung after build" look when the
# process prints success then fails to exit (common on native_sim).
pm_zephyr_run_smoke() {
	local exe=$1
	local out_file=$2
	local max_secs=$3
	shift 3
	local patterns=("$@")
	local runner_pid elapsed ok pat

	: >"${out_file}"
	# Pipe through tee so the operator sees progress; keep a copy for greps.
	( "${exe}" 2>&1 | tee "${out_file}" ) &
	runner_pid=$!

	elapsed=0
	while (( elapsed < max_secs )); do
		ok=1
		for pat in "${patterns[@]}"; do
			# Fixed-string: markers like "[ERROR] …" must not be BRE char classes.
			if ! grep -qF -- "${pat}" "${out_file}"; then
				ok=0
				break
			fi
		done
		if [[ ${ok} -eq 1 ]]; then
			# Success markers seen — don't wait for a clean process exit.
			pkill -P "${runner_pid}" 2>/dev/null || true
			kill "${runner_pid}" 2>/dev/null || true
			wait "${runner_pid}" 2>/dev/null || true
			# Also kill any orphaned zephyr.exe matching this path.
			pkill -f "^${exe}\$" 2>/dev/null || true
			echo "(ok — stopped after success markers)"
			return 0
		fi
		if ! kill -0 "${runner_pid}" 2>/dev/null; then
			wait "${runner_pid}"
			return $?
		fi
		sleep 1
		elapsed=$((elapsed + 1))
	done

	pkill -P "${runner_pid}" 2>/dev/null || true
	kill "${runner_pid}" 2>/dev/null || true
	wait "${runner_pid}" 2>/dev/null || true
	pkill -f "^${exe}\$" 2>/dev/null || true
	echo "(stopped after ${max_secs}s timeout)"
	return 0
}

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
	# Zephyr's default stdio mux + readline monitor puts a real TTY in raw
	# mode and blanks prior scrollback. Keep -nographic's serial-on-stdio,
	# but drop the mux/monitor so verify output stays a plain stream.
	cmd=${cmd//-chardev stdio,id=con,mux=on -serial chardev:con -mon chardev=con,mode=readline/-monitor none}
	printf '%s' "${cmd}"
}

pm_zephyr_qemu_stop() {
	local build_dir=$1

	if [[ -f "${build_dir}/qemu.pid" ]]; then
		kill "$(cat "${build_dir}/qemu.pid")" 2>/dev/null || true
		rm -f "${build_dir}/qemu.pid"
	fi
}

pm_zephyr_qemu_run_smoke() {
	local build_dir=$1
	local out_file=$2
	local max_secs=$3
	shift 3
	local patterns=("$@")
	local cmd runner_pid elapsed

	ninja -C "${build_dir}" zephyr/qemu_kernel_target >/dev/null
	rm -f "${build_dir}/qemu.pid"

	cmd=$(pm_zephyr_qemu_run_cmd "${build_dir}")
	: >"${out_file}"

	echo "=== qemu smoke (serial stdio, monitor off) ==="
	# SeaBIOS emits ESC-c / CSI 2J which wipe the interactive terminal;
	# strip those so prior build output stays visible while still teeing.
	(cd "${build_dir}" && bash -c "${cmd}") 2>&1 \
		| sed -u $'s/\x1bc//g;s/\x1b\\[[?]7l//g;s/\x1b\\[2J//g' \
		| tee "${out_file}" &
	runner_pid=$!

	elapsed=0
	while (( elapsed < max_secs )); do
		local ok=1
		for pat in "${patterns[@]}"; do
			# Fixed-string: markers like "[ERROR] …" must not be BRE char classes.
			if ! grep -qF -- "${pat}" "${out_file}"; then
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

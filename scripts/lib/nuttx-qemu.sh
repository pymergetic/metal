# NuttX qemu-intel64 run/smoke helpers (serial stdio, optional KVM).
# shellcheck shell=bash

pm_nuttx_qemu_kvm_usable() {
	[[ -e /dev/kvm && -r /dev/kvm && -w /dev/kvm ]]
}

# Build qemu-system-x86_64 command string for kernel at $1.
# Accel notices → stderr. PM_NUTTX_QEMU_ACCEL=auto|kvm|tcg.
pm_nuttx_qemu_cmd() {
	local kernel=$1
	local mode="${PM_NUTTX_QEMU_ACCEL:-auto}"
	local use_kvm=0
	local cpu="qemu64"
	local cmd

	case "${mode}" in
	tcg) use_kvm=0 ;;
	kvm)
		if pm_nuttx_qemu_kvm_usable; then
			use_kvm=1
		else
			echo "nuttx qemu: PM_NUTTX_QEMU_ACCEL=kvm but /dev/kvm not usable — TCG (add user to group kvm and re-login)" >&2
		fi
		;;
	auto|"")
		if pm_nuttx_qemu_kvm_usable; then
			use_kvm=1
		else
			echo "nuttx qemu: TCG (no usable /dev/kvm — add user to group kvm and re-login)" >&2
		fi
		;;
	*)
		echo "nuttx qemu: unknown PM_NUTTX_QEMU_ACCEL=${mode} (use auto|kvm|tcg) — TCG" >&2
		;;
	esac

	# -accel after binary (never as bash -c first token).
	# -nographic already wires UART to stdio; adding -serial stdio conflicts.
	# Drop the default monitor mux so serial is a plain stream (same as Zephyr).
	cmd="/usr/bin/qemu-system-x86_64"
	if [[ ${use_kvm} -eq 1 ]]; then
		cmd+=" -accel kvm"
		cpu="host"
		echo "nuttx qemu: accel=kvm" >&2
	fi
	cmd+=" -cpu ${cpu} -m 2G -kernel ${kernel} -nographic -monitor none -no-reboot"
	printf '%s' "${cmd}"
}

pm_nuttx_qemu_stop() {
	local pidfile=$1
	local pid=""

	if [[ -f "${pidfile}" ]]; then
		pid="$(tr -d '[:space:]' <"${pidfile}" || true)"
		rm -f "${pidfile}"
	fi
	if [[ -n "${pid}" ]]; then
		kill "${pid}" 2>/dev/null || true
		sleep 0.2
		kill -9 "${pid}" 2>/dev/null || true
	fi
}

# Tear down the smoke pipeline (feeder | qemu | tee). Do not wait on the
# feeder's hold-open sleep — kill the job and reap with a short bound.
pm_nuttx_qemu_kill_pipeline() {
	local runner_pid=$1
	local pidfile=$2

	pm_nuttx_qemu_stop "${pidfile}"
	if [[ -n "${runner_pid}" ]]; then
		kill "${runner_pid}" 2>/dev/null || true
		pkill -P "${runner_pid}" 2>/dev/null || true
		local i
		for i in 1 2 3 4 5 6 7 8 9 10; do
			kill -0 "${runner_pid}" 2>/dev/null || break
			sleep 0.1
		done
		kill -9 "${runner_pid}" 2>/dev/null || true
		pkill -9 -P "${runner_pid}" 2>/dev/null || true
		wait "${runner_pid}" 2>/dev/null || true
	fi
}

# grep -a: SeaBIOS/CSI leave NULs that make plain grep treat the log as binary.
pm_nuttx_qemu_grep() {
	grep -a "$@"
}

pm_nuttx_qemu_nsh_prompts() {
	local out_file=$1
	pm_nuttx_qemu_grep -oF 'nsh>' "${out_file}" 2>/dev/null | wc -l
}

# Run kernel; wait for nsh prompt; feed script; stop when all patterns appear.
# Args: <kernel> <nsh_script> <out_file> <max_secs> <patterns...>
pm_nuttx_qemu_run_smoke() {
	local kernel=$1
	local nsh_script=$2
	local out_file=$3
	local max_secs=$4
	shift 4
	local patterns=("$@")
	local cmd runner_pid elapsed pidfile build_dir
	local boot_wait=90

	build_dir="$(dirname "${kernel}")"
	pidfile="${build_dir}/qemu.pid"
	rm -f "${pidfile}"
	: >"${out_file}"

	cmd=$(pm_nuttx_qemu_cmd "${kernel}")
	cmd+=" -pidfile ${pidfile}"

	echo "=== nuttx qemu smoke (serial stdio, monitor off) ==="

	# Pipe stdin (like nuttx sim). Use /usr/bin/printf so each line flushes when
	# the process exits (bash builtins fully-buffer to pipes/fifos).
	# Keep the output path short (tee only) — extra filters can stall serial TX.
	(
		local t=0 line prompts
		while ((t < boot_wait * 4)); do
			if [[ "$(pm_nuttx_qemu_nsh_prompts "${out_file}")" -ge 1 ]]; then
				break
			fi
			sleep 0.25
			t=$((t + 1))
		done
		sleep 0.5
		while IFS= read -r line || [[ -n "${line}" ]]; do
			[[ -z "${line}" ]] && continue
			prompts=$(pm_nuttx_qemu_nsh_prompts "${out_file}")
			/usr/bin/printf '%s\n' "${line}"
			t=0
			while ((t < 2400)); do
				if [[ "$(pm_nuttx_qemu_nsh_prompts "${out_file}")" -gt "${prompts}" ]]; then
					break
				fi
				sleep 0.25
				t=$((t + 1))
			done
		done <"${nsh_script}"
		# Hold pipe open briefly so qemu does not see EOF mid-suite.
		sleep 5
	) | (
		cd "${build_dir}"
		bash -c "${cmd}"
	) 2>&1 \
		| stdbuf -o0 -e0 tee "${out_file}" &
	runner_pid=$!

	elapsed=0
	while ((elapsed < max_secs)); do
		local ok=1
		for pat in "${patterns[@]}"; do
			if ! pm_nuttx_qemu_grep -qF -- "${pat}" "${out_file}"; then
				ok=0
				break
			fi
		done
		if [[ ${ok} -eq 1 ]]; then
			pm_nuttx_qemu_kill_pipeline "${runner_pid}" "${pidfile}"
			echo "(ok — stopped qemu)"
			return 0
		fi
		if ! kill -0 "${runner_pid}" 2>/dev/null; then
			wait "${runner_pid}" 2>/dev/null || true
			echo "nuttx qemu: guest exited before markers matched" >&2
			return 1
		fi
		sleep 1
		elapsed=$((elapsed + 1))
	done

	pm_nuttx_qemu_kill_pipeline "${runner_pid}" "${pidfile}"
	echo "(stopped after ${max_secs}s timeout — markers missing)" >&2
	return 1
}

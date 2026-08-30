#!/usr/bin/env bash
# overnight.sh — the big job: everything provable, in one unattended run.
#
#   tools/overnight.sh [LOGDIR]
#
# Sequence (each stage gated on the previous; failures append to FAILED
# but do not stop later independent stages):
#   1. metal prove-all      — host + upy + browser + firmware seats
#   2. metal selfhost       — the full self-host cycle incl. stage 7 ksweep
#   3. upywm unix seat      — micropython-wasmmod build with the shared wasmmod
#   4. wasmmod engine tests — the gen/face toolchain suite
#
# Output: LOGDIR (default /tmp/overnight_<date>) with one log per stage,
# a SUMMARY.txt, and the ksweep readiness report copied in. Never a prove
# seat itself — tools/ owns it, same posture as selfhost_cycle.sh.
set -uo pipefail

METAL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MP_DIR="$(cd "$METAL_DIR/../.." && pwd)"          # metalpython
UPYWM_DIR="$(cd "$MP_DIR/.." && pwd)/micropython-wasmmod"
EMSDK_ROOT="${EMSDK:-/home/ladmin/emsdk}"

STAMP="$(date +%Y%m%d_%H%M%S)"
LOGDIR="${1:-/tmp/overnight_${STAMP}}"
mkdir -p "$LOGDIR"

step() { printf '\n=== %s [%s]\n' "$*" "$(date +%H:%M:%S)" | tee -a "$LOGDIR/SUMMARY.txt"; }
note() { printf '%s\n' "$*" | tee -a "$LOGDIR/SUMMARY.txt"; }

declare -a FAILED=()

run_stage() {
    # run_stage <name> <cmd...>
    local name="$1"; shift
    local log="$LOGDIR/${name}.log"
    step "$name"
    if "$@" >"$log" 2>&1; then
        note "ok   $name"
    else
        note "FAIL $name (see $log)"
        FAILED+=("$name")
    fi
}

note "overnight run $STAMP"
note "metal:   $METAL_DIR"
note "upywm:   $UPYWM_DIR"
note "logs:    $LOGDIR"

# ---------------------------------------------------------------- 1. prove --
run_stage "metal_prove_all" \
    env EMSDK="$EMSDK_ROOT" make -C "$METAL_DIR" prove-all

# -------------------------------------------------------------- 2. selfhost --
run_stage "metal_selfhost_cycle" \
    bash "$METAL_DIR/tools/selfhost_cycle.sh"

# keep the cycle's artifacts with the run
for f in ksweep.log ksweep_report.txt gen1.c gen2.c gen2_link.c sh_self.o; do
    [ -f "/tmp/selfhost_cycle/$f" ] && cp "/tmp/selfhost_cycle/$f" "$LOGDIR/" 2>/dev/null
done

# --------------------------------------------------------------- 3. upywm ----
if [ -d "$UPYWM_DIR" ]; then
    run_stage "upywm_unix_seat" \
        make -C "$UPYWM_DIR/ports/unix" MICROPY_PY_WASM=1
else
    note "skip upywm_unix_seat (no $UPYWM_DIR)"
fi

# ------------------------------------------------------------ 4. gen suite --
run_stage "wasmmod_engine_tests" \
    env -C "$METAL_DIR/../wasmmod" cargo test --release --no-default-features \
        --features upy-host

# --------------------------------------------------------------- summary ----
step "summary"
if [ ${#FAILED[@]} -eq 0 ]; then
    note "ALL STAGES PASSED"
    RC=0
else
    note "${#FAILED[@]} stage(s) failed: ${FAILED[*]}"
    RC=1
fi
note "logs in $LOGDIR"

exit $RC

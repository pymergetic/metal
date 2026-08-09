#!/usr/bin/env bash
# Grep gates: RegMod is the SoT; parallel declare surfaces must stay dead.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fail=0

check_absent() {
  local label=$1
  local pattern=$2
  shift 2
  if rg -n --glob '!**/tools/regmod_sot_gates.sh' "$pattern" "$@" >/tmp/regmod_gate_hits.txt 2>/dev/null; then
    echo "FAIL $label — unexpected hits:"
    head -40 /tmp/regmod_gate_hits.txt
    fail=1
  else
    echo "ok  $label"
  fi
}

check_max() {
  local label=$1
  local pattern=$2
  local max=$3
  shift 3
  local n
  n=$(rg -c "$pattern" "$@" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}' || true)
  n=${n:-0}
  if [ "$n" -gt "$max" ]; then
    echo "FAIL $label — count $n > max $max"
    fail=1
  else
    echo "ok  $label ($n ≤ $max)"
  fi
}

# Pilot ledger fake rows must stay a no-op (body is just `0`).
if rg -U --multiline 'fn pm_metal_reg_ledger_seed_pilot\(\) -> i32 \{\n    0\n\}' \
  "$ROOT/src/pymergetic/metal/reg/__init__.rs" >/dev/null; then
  echo "ok  seed_pilot is no-op"
else
  echo "FAIL seed_pilot is not a no-op"
  fail=1
fi

check_absent "linker seat section" 'pm_metal_seats' "$ROOT/port/boards"

check_absent "seats_frozen.c" 'seats_frozen' "$ROOT/src" "$ROOT/port/boards"

# Floor glue seats for RegMod modules live in floor_seats.rs — only leftover
# 
check_absent "PM_METAL_REG_SEAT_TEST_ONLY" 'PM_METAL_REG_SEAT_TEST_ONLY' "$ROOT/port" "$ROOT/glue" "$ROOT/src"

check_max "PM_METAL_REG_SEAT in glue" 'PM_METAL_REG_SEAT\(' 0 "$ROOT/glue"

# Floor bind_reg / register_rows must not reappear as module SoT (py late-attach OK).
check_absent "floor bind_reg in RS crates" 'register_rows_bytes' \
  "$ROOT/crates" "$ROOT/src/pymergetic/metal/net" "$ROOT/src/pymergetic/metal/async" \
  "$ROOT/src/pymergetic/metal/fs" "$ROOT/src/pymergetic/metal/mem" \
  "$ROOT/src/pymergetic/metal/util"


# Floor modules must not use register_rows_bytes (dyn late-attach helper only).
if rg -n --glob '!**/reg/_bulk.rs' --glob '!**/reg/__init__.rs' --glob '!**/tools/**' \
  'register_rows_bytes' "$ROOT/src" "$ROOT/crates" >/tmp/regmod_gate_hits.txt 2>/dev/null; then
  echo "FAIL floor register_rows_bytes — unexpected hits:"
  head -40 /tmp/regmod_gate_hits.txt
  fail=1
else
  echo "ok  no floor register_rows_bytes"
fi

# seed_pilot must not be called from product C (no-op stub may remain in RS).
if rg -n --glob '!**/tools/**' 'pm_metal_reg_ledger_seed_pilot\(' \
  "$ROOT/src" "$ROOT/glue" "$ROOT/port" >/tmp/regmod_gate_hits.txt 2>/dev/null; then
  # allow the RS no-op definition only
  if rg -v 'reg/__init__\.rs:.*fn pm_metal_reg_ledger_seed_pilot|ledger\.h:' /tmp/regmod_gate_hits.txt \
    | rg -q .; then
    echo "FAIL seed_pilot call sites remain:"
    rg -v 'reg/__init__\.rs:|ledger\.h:' /tmp/regmod_gate_hits.txt | head -40
    fail=1
  else
    echo "ok  seed_pilot call sites cleared"
  fi
else
  echo "ok  seed_pilot call sites cleared"
fi

# C declare bridge + C-declared floor leaves (must not dual-live in floor_c.rs).
if rg -q 'pm_metal_reg_mod_load_c' "$ROOT/src/pymergetic/metal/reg/_c_desc.rs" \
  && rg -F -q '#define PM_METAL_REG_MOD(' "$ROOT/include/pymergetic/metal/reg/mod.h" \
  && rg -F -q '#define PM_METAL_REG_EXPORT(' "$ROOT/include/pymergetic/metal/reg/mod.h" \
  && ! rg -q 'PM_METAL_PP_MAP' "$ROOT/include/pymergetic/metal/reg/mod.h"; then
  echo "ok  C RegMod bridge"
else
  echo "FAIL C RegMod bridge missing"
  fail=1
fi

# C floor declares live in owning muscle TUs; floor_c/dev.rs are load-only.
if rg -q 'pm_metal_reg_mod_load_c' "$ROOT/src/pymergetic/metal/reg/_c_desc.rs" \
  && rg -q 'pm_metal_boot_mod_reg_load' "$ROOT/crates/pymergetic_metal_rs/src/floor_c.rs" \
  && rg -q 'pm_metal_externals_reg_load' "$ROOT/crates/pymergetic_metal_rs/src/floor_dev.rs" \
  && rg -F -q 'PM_METAL_REG_MOD(net_ip,' "$ROOT/src/pymergetic/metal/net/ip/ip_lwip_netif.c" \
  && rg -F -q 'PM_METAL_REG_MOD(boot_mod,' "$ROOT/src/pymergetic/metal/boot/unboot.c" \
  && rg -F -q 'PM_METAL_REG_MOD(externals,' "$ROOT/src/pymergetic/metal/boot/externals.c" \
  && ! test -e "$ROOT/src/pymergetic/metal/reg/floor_c_reg.c" \
  && ! test -e "$ROOT/src/pymergetic/metal/reg/floor_dev_reg.c"; then
  echo "ok  C muscle RegMod + thin floor load"
else
  echo "FAIL C muscle RegMod / thin floor load missing"
  fail=1
fi

for f in floor_c.rs floor_dev.rs; do
  if rg -n 'reg_mod!' "$ROOT/crates/pymergetic_metal_rs/src/$f" >/dev/null; then
    echo "FAIL $f still has reg_mod! declares:"
    rg -n 'reg_mod!' "$ROOT/crates/pymergetic_metal_rs/src/$f" | head -20
    fail=1
  else
    echo "ok  $f is load-only (no reg_mod!)"
  fi
done

if [ "$fail" -ne 0 ]; then
  echo "regmod_sot_gates: FAIL"
  exit 1
fi
echo "regmod_sot_gates: OK"

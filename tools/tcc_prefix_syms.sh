#!/bin/sh
# tcc_prefix_syms.sh — rename every defined global in a TCC instance object.
#
# The one recipe for cross instances (tools/tcc_instances.mk): one libtcc.c
# compiled per backend, every defined global renamed with an instance prefix
# (objcopy --redefine-sym) so N instances can link into one binary.
#
# Why EVERY defined global, not just tcc_*/wasm_*: the old grep filter
# (`^_?tcc_|^wasm_`) leaks backend globals that happen to have other names —
# gen_negf (tccgen.c, wasm32 + arm both define it: two cross instances in one
# binary COLLIDE on it today by luck of link order), assign_vfpreg,
# decbranch, encbranch, floats_in_core_regs, stuff_const_harder (arm-gen.c).
# Defined-only already excludes the libc imports (strlen, memcpy, ... are
# undefined references — objcopy --prefix-symbols would rename those and
# break every call; redefine-sym on defined globals cannot touch them), and
# the object is only ever called through the hand-written prefixed shims in
# jit/c/__impl__.c, so renaming all of them is self-consistent: no consumer
# names any of these symbols unprefixed.
#
# usage: tcc_prefix_syms.sh <prefix> <raw-object> <final-object> [nm] [objcopy]
#   prefix      e.g. pm_tccw_ (with trailing underscore)
#   raw-object  the freshly compiled object (renamed in place by objcopy)
#   final-object  destination path for the renamed object
# Exit status: 0 ok, 1 usage error, 2 nm/objcopy failure (make treats as error).
set -u

PREFIX="$1"
RAW="$2"
FINAL="$3"
NM="${4:-nm}"
OBJCOPY="${5:-objcopy}"

if [ -z "$PREFIX" ] || [ -z "$RAW" ] || [ -z "$FINAL" ]; then
    echo "usage: tcc_prefix_syms.sh <prefix> <raw-object> <final-object> [nm] [objcopy]" >&2
    exit 1
fi

# Defined globals only (T/D/B/R sections): imports stay untouched.
REDEF="$FINAL.redef"
$NM -g --defined-only "$RAW" \
    | awk '$2 ~ /[TDBR]/ {print $3}' \
    | sed "s/.*/--redefine-sym &=$PREFIX&/" \
    | tr '\n' ' ' > "$REDEF" || exit 2

$OBJCOPY $(cat "$REDEF") "$RAW" "$FINAL" || exit 2
rm -f "$RAW" "$REDEF"
exit 0

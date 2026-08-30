#!/usr/bin/env bash
# selfhost_cycle.sh — the micro-rustc self-host loop in one command.
#
#   tools/selfhost_cycle.sh [--skip-build] [--corpus-only] [--no-ksweep]
#
# Stages (each prints PASS/FAIL and stops the run on the first failure):
#   0. rebuild boot (cargo, real rustc)           [unless --skip-build]
#   1. gen-1: boot compiles __impl__.rs            -> /tmp/sh_gen1.c
#   2. self:  cc builds a compiler from gen-1's C   -> /tmp/sh_self
#   3. gen-2: self compiles __impl__.rs             -> /tmp/sh_gen2.c
#   4. fixed point: gen-2 == gen-1, byte for byte
#   4b. object: gen-2's C through the kernel's own C compiler
#      (jit.c TCC card, linked into the self binary) -> real object
#      bytes. The full loop with no host cc: Rust -> micro-rustc -> C
#      -> TCC -> object, all in-process.
#   4c. link: that object linked in-kernel (build card ELF relocator)
#      and the LINKED compiler's own output byte-compared against
#      gen-1 — the compiler that runs here was itself produced by the
#      kernel's Rust -> C -> TCC -> link chain. No host cc in 4b/4c.
#   5. corpus: every fixtures/*.rs compiles under BOTH boot and self,
#      and both agree byte for byte with the golden outputs when present
#      (fixtures/golden/<name>.c; regenerate with --regen-golden).
#   6. c++: the C++ card's own chain — templates_virtual.cpp lowers to C,
#      TCC (in the same binary) makes the object, the build card's ELF
#      relocator links it, and use() is CALLED out of the linked image and
#      checked against the documented answer. C++ -> C -> TCC -> link ->
#      execute, no host cc/cc1plus anywhere.
#   6b. c++ selfhost: the card transpiles its own __impl__.c to C1, TCC
#      compiles it, the relocator links it in-process, and the LINKED
#      card's lex/parse/lower re-transpile __impl__.c — the output must
#      byte-match C1. The C++ fixed point through the kernel's own chain.
#   7. ksweep: every card in the embedded table through discover ->
#      unit_compile (TCC object + in-kernel link). A readiness map of the
#      whole tree, not a gate: refusals name the next backend seams (TLS
#      relocs, __va_arg, __atomic_store_8).
#
# Why this exists: the 2026-08-28 self-host night burned most of its time on
# loop latency (~35s cargo rebuilds) and a misdiagnosis that a gen1-vs-gen2
# text diff would have exposed in minutes. This script makes that diff the
# first thing you see, not the last.
#
# Never a prove seat: developer tooling owned by tools/.

set -euo pipefail

METAL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WASMMOD_DIR="$METAL_DIR/../wasmmod"
RSX_SRC="$METAL_DIR/src/pymergetic/metal/jit/rs/compiler/__impl__.rs"
CORPUS_DIR="$METAL_DIR/tools/selfhost_corpus"
WORK="${PM_SELFHOST_WORK:-/tmp/selfhost_cycle}"
TARGET_DIR="${PM_SELFHOST_CARGO_TARGET:-/tmp/selfhost_cycle/cargo}"

SKIP_BUILD=0
CORPUS_ONLY=0
REGEN_GOLDEN=0
NO_KSWEEP=0
for arg in "$@"; do
    case "$arg" in
        --skip-build)   SKIP_BUILD=1 ;;
        --corpus-only)  CORPUS_ONLY=1 ;;
        --regen-golden) REGEN_GOLDEN=1 ;;
        --no-ksweep)    NO_KSWEEP=1 ;;
        -h|--help)
            sed -n '2,32p' "$0"; exit 0 ;;
        *)
            echo "unknown flag: $arg (try --help)" >&2; exit 2 ;;
    esac
done

mkdir -p "$WORK" "$TARGET_DIR"

step() { printf '\n=== %s\n' "$*"; }
die()  { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- 0. boot ----
if [ "$CORPUS_ONLY" -eq 0 ]; then
    if [ "$SKIP_BUILD" -eq 0 ]; then
        step "0. rebuild boot (cargo + selfhost feed)"
        ( cd "$METAL_DIR" && \
          CARGO_TARGET_DIR="$TARGET_DIR" cargo build --lib --release \
              --no-default-features --features upy-host \
              2>&1 | grep -aE '^(error|warning: unused)' || true )
        [ -f "$TARGET_DIR/release/libpymergetic_metal.a" ] || die "cargo produced no lib"
        # The feed is the host-prove link config (all cards + TCC + the ELF
        # loader) with the cycle's own entrypoint — its --link mode drives the
        # build card's in-kernel relocator, so stage 4c needs nothing else.
        # CC/CXX stay the host's: this is the one-time boot, everything past
        # it is kernel-only.
        ( cd "$METAL_DIR" && \
          CARGO_TARGET_DIR="$TARGET_DIR" make -B selfhost-feed \
              2>&1 | grep -aE '(error|Error)' || true )
        [ -x "$METAL_DIR/build/selfhost_feed" ] || die "make selfhost-feed produced no binary"
        mkdir -p "$WORK"
        cp "$METAL_DIR/build/selfhost_feed" "$WORK/feed_boot"
    fi
    [ -x "$WORK/feed_boot" ] || die "no $WORK/feed_boot (run without --skip-build once)"

    # ------------------------------------------------------------- 1. gen1 ----
    step "1. gen-1: boot compiles the compiler"
    "$WORK/feed_boot" "$RSX_SRC" "$WORK/gen1.c" >"$WORK/gen1.log" || {
        cat "$WORK/gen1.log"; die "boot compile refused"; }

    # ------------------------------------------------------------- 2. self ----
    step "2. build the self compiler from gen-1 C (kernel cards + gen-1 rsx)"
    # The self binary keeps every card from the boot build (host-prove link
    # config) but swaps the rsx card for gen-1's own C — one card isolated,
    # the rest of the kernel stays the trusted boot build. gen1.o rides the
    # command line before the metal archive, so the archive's rsx member is
    # never extracted (it defines rsx only — explicit objects win) and asgi +
    # the rest of the crate still come from the archive. Stage 4c makes the
    # no-host-cc claim independently: there the whole Rust->C->TCC->link
    # chain runs in-process.
    ( cd "$METAL_DIR" && \
      make selfhost-self SELFHOST_GEN1_C="$WORK/gen1.c" >/dev/null 2>&1 ) \
      || die "make selfhost-self failed"
    [ -x "$METAL_DIR/build/selfhost_self" ] || die "make selfhost-self produced no binary"
    cp "$METAL_DIR/build/selfhost_self" "$WORK/feed_self"

    # ------------------------------------------------------------- 3. gen2 ----
    step "3. gen-2: the self compiler compiles the compiler"
    "$WORK/feed_self" "$RSX_SRC" "$WORK/gen2.c" >"$WORK/gen2.log" || {
        cat "$WORK/gen2.log"; die "self compile refused"; }

    # -------------------------------------------------------- 4. fixed pt ----
    step "4. fixed point: gen-2 == gen-1"
    if cmp -s "$WORK/gen1.c" "$WORK/gen2.c"; then
        echo "FIXED POINT OK ($(stat -c%s "$WORK/gen1.c") bytes identical)"
    else
        echo "--- first differences (gen1 vs gen2) ---"
        diff "$WORK/gen1.c" "$WORK/gen2.c" | head -40 || true
        die "gen-2 differs from gen-1 — NOT a fixed point"
    fi

    # ------------------------------------------------------- 4b. object ----
    step "4b. in-kernel object: self compiles __impl__.rs, TCC makes the object"
    "$WORK/feed_self" --object "$RSX_SRC" "$WORK/gen2.c" "$WORK/sh_self.o" \
        >"$WORK/obj.log" || { cat "$WORK/obj.log"; die "in-kernel object stage refused"; }
    grep -q "^object ok:" "$WORK/obj.log" || { cat "$WORK/obj.log"; die "no object line"; }
    [ "$(stat -c%s "$WORK/sh_self.o")" -gt 512 ] || die "object implausibly small"
    if [ "$(head -c 4 "$WORK/sh_self.o" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]; then
        echo "IN-KERNEL OBJECT OK ($(stat -c%s "$WORK/sh_self.o") bytes, ELF)"
    else
        die "object bytes are not ELF"
    fi

    # -------------------------------------------------------- 4c. link ----
    step "4c. in-kernel link: the object links, the LINKED compiler runs"
    # No host cc from here: feed_self's rsx card IS gen-1 C (a compiler built
    # by the kernel's own chain), it compiles __impl__.rs to C, TCC makes the
    # object, the build card's ELF relocator links it, and the rsx entries
    # are called straight out of the linked image. The file this stage writes
    # is produced by THAT compiler, so a byte-compare against gen-1 proves
    # the fixed point through the whole kernel chain:
    # Rust -> micro-rustc(g) -> C -> TCC -> link -> run, generation g+1.
    "$WORK/feed_self" --link "$RSX_SRC" "$WORK/gen2_link.c" \
        >"$WORK/link.log" || { cat "$WORK/link.log"; die "in-kernel link stage refused"; }
    grep -q "^link: image ok:" "$WORK/link.log" || { cat "$WORK/link.log"; die "no link line"; }
    grep -q "^linked lower ok:" "$WORK/link.log" || { cat "$WORK/link.log"; die "no linked-compile line"; }
    if cmp -s "$WORK/gen1.c" "$WORK/gen2_link.c"; then
        echo "IN-KERNEL LINK OK (linked compiler output == gen-1, $(stat -c%s "$WORK/gen2_link.c") bytes identical)"
    else
        echo "--- first differences (gen1 vs gen2_link) ---"
        diff "$WORK/gen1.c" "$WORK/gen2_link.c" | head -40 || true
        die "linked compiler output differs from gen-1"
    fi
fi

# ------------------------------------------------------------- 5. corpus ----
step "5. corpus (boot and self must agree; golden when present)"
GOLDEN_DIR="$CORPUS_DIR/golden"
if [ "$REGEN_GOLDEN" -eq 1 ]; then
    [ -x "$WORK/feed_boot" ] || die "--regen-golden needs feed_boot (drop --corpus-only)"
    mkdir -p "$GOLDEN_DIR"
fi

[ -x "$WORK/feed_boot" ] || die "no feed_boot for corpus"
[ -x "$WORK/feed_self" ] || die "no feed_self for corpus (drop --corpus-only)"
n_cases=0
n_fail=0
shopt -s nullglob
for rs in "$CORPUS_DIR"/*.rs; do
    name="$(basename "$rs" .rs)"
    n_cases=$((n_cases + 1))
    out_b="$WORK/corpus_${name}_boot.c"
    out_s="$WORK/corpus_${name}_self.c"
    if ! "$WORK/feed_boot" "$rs" "$out_b" >/dev/null 2>"$WORK/corpus_${name}_boot.err"; then
        echo "FAIL $name: boot refused ($(head -c 200 "$WORK/corpus_${name}_boot.err"))"
        n_fail=$((n_fail + 1)); continue
    fi
    if ! "$WORK/feed_self" "$rs" "$out_s" >/dev/null 2>"$WORK/corpus_${name}_self.err"; then
        echo "FAIL $name: self refused ($(head -c 200 "$WORK/corpus_${name}_self.err"))"
        n_fail=$((n_fail + 1)); continue
    fi
    if ! cmp -s "$out_b" "$out_s"; then
        echo "FAIL $name: boot/self outputs differ"
        diff "$out_b" "$out_s" | head -10
        n_fail=$((n_fail + 1)); continue
    fi
    gold="$GOLDEN_DIR/$name.c"
    if [ "$REGEN_GOLDEN" -eq 1 ]; then
        cp "$out_b" "$gold"
        echo "ok   $name (golden refreshed)"
    elif [ -f "$gold" ]; then
        if cmp -s "$out_b" "$gold"; then
            echo "ok   $name (matches golden)"
        else
            echo "FAIL $name: differs from golden (regen with --regen-golden if intended)"
            diff "$gold" "$out_b" | head -10
            n_fail=$((n_fail + 1)); continue
        fi
    else
        echo "ok   $name (no golden yet)"
    fi
done
shopt -u nullglob

if [ "$REGEN_GOLDEN" -eq 1 ]; then
    echo "golden regenerated for $n_cases case(s)"
    exit 0
fi
if [ "$n_fail" -gt 0 ]; then
    die "$n_fail/$n_cases corpus case(s) failed"
fi
echo "corpus ok: $n_cases/$n_cases"

# --------------------------------------------------------------- 6. c++ ----
step "6. c++ chain: lower -> TCC object -> in-kernel link -> run"
# The C++ card's prove mirrors the Rust loop's stages 4b+4c in one shot:
# the card lowers the templates+virtual fixture, the kernel's own TCC makes
# the object, the build card's relocator links it, and use() executes out of
# the linked image. Expected: describe()==2 twice + identity<int>(3)==3 = 7.
CPPX_FEED="$METAL_DIR/build/cppx_feed"
CPPX_FIXTURE="$METAL_DIR/src/pymergetic/metal/jit/cpp/fixtures/templates_virtual.cpp"
CPPX_SRC="$METAL_DIR/src/pymergetic/metal/jit/cpp/__impl__.c"
if [ ! -x "$CPPX_FEED" ]; then
    ( cd "$METAL_DIR" && make cppx-feed >/dev/null 2>&1 ) \
        || die "make cppx-feed failed"
fi
[ -x "$CPPX_FEED" ] || die "no cppx_feed binary"
"$CPPX_FEED" --link use 7 "$CPPX_FIXTURE" "$WORK/cppx_fixture.c" \
    >"$WORK/cppx.log" || { cat "$WORK/cppx.log"; die "c++ chain refused"; }
grep -q "^link: image ok:" "$WORK/cppx.log" || { cat "$WORK/cppx.log"; die "no link line"; }
grep -q "^linked run ok: use() = 7" "$WORK/cppx.log" \
    || { cat "$WORK/cppx.log"; die "linked use() returned the wrong answer"; }
echo "C++ CHAIN OK (templates + virtual dispatch ran from the kernel-linked image)"

# ------------------------------------------------------ 6b. c++ selfhost ----
step "6b. c++ selfhost: the card transpiles ITS OWN source, in-kernel"
# The cppx card's fixed point, the C++ twin of stages 3+4+4c: the card
# transpiles __impl__.c to C1, TCC makes the object, the build card's ELF
# relocator links it in-process, and the LINKED card's own lex/parse/lower
# entries re-transpile __impl__.c. The output must byte-match C1 — the
# compiler that ran was itself produced by the kernel's C++ -> C -> TCC ->
# link chain. No host cc/cc1plus anywhere in 6b.
"$CPPX_FEED" "$CPPX_SRC" "$WORK/cppx_c1.c" >"$WORK/cppx_boot.log" \
    || { cat "$WORK/cppx_boot.log"; die "cppx boot transpile refused"; }
grep -q "^lower ok:" "$WORK/cppx_boot.log" \
    || { cat "$WORK/cppx_boot.log"; die "no cppx lower line"; }
"$CPPX_FEED" --selfhost "$CPPX_SRC" "$WORK/cppx_link.c" >"$WORK/cppx_self.log" \
    || { cat "$WORK/cppx_self.log"; die "cppx selfhost chain refused"; }
grep -q "^link: image ok:" "$WORK/cppx_self.log" \
    || { cat "$WORK/cppx_self.log"; die "no cppx link line"; }
grep -q "^linked lower ok:" "$WORK/cppx_self.log" \
    || { cat "$WORK/cppx_self.log"; die "no linked-lower line"; }
if cmp -s "$WORK/cppx_c1.c" "$WORK/cppx_link.c"; then
    echo "C++ SELFHOST OK (linked card output == boot C1, $(stat -c%s "$WORK/cppx_c1.c") bytes identical)"
else
    echo "--- first differences (boot C1 vs linked) ---"
    diff "$WORK/cppx_c1.c" "$WORK/cppx_link.c" | head -40 || true
    die "linked cppx output differs from boot C1"
fi

# ------------------------------------------------------ 7. ksweep ----
if [ "$NO_KSWEEP" -eq 0 ] && [ "$CORPUS_ONLY" -eq 0 ]; then
step "7. ksweep: every card through the in-kernel compile chain"
# The N-card readiness map: pm_metal_build_discover -> unit_compile (TCC
# object + ELF relocator link) over the whole embedded card table. The
# result lines are data, not gates — a refused card is the next milestone's
# seam (TLS relocs, __va_arg, __atomic_store_8, ...), which is exactly what
# the overnight run wants to enumerate fresh.
( cd "$METAL_DIR" && make ksweep >"$WORK/ksweep.log" 2>&1 ) \
    || { tail -40 "$WORK/ksweep.log"; die "ksweep run failed"; }
grep -q "^ksweep done:" "$WORK/ksweep.log" \
    || { tail -40 "$WORK/ksweep.log"; die "no ksweep summary line"; }
cp "$METAL_DIR/build/ksweep_report.txt" "$WORK/ksweep_report.txt" 2>/dev/null || true
grep "^ksweep done:" "$WORK/ksweep.log"
fi

printf '\nALL STAGES PASSED\n'
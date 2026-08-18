#!/usr/bin/env bash
# Tree × seat × mode. One command:
#   make -C extmod/metal menu
#   ./menu.sh prove mp unix
#   ./menu.sh prove-all
set -euo pipefail

METAL="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
MP="$(CDPATH= cd -- "$METAL/../.." && pwd)"
PACKAGES="$(CDPATH= cd -- "$MP/.." && pwd)"
UPY="$PACKAGES/micropython"
UPYWM="$PACKAGES/micropython-wasmmod"
CDN="$PACKAGES/wasmmod-cdn"
PORT="$METAL/port"

if [[ -t 1 ]] && command -v tput >/dev/null 2>&1; then
    BOLD="$(tput bold 2>/dev/null || true)"
    DIM="$(tput dim 2>/dev/null || true)"
    RESET="$(tput sgr0 2>/dev/null || true)"
else
    BOLD="" DIM="" RESET=""
fi

# tree|seat|modes  (b build, p prove, r run, u upload)
CELLS=(
    "upy|unix|bpr"
    "upy|emcc|bp"
    "upywm|unix|bpr"
    "upywm|emcc|bp"
    "mp|unix|bpr"
    "mp|emcc|bp"
    "mp|bios|bpru"
    "mp|uefi|bpru"
    "mp|rv1106|bpru"
    "mp|cards|bp"
)

TREES=(upy upywm mp)
SEATS=(unix emcc bios uefi rv1106 cards)

cell_modes() {
    local tree="$1" seat="$2" row
    for row in "${CELLS[@]}"; do
        if [[ "$row" == "$tree|$seat|"* ]]; then
            printf '%s\n' "${row##*|}"
            return 0
        fi
    done
    return 1
}

say() {
    printf '%s→%s %s\n' "$DIM" "$RESET" "$*"
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

need_tree() {
    local dir="$1" name="$2"
    [[ -d "$dir" ]] || die "missing $name tree: $dir"
}

need_emcc() {
    if command -v emcc >/dev/null 2>&1; then
        return 0
    fi
    if [[ -n "${EMSDK-}" && -x "${EMSDK}/upstream/emscripten/emcc" ]]; then
        PATH="${EMSDK}/upstream/emscripten:${PATH}"
        export PATH
        return 0
    fi
    die "emcc not on PATH. Set EMSDK to an emsdk checkout (or install emscripten)."
}

unix_flags() {
    case "$1" in
    upy) printf '' ;;
    upywm) printf 'MICROPY_PY_WASM=1' ;;
    mp) printf 'MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 BUILD=build-metal' ;;
    esac
}

unix_root() {
    case "$1" in
    upy) printf '%s\n' "$UPY" ;;
    upywm) printf '%s\n' "$UPYWM" ;;
    mp) printf '%s\n' "$MP" ;;
    esac
}

unix_bin() {
    local root="$1" f
    for f in \
        "$root/ports/unix/build-metal/micropython" \
        "$root/ports/unix/build-wasmmod/micropython" \
        "$root/ports/unix/build-standard/micropython" \
        "$root/ports/unix/build/micropython"; do
        if [[ -x "$f" ]]; then
            printf '%s\n' "$f"
            return 0
        fi
    done
    die "no unix micropython under $root/ports/unix"
}

emcc_flags() {
    case "$1" in
    upy) printf 'VARIANT=standard BUILD=build-standard' ;;
    upywm) printf 'MICROPY_PY_WASM=1 BUILD=build-wasmmod' ;;
    mp) printf 'MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 BUILD=build-metal' ;;
    esac
}

emcc_mjs() {
    local root="$1" f
    for f in \
        "$root/ports/webassembly/build-metal/micropython.mjs" \
        "$root/ports/webassembly/build-wasmmod/micropython.mjs" \
        "$root/ports/webassembly/build-standard/micropython.mjs"; do
        if [[ -f "$f" ]]; then
            printf '%s\n' "$f"
            return 0
        fi
    done
    die "no emcc micropython.mjs under $root/ports/webassembly"
}

fw() {
    local board="$1" goal="$2"
    say "make -C extmod/metal/port BOARD=$board $goal"
    make -C "$PORT" BOARD="$board" "$goal"
}

build_unix() {
    local tree="$1" root flags
    root="$(unix_root "$tree")"
    need_tree "$root" "$tree"
    flags="$(unix_flags "$tree")"
    say "make -C $tree/ports/unix $flags"
    # shellcheck disable=SC2086
    make -C "$root/ports/unix" $flags
}

prove_unix() {
    local tree="$1" bin
    if [[ "$tree" == mp ]]; then
        say "make -C extmod/metal upy"
        make -C "$METAL" upy
        return
    fi
    build_unix "$tree"
    bin="$(unix_bin "$(unix_root "$tree")")"
    case "$tree" in
    upy)
        say "$bin -c 'print(\"upy\")'"
        "$bin" -c 'print("upy")'
        ;;
    upywm)
        say "$bin -c 'import pymergetic.wasmmod'"
        "$bin" -c 'import pymergetic.wasmmod; print("upywm wasmmod")'
        ;;
    esac
}

run_unix() {
    local tree="$1" bin
    build_unix "$tree"
    bin="$(unix_bin "$(unix_root "$tree")")"
    say "$bin"
    # Interactive run seat: boot straight into serving httpd+sshd. prove/one-shot runs never set this.
    export METAL_SERVE=1
    exec "$bin"
}

build_emcc() {
    local tree="$1" root flags
    need_emcc
    root="$(unix_root "$tree")"
    need_tree "$root" "$tree"
    flags="$(emcc_flags "$tree")"
    say "make -C $tree/ports/webassembly $flags"
    # shellcheck disable=SC2086
    make -C "$root/ports/webassembly" $flags
}

prove_emcc() {
    local tree="$1" mjs
    if [[ "$tree" == mp ]]; then
        say "make -C extmod/metal browser"
        make -C "$METAL" browser
        return
    fi
    build_emcc "$tree"
    mjs="$(emcc_mjs "$(unix_root "$tree")")"
    say "built $mjs"
    test -s "$mjs"
}

do_cell() {
    local mode="$1" tree="$2" seat="$3" modes
    if ! modes="$(cell_modes "$tree" "$seat")"; then
        die "$tree $seat is empty (see matrix)"
    fi
    case "$mode" in
    build) [[ "$modes" == *b* ]] || die "$tree $seat has no build" ;;
    prove) [[ "$modes" == *p* ]] || die "$tree $seat has no prove" ;;
    run) [[ "$modes" == *r* ]] || die "$tree $seat has no run" ;;
    upload) [[ "$modes" == *u* ]] || die "$tree $seat has no upload" ;;
    esac
    case "$tree:$seat:$mode" in
    *:unix:build) build_unix "$tree" ;;
    *:unix:prove) prove_unix "$tree" ;;
    *:unix:run) run_unix "$tree" ;;
    *:emcc:build) build_emcc "$tree" ;;
    *:emcc:prove) prove_emcc "$tree" ;;
    mp:cards:build)
        say "cc … build/metal-async-test"
        make -C "$METAL" "$METAL/build/metal-async-test"
        ;;
    mp:cards:prove)
        say "make -C extmod/metal  (C cards, no µPy)"
        make -C "$METAL" "$METAL/build/metal-async-test"
        "$METAL/build/metal-async-test"
        ;;
    mp:bios:build) fw X86_64_BIOS all ;;
    mp:bios:prove) fw X86_64_BIOS prove ;;
    mp:bios:run) fw X86_64_BIOS run ;;
    mp:bios:upload) fw X86_64_BIOS upload ;;
    mp:uefi:build) fw X86_64_UEFI all ;;
    mp:uefi:prove) fw X86_64_UEFI prove ;;
    mp:uefi:run) fw X86_64_UEFI run ;;
    mp:uefi:upload) fw X86_64_UEFI upload ;;
    mp:rv1106:build) fw ARMV7_RV1106 all ;;
    mp:rv1106:prove) fw ARMV7_RV1106 prove ;;
    mp:rv1106:run) fw ARMV7_RV1106 run ;;
    mp:rv1106:upload) fw ARMV7_RV1106 upload ;;
    *) die "unwired $mode $tree $seat" ;;
    esac
}

prove_all() {
    local row tree seat modes
    say "prove-all (build+prove, no run)"
    for row in "${CELLS[@]}"; do
        tree="${row%%|*}"
        seat="${row#*|}"
        seat="${seat%%|*}"
        modes="${row##*|}"
        if [[ "$modes" == *p* ]]; then
            printf '%s\n' "${BOLD}prove $tree $seat${RESET}"
            do_cell prove "$tree" "$seat"
        fi
    done
}

print_matrix() {
    local tree seat modes cell
    printf '%s' "$BOLD"
    printf '%-8s' ''
    for seat in "${SEATS[@]}"; do
        printf '%-8s' "$seat"
    done
    printf '%s\n' "$RESET"
    for tree in "${TREES[@]}"; do
        printf '%-8s' "$tree"
        for seat in "${SEATS[@]}"; do
            if modes="$(cell_modes "$tree" "$seat")"; then
                cell="$modes"
            else
                cell="-"
            fi
            printf '%-8s' "$cell"
        done
        printf '\n'
    done
    printf '\n'
    printf '  %sb%s build  %sp%s prove  %sr%s run  %su%s upload  %s-%s none\n' \
        "$BOLD" "$RESET" "$BOLD" "$RESET" "$BOLD" "$RESET" "$BOLD" "$RESET" "$DIM" "$RESET"
    printf '  %sprove-all%s  every p cell; never run, never upload (no REPL, no QEMU stdio, no Luckfox, no TFTP, no docker)\n' \
        "$BOLD" "$RESET"
    printf '\n'
    printf '  %smp%s metalpython   %supywm%s wasmmod, no metal   %supy%s vanilla µPy\n' \
        "$BOLD" "$RESET" "$BOLD" "$RESET" "$BOLD" "$RESET"
    printf '  QEMU prove is %sprove mp bios|uefi%s. Interactive serial is %srun%s.\n' \
        "$DIM" "$RESET" "$DIM" "$RESET"
    printf '  %supload%s  bios/uefi → TFTP/BOOTP host (%sMETAL_PXE_HOST%s); rv1106 → Maskrom (same as run).\n' \
        "$BOLD" "$RESET" "$DIM" "$RESET"
    printf '  extra: %srun cdn%s  (wasmmod-cdn docker lab)\n' "$DIM" "$RESET"
    printf '  extra: %sbench%s  make -C extmod/metal bench  → host bench runner, numbers never gate\n' "$BOLD" "$RESET"
    printf '  %scards%s  metal C tests, no µPy. Only %smp%s — upywm has wasmmod, not metal.\n' \
        "$BOLD" "$RESET" "$BOLD" "$RESET"
    printf '\n'
    printf '  env: EMSDK  RKBIN  RKTOOLS  LUCKFOX_IP  LUCKFOX_ETHADDR  METAL_PXE_HOST  METAL_PXE_USER  METAL_PXE_PATH  METAL_PXE_SSH_OPTS\n'
}

usage() {
    cat <<EOF
usage: menu.sh                     interactive (mode, then tree, then seat)
       menu.sh list
       menu.sh MODE TREE SEAT      MODE is build|prove|run|upload (b|p|r|u)
       menu.sh prove-all           build+prove every cell; run none
       menu.sh run cdn             CDN docker lab
       make -C extmod/metal bench  host bench runner (numbers never gate)

trees:  upy  upywm  mp
seats:  unix  emcc  bios  uefi  rv1106  cards
EOF
}

mode_char() {
    case "$1" in
    build) printf 'b\n' ;;
    prove) printf 'p\n' ;;
    run) printf 'r\n' ;;
    upload) printf 'u\n' ;;
    esac
}

seats_for() {
    local tree="$1" mode="$2" ch seat modes
    ch="$(mode_char "$mode")"
    for seat in "${SEATS[@]}"; do
        if modes="$(cell_modes "$tree" "$seat" 2>/dev/null)" && [[ "$modes" == *"$ch"* ]]; then
            printf '%s ' "$seat"
        fi
    done
    if [[ "$mode" == run ]]; then
        printf 'cdn '
    fi
    printf '\n'
}

ask() {
    local prompt="$1" default="$2" ans
    if [[ -n "$default" ]]; then
        printf '  %s [%s] ' "$prompt" "$default" >&2
    else
        printf '  %s ' "$prompt" >&2
    fi
    read -r ans || exit 0
    case "$ans" in
    q | Q | quit | exit) exit 0 ;;
    esac
    if [[ -z "$ans" ]]; then
        printf '%s\n' "$default"
    else
        printf '%s\n' "$ans"
    fi
}

walk() {
    local mode="$1" tree="${2-}" seat seats first
    if [[ "$mode" == prove-all ]]; then
        prove_all
        return
    fi
    if [[ -z "$tree" ]]; then
        tree="$(ask "tree?  mp  upywm  upy" "mp")"
    fi
    tree="$(norm_tree "$tree")" || die "unknown tree: $tree"
    seats="$(seats_for "$tree" "$mode")"
    first="${seats%% *}"
    [[ -n "$first" ]] || die "$tree has no $mode seats"
    seat="$(ask "seat?  $seats" "$first")"
    dispatch "$mode" "$tree" "$seat"
}

interactive() {
    local line mode tree
    print_matrix
    printf '  type %sb%s / %sp%s / %sr%s / %su%s / %sa%s, or a full line: p mp unix\n' \
        "$BOLD" "$RESET" "$BOLD" "$RESET" "$BOLD" "$RESET" "$BOLD" "$RESET" "$BOLD" "$RESET"
    line="$(ask "mode?" "p")"
    # shellcheck disable=SC2086
    set -- $line
    if [[ $# -ge 3 ]]; then
        parse_line "$@"
        return
    fi
    mode="$(norm_mode "$1")" || die "unknown mode: $1"
    walk "$mode" "${2-}"
}

norm_mode() {
    case "$1" in
    b | B | build) printf 'build\n' ;;
    p | P | prove) printf 'prove\n' ;;
    r | R | run) printf 'run\n' ;;
    u | U | upload) printf 'upload\n' ;;
    a | A | all | prove-all) printf 'prove-all\n' ;;
    *) return 1 ;;
    esac
}

norm_tree() {
    case "$1" in
    upy | upywm | mp) printf '%s\n' "$1" ;;
    metalpython | metal) printf 'mp\n' ;;
    mpwm | wm) printf 'upywm\n' ;;
    micropython) printf 'upy\n' ;;
    *) return 1 ;;
    esac
}

norm_seat() {
    case "$1" in
    unix | emcc | bios | uefi | rv1106 | cards | cdn) printf '%s\n' "$1" ;;
    host | c) printf 'cards\n' ;;
    browser) printf 'emcc\n' ;;
    *) return 1 ;;
    esac
}

run_cdn() {
    if [[ ! -x "$CDN/scripts/dev-up.sh" ]]; then
        die "cdn lab: missing $CDN/scripts/dev-up.sh"
    fi
    say "$CDN/scripts/dev-up.sh"
    exec "$CDN/scripts/dev-up.sh"
}

dispatch() {
    local mode="$1" tree="${2-}" seat="${3-}"
    if [[ "$mode" == prove-all ]]; then
        prove_all
        return
    fi
    if [[ "$mode" == run && "$tree" == cdn ]]; then
        run_cdn
    fi
    [[ -n "$tree" && -n "$seat" ]] || die "$(usage)"
    tree="$(norm_tree "$tree")" || die "unknown tree: $2"
    seat="$(norm_seat "$seat")" || die "unknown seat: $3"
    if [[ "$seat" == cdn ]]; then
        [[ "$mode" == run ]] || die "cdn is run-only"
        run_cdn
    fi
    do_cell "$mode" "$tree" "$seat"
}

parse_line() {
    local a="${1-}" b="${2-}" c="${3-}" mode
    if [[ -z "$a" ]]; then
        return 1
    fi
    case "$a" in
    q | Q | quit | exit) exit 0 ;;
    esac
    mode="$(norm_mode "$a")" || die "unknown mode: $a"
    dispatch "$mode" "$b" "$c"
}

main() {
    local cmd="${1-}"
    case "$cmd" in
    -h | --help | help)
        usage
        printf '\n'
        print_matrix
        ;;
    list | --list | -l)
        print_matrix
        ;;
    prove-all | all)
        prove_all
        ;;
    build | prove | run | upload | b | p | r | u | B | P | R | U)
        if [[ $# -ge 3 ]]; then
            parse_line "$@"
        elif [[ -t 0 ]]; then
            mode="$(norm_mode "$1")" || die "unknown mode: $1"
            walk "$mode" "${2-}"
        else
            parse_line "$@"
        fi
        ;;
    "")
        if [[ ! -t 0 ]]; then
            print_matrix
            exit 0
        fi
        interactive
        ;;
    *)
        usage >&2
        exit 1
        ;;
    esac
}

main "$@"

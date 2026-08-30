#!/bin/sh
# serial_prove.sh — run a QEMU firmware prove, retry only the SLIRP flake.
#
#   serial_prove.sh <serial.log> <grep-list-file> <cmd> [args...]
#
# The prove reads the guest's serial log and greps a fixed marker list. QEMU
# user-net (SLIRP) very occasionally reorders/drops a segment the guest TCP
# stack never retransmits, so "upy cdn fetch 11" goes missing while every
# other marker printed — a transport hiccup, not a kernel failure. The guest
# already retries the fetch once in its autoexec (firmware_upy_cdn.py); this
# wrapper is the host-side second chance for the rare case the whole TCP
# exchange died. The same race class hits boot DHCP: with two user-net NICs
# SLIRP can flood one (rx hundreds of thousands) so the OFFER/ACK exchange
# loses — the NIC still boots static with no lease, no gw route, no "gw"
# banner line. DHCP is asked once at boot with no retransmit, so the re-run
# is the retry; a server that never answers fails both attempts. Any OTHER
# missing marker or a nonzero guest exit fails immediately — only these two
# lines are retryable, and only 2 tries.
#
# The exit status of the last QEMU run is passed through (the caller decides
# what isa-debug-exit code means), except that a retryable CDN flake turns a
# would-be failure into a re-run.
set -u

serial=$1
markers=$2
shift 2

retryable="upy cdn fetch 11"

# "gw <ip>" from the boot net tree: SLIRP boot-DHCP race (see header).
is_retryable() {
    case "$1" in
    "$retryable") return 0 ;;
    gw\ *) return 0 ;;
    esac
    return 1
}

run_once() {
    # 0 = prove ok, 1 = retryable flake, 2 = hard failure
    rm -f "$serial"
    "$@"
    st=$?
    if [ ! -f "$serial" ]; then
        echo "serial prove: no $serial produced" >&2
        return 2
    fi
    cat "$serial"
    flake=0
    while IFS= read -r m; do
        [ -n "$m" ] || continue
        if grep -q "$m" "$serial"; then
            continue
        fi
        if is_retryable "$m"; then
            echo "serial prove: retryable SLIRP flake (missing: $m)" >&2
            flake=1
            continue
        fi
        echo "serial prove: missing marker: $m" >&2
        return 2
    done <"$markers"
    # QEMU isa-debug-exit: 1 = the guest asked to power off (prove done).
    if [ "$st" -eq 1 ] || [ "$st" -eq 0 ]; then
        if [ "$flake" -eq 1 ]; then return 1; fi
        return 0
    fi
    echo "serial prove: qemu exit $st" >&2
    return 2
}

run_once "$@"
rc=$?
if [ "$rc" -eq 1 ]; then
    echo "serial prove: retrying once after SLIRP flake" >&2
    run_once "$@"
    rc=$?
fi
if [ "$rc" -eq 0 ]; then
    exit 0
fi
exit 1

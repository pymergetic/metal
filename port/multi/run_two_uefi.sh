#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD:-$HERE/port/build/X86_64_UEFI-mp-repl}
QEMU=${QEMU:-qemu-system-x86_64}
OVMF=${OVMF:-/usr/share/ovmf/OVMF.fd}
OUT=${OUT:-$HERE/port/build/multi-uefi}
MCAST=${MCAST:-230.0.0.42:10420}
KEEP=${KEEP:-0}
PYTHON=${PYTHON:-python3}
PLAYWRIGHT_PYTHON=${PLAYWRIGHT_PYTHON:-}
if [[ -z "$PLAYWRIGHT_PYTHON" ]]; then
  if "$PYTHON" -c 'import playwright.sync_api' >/dev/null 2>&1; then
    PLAYWRIGHT_PYTHON=$PYTHON
  elif /usr/bin/python3 -c 'import playwright.sync_api' >/dev/null 2>&1; then
    PLAYWRIGHT_PYTHON=/usr/bin/python3
  else
    echo "multi-seat: Python Playwright is required (set PLAYWRIGHT_PYTHON or install playwright)" >&2
    exit 2
  fi
fi
mkdir -p "$OUT/a" "$OUT/b"
: > "$OUT/a/serial.log"
: > "$OUT/b/serial.log"
rm -f "$OUT/a/uart.sock" "$OUT/b/uart.sock"
cp "$BUILD/esp.img" "$OUT/a/esp.img"
cp "$BUILD/esp.img" "$OUT/b/esp.img"
cp "$BUILD/blk.img" "$OUT/a/blk.img"
cp "$BUILD/blk.img" "$OUT/b/blk.img"
pids=()
cleanup() { for p in "${pids[@]:-}"; do kill "$p" 2>/dev/null || true; done; wait 2>/dev/null || true; }
trap cleanup EXIT INT TERM
launch() {
  local node=$1 octet=$2 http=$3 ssh=$4 wire http_inner ssh_inner
  if [[ $node == a ]]; then wire="listen=127.0.0.1:10420"; else wire="connect=127.0.0.1:10420"; fi
  http_inner=$((http + 10000))
  ssh_inner=$((ssh + 10000))
  "$QEMU" -machine q35,accel=tcg -cpu max,-svm -m 256 -smp 4 \
    -vga none -audio none -display none -bios "$OVMF" \
    -netdev user,id=wan,hostfwd=tcp:127.0.0.1:${http_inner}-10.0.2.15:8090,hostfwd=tcp:127.0.0.1:${ssh_inner}-10.0.2.15:2222 \
    -device virtio-net-pci,disable-legacy=on,netdev=wan,mac=52:54:00:60:00:$(printf %02x "$octet") \
    -netdev socket,id=lan,$wire \
    -device virtio-net-pci,disable-legacy=on,netdev=lan,mac=52:54:00:70:00:$(printf %02x "$octet") \
    -drive file="$OUT/$node/blk.img",if=none,format=raw,id=d0 \
    -device virtio-blk-pci,disable-legacy=on,drive=d0 \
    -drive file="$OUT/$node/esp.img",if=none,format=raw,id=esp \
    -device virtio-blk-pci,disable-legacy=on,drive=esp,bootindex=0 \
    -chardev socket,id=uart,path="$OUT/$node/uart.sock",server=on,wait=off,logfile="$OUT/$node/serial.log" \
    -serial chardev:uart -monitor none >"$OUT/$node/qemu.log" 2>&1 &
  pids+=("$!")
  "$PYTHON" "$HERE/tools/tcp_relay.py" 0.0.0.0 "$http" 127.0.0.1 "$http_inner" \
    >"$OUT/$node/http-relay.log" 2>&1 &
  pids+=("$!")
  "$PYTHON" "$HERE/tools/tcp_relay.py" 0.0.0.0 "$ssh" 127.0.0.1 "$ssh_inner" \
    >"$OUT/$node/ssh-relay.log" 2>&1 &
  pids+=("$!")
}
launch a 11 18091 12221
sleep 1
launch b 12 18092 12222
"$PYTHON" "$HERE/tools/multi_uart.py" "$OUT/a/uart.sock" "$OUT/b/uart.sock"
for _ in $(seq 1 120); do
  if curl --max-time 2 -fsS http://127.0.0.1:18091/health >/dev/null 2>&1 && curl --max-time 2 -fsS http://127.0.0.1:18092/health >/dev/null 2>&1; then break; fi
  sleep 1
done
curl --max-time 2 -fsS http://127.0.0.1:18091/health >/dev/null
curl --max-time 2 -fsS http://127.0.0.1:18092/health >/dev/null
# Let the boot-time HTTP clients finish and release their socket rows before
# Chromium opens the dashboard plus its parallel asset/API connections.
sleep 2
# Render first, while the firmware socket table is fresh. Chromium itself
# polls the same APIs and therefore also waits for mesh convergence.
"$PLAYWRIGHT_PYTHON" "$HERE/tools/multi_browser_e2e.py" http://127.0.0.1:18091 http://127.0.0.1:18092
"$PYTHON" "$HERE/tools/multi_e2e.py" http://127.0.0.1:18091 http://127.0.0.1:18092
if [[ $KEEP == 1 ]]; then
  # Tests use loopback, but a browser on another machine must use this host's
  # routed LAN address. Prefer the source address selected by the route table;
  # hostname -I is only a fallback for hosts without iproute2/default route.
  remote_host=$(ip -4 route get 1.1.1.1 2>/dev/null | awk '''{for (i = 1; i <= NF; i++) if ($i == "src") { print $(i + 1); exit }}''')
  if [[ -z "$remote_host" ]]; then
    remote_host=$(hostname -I 2>/dev/null | awk '''{print $1}''')
  fi
  if [[ -n "$remote_host" ]]; then
    echo "node A http://${remote_host}:18091  node B http://${remote_host}:18092"
  else
    echo "node A port 18091  node B port 18092 (use this host'''s LAN address)"
  fi
  wait
fi

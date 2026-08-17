#!/bin/sh
# Serve the example packs on the loopback, then run a command (the QEMU prove)
# with that server up. A guest on QEMU user-net reaches it at 10.0.2.2:<port>,
# so the pack import in the guest autoexec is a real fetch over a real wire.
#
#   live_cdn.sh <packs-dir> <port> <logfile> <cmd> [args...]
#
# The command's exit status is passed through: the caller decides what QEMU's
# isa-debug-exit code means.
set -u

dir=$1
port=$2
log=$3
shift 3

if [ ! -d "$dir" ]; then
    echo "live cdn: no packs at $dir (make -C extmod/wasmmod/examples)" >&2
    exit 1
fi

# The cdn card asks for <base>/artifacts/lead/<module><ext>, so the packs have
# to answer under that path and not at the server root.
root=${log%/*}/cdnroot
rm -rf "$root"
mkdir -p "$root/artifacts"
ln -s "$(cd "$dir" && pwd)" "$root/artifacts/lead"

python3 -m http.server "$port" --bind 127.0.0.1 --directory "$root" >"$log" 2>&1 &
srv=$!
trap 'kill "$srv" 2>/dev/null' EXIT INT TERM
sleep 0.15
if ! kill -0 "$srv" 2>/dev/null; then
    echo "live cdn: server died (port $port in use? see $log)" >&2
    exit 1
fi

i=0
while [ "$i" -lt 50 ]; do
    if python3 - "$port" <<'PY'
import socket
import sys

s = socket.socket()
s.settimeout(0.2)
sys.exit(0 if s.connect_ex(("127.0.0.1", int(sys.argv[1]))) == 0 else 1)
PY
    then
        break
    fi
    i=$((i + 1))
    sleep 0.1
done
if [ "$i" -ge 50 ]; then
    echo "live cdn: server never came up on 127.0.0.1:$port" >&2
    exit 1
fi

"$@"
exit $?

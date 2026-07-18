#!/usr/bin/env bash
# Generate a merged compile_commands.json + .clangd for this checkout.
# Linux CDB alone makes clangd invent host flags for src/zephyr/* and
# tests/zephyr_verify.* (lots of fake red squiggles). Prefer merging in
# the native_sim CDB when present.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LINUX_BUILD="${ROOT}/build/linux/runtime"
ZEPHYR_BUILD="${ROOT}/build/zephyr/native_sim"
MERGED="${ROOT}/build/compile_commands.json"

cmake -S "${ROOT}" -B "${LINUX_BUILD}"

mkdir -p "${ROOT}/build"
python3 - <<PY
import json
from pathlib import Path

root = Path(${ROOT@Q})
linux = root / "build/linux/runtime/compile_commands.json"
zephyr = root / "build/zephyr/native_sim/compile_commands.json"
out = root / "build/compile_commands.json"
entries = []
seen = set()
for path in (linux, zephyr):
    if not path.is_file():
        continue
    for e in json.loads(path.read_text()):
        key = (e.get("file"), e.get("directory"))
        if key in seen:
            continue
        seen.add(key)
        entries.append(e)

# Newly added zephyr port/{dns,udp,tcp,tls}.c may be absent from a stale
# native_sim CDB. Clone wasi/socket.c's command so clangd does not borrow
# src/linux/.../port/udp.c (same basename) and miss <zephyr/net/socket.h>.
sock = None
for e in entries:
    f = e.get("file") or ""
    if f.endswith("src/zephyr/pymergetic/metal/wasi/socket.c"):
        sock = e
        break
if sock and "command" in sock:
    port_dir = root / "src/zephyr/pymergetic/metal/port"
    for name in ("dns.c", "udp.c", "tcp.c", "tls.c"):
        src = port_dir / name
        if not src.is_file():
            continue
        fpath = str(src.resolve())
        key = (fpath, sock.get("directory"))
        if key in seen:
            continue
        cmd = sock["command"].replace(sock["file"], fpath)
        entries.append({"directory": sock["directory"], "command": cmd, "file": fpath})
        seen.add(key)
        print(f"  synthesized CDB entry: {fpath}")

out.write_text(json.dumps(entries, indent=2) + "\n")
print(f"merged {len(entries)} entries -> {out}")
for path in (linux, zephyr):
    print(f"  source: {path} ({'ok' if path.is_file() else 'missing'})")
PY

ln -sfn "${MERGED}" "${ROOT}/compile_commands.json"
echo "compile_commands.json -> ${MERGED}"

# .clangd needs a few absolute paths (see .clangd.template's own comment for
# why relative ones don't work there) but the absolute value is specific to
# this checkout's own location on disk — .clangd.template is the tracked,
# portable source of truth (placeholder @@ROOT@@), and this generated .clangd
# is gitignored so no machine-specific path ever lands in version control.
sed "s|@@ROOT@@|${ROOT}|g" "${ROOT}/.clangd.template" > "${ROOT}/.clangd"
echo ".clangd -> generated from .clangd.template (ROOT=${ROOT})"

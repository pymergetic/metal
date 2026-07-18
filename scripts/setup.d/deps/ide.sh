#!/usr/bin/env bash
# Generate a merged compile_commands.json + .clangd for this checkout.
# Linux CDB alone makes clangd invent host flags for src/zephyr/* and
# tests/zephyr_verify.* (lots of fake red squiggles). Prefer merging in
# the native_sim CDB when present.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
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

    # qemu-only mbedtls_entropy.c — not in native_sim CDB. Use a dedicated
    # host gcc command (not a socket.c clone): cpptools ignores .clangd and
    # fails on borrowed -nostdinc / flag-order quirks with 'mbedtls/*.h'.
    ent = port_dir / "mbedtls_entropy.c"
    if ent.is_file():
        fpath = str(ent.resolve())
        key = (fpath, str(root))
        if key not in seen:
            mbedtls_i = root / "external/mbedtls/include"
            user_cfg = root / "src/zephyr/mbedtls_user_config.h"
            autoconf = (
                root
                / "build/zephyr/native_sim/zephyr/include/generated/zephyr/autoconf.h"
            )
            # JSON string; quotes inside -D value are escaped for the shell cmd.
            bq = chr(92) + '"'
            cmd = (
                f"/usr/bin/gcc -std=gnu11 -c "
                f"-I{root / 'include'} -I{root / 'src/common'} -I{root / 'src/zephyr'} "
                f"-I{mbedtls_i} "
                f"-DMBEDTLS_USER_CONFIG_FILE={bq}{user_cfg}{bq} "
                f"-DPM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_ZEPHYR "
                f"-DKERNEL -D__ZEPHYR__=1 "
                f"-imacros {autoconf} "
                f"-I{root / 'external/zephyr/include'} "
                f"-I{root / 'build/zephyr/native_sim/zephyr/include/generated'} "
                f"-I{root / 'build/zephyr/native_sim/zephyr/include/generated/zephyr'} "
                f"-I{root / 'external/zephyr/soc/native/inf_clock'} "
                f"-I{root / 'external/zephyr/boards/native/native_sim'} "
                f"-I{root / 'external/zephyr/lib/libc/picolibc/include'} "
                f"-isystem {root / 'build/zephyr/native_sim/modules/picolibc/picolibc/include'} "
                f"-o /dev/null {fpath}"
            )
            entries.append({"directory": str(root), "command": cmd, "file": fpath})
            seen.add(key)
            print(f"  synthesized CDB entry: {fpath}")

    # host_net_adapt.c is a native_simulator *host* TU (glibc + curl), not in
    # the zephyr app CDB — give it a plain host compile command.
    host_adapt = port_dir / "host_net_adapt.c"
    if host_adapt.is_file():
        fpath = str(host_adapt.resolve())
        key = (fpath, str(root))
        if key not in seen:
            curl_i = root / "external/curl/include"
            cmd = (
                f"/usr/bin/gcc -std=gnu11 -c "
                f"-I{root / 'include'} -I{root / 'src/common'} -I{curl_i} "
                f"-o /dev/null {fpath}"
            )
            entries.append({"directory": str(root), "command": cmd, "file": fpath})
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

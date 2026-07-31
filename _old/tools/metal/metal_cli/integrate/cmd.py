"""metal integrate — unpack package for build-against sysroot."""
from __future__ import annotations

import asyncio
import shutil
import sys
import tempfile
from pathlib import Path

from metal_cli.lib.pkgfmt import (
    WASM_CUSTOM_SECTION_NAME,
    extract_custom_section,
    extract_payload_to,
)


async def cmd_integrate(pkg: str, out: str) -> int:
    wasm_path = Path(pkg).resolve()
    dest = Path(out).resolve()
    if not wasm_path.is_file():
        print(f"metal integrate: missing {wasm_path}", file=sys.stderr)
        return 2
    data = wasm_path.read_bytes()
    payload = extract_custom_section(data, WASM_CUSTOM_SECTION_NAME)
    if payload is None:
        print("metal integrate: no metal.pkg section", file=sys.stderr)
        return 1
    if dest.exists():
        shutil.rmtree(dest)
    man = extract_payload_to(payload, dest)
    print(f"metal integrate: {dest}")
    print(man.rstrip())

    # Smoke: if a .h exists, pretends compile with clang -fsyntax-only empty TU
    headers = list(dest.rglob("*.h"))
    if headers and shutil.which("clang"):
        h = headers[0]
        with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as tf:
            tf.write(f'#include "{h.name}"\n')
            tf_path = Path(tf.name)
        try:
            proc = await asyncio.create_subprocess_exec(
                "clang",
                "-fsyntax-only",
                "-I",
                str(h.parent),
                str(tf_path),
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            _out, err = await proc.communicate()
            if proc.returncode == 0:
                print(f"metal integrate: smoke ok ({h.name})")
            else:
                msg = err.decode("utf-8", errors="replace") if err else ""
                print(
                    f"metal integrate: smoke warn (header may be stub):\n{msg}",
                    file=sys.stderr,
                )
        finally:
            tf_path.unlink(missing_ok=True)
    return 0

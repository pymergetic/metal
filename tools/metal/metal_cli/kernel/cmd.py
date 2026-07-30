"""metal kernel build|run — facade over scripts/ and exp2/scripts/."""
from __future__ import annotations

import asyncio
import sys

from metal_cli.paths import exp2_root, metal_root


async def _run(argv: list[str]) -> int:
    proc = await asyncio.create_subprocess_exec(*argv)
    return await proc.wait()


async def cmd_kernel_build(target: str, arch: str | None = None) -> int:
    root = metal_root()
    target = target.lower()
    if target == "exp2":
        script = exp2_root() / "scripts" / "build"
        args = [str(script)]
        if arch:
            args.append(arch)
        return await _run(args)
    if target == "bios":
        script = root / "scripts" / "build"
        args = [str(script), "bios"]
        if arch:
            args.append(arch)
        return await _run(args)
    if target == "efi":
        return await _run([str(root / "scripts" / "build"), "efi"])
    print(f"metal kernel build: unknown target {target!r} (want efi|bios|exp2)", file=sys.stderr)
    return 2


async def cmd_kernel_run(target: str) -> int:
    root = metal_root()
    target = target.lower()
    if target == "exp2":
        return await _run([str(exp2_root() / "scripts" / "run")])
    if target == "bios":
        return await _run([str(root / "scripts" / "run"), "bios", "--no-vnc"])
    if target == "efi":
        return await _run([str(root / "scripts" / "run"), "efi", "--no-vnc"])
    print(f"metal kernel run: unknown target {target!r} (want efi|bios|exp2)", file=sys.stderr)
    return 2


async def cmd_kernel_br(target: str, arch: str | None = None) -> int:
    """Build then run."""
    rc = await cmd_kernel_build(target, arch)
    if rc != 0:
        return rc
    return await cmd_kernel_run(target)
